"""Append-only JSONL writer with size-capped rotation.

A `_RotatingJsonl` owns one live `.jsonl` file. Writes are line-delimited
JSON objects (one row per call). When the live file exceeds `max_bytes`,
it is closed, gzipped to `<name>.YYYYMMDD-HHMMSS-uuuuuu-NNNNN.jsonl.gz`,
and the live file resets to empty. Old archives past `keep_archives` are
unlinked oldest-first.

Size check is amortized — `os.fstat` runs every `check_every` writes,
not per-write, so the hot path stays at one `fh.write` + one `fh.flush`.

Threading: every public method acquires `self._lock`. The recorder runs
several pubsub handlers on whatever thread the meshtastic library
dispatches from (varies by interface), and queries from MCP tool calls
arrive on the FastMCP request thread, so this lock is not optional.
"""

from __future__ import annotations

import gzip
import json
import os
import shutil
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


class _RotatingJsonl:
    """Append-only JSONL with size rotation. Thread-safe."""

    def __init__(
        self,
        path: Path,
        *,
        max_bytes: int = 100 * 1024 * 1024,
        keep_archives: int = 5,
        check_every: int = 1000,
    ) -> None:
        self.path = path
        self.max_bytes = max_bytes
        self.keep_archives = keep_archives
        self.check_every = check_every
        self._lock = threading.Lock()
        self._fh: Any = None
        self._writes_since_check = 0
        self._rotations = 0
        self._lines_written = 0
        self._last_ts: float | None = None
        self._open()

    # -- lifecycle ----------------------------------------------------

    def _open(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = self.path.open("a", encoding="utf-8")

    def close(self) -> None:
        with self._lock:
            if self._fh is not None:
                try:
                    self._fh.close()
                finally:
                    self._fh = None

    # -- write --------------------------------------------------------

    def write(self, record: dict[str, Any]) -> None:
        """Append one JSON object as a line. Triggers rotation if oversized."""
        line = json.dumps(record, separators=(",", ":"), default=str) + "\n"
        with self._lock:
            if self._fh is None:
                return
            try:
                self._fh.write(line)
                self._fh.flush()
            except Exception:
                # Best-effort: a failed write must not crash the pubsub
                # handler. Caller has no way to react anyway.
                return
            self._lines_written += 1
            ts = record.get("ts")
            if isinstance(ts, (int, float)):
                self._last_ts = float(ts)
            self._writes_since_check += 1
            if self._writes_since_check >= self.check_every:
                self._writes_since_check = 0
                self._maybe_rotate()

    # -- rotation -----------------------------------------------------

    def _maybe_rotate(self) -> None:
        # Caller holds self._lock.
        try:
            size = os.fstat(self._fh.fileno()).st_size
        except OSError:
            return
        if size < self.max_bytes:
            return
        self._rotate_locked()

    def _rotate_locked(self) -> None:
        # Close, gzip-rename, reopen empty, prune oldest archives.
        try:
            self._fh.close()
        except Exception:
            pass
        self._fh = None
        # Microsecond-resolution timestamp + per-instance counter so back-
        # to-back rotations (small max_bytes, repeated `force_rotate()`,
        # or chatty test loops) get unique archive filenames. The lex
        # sort order of `YYYYMMDD-HHMMSS-uuuuuu-NNNNN` is chronological,
        # which `_prune_archives()` and `log_query._iter_jsonl()` both
        # rely on.
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
        archive = self.path.with_suffix(f".{stamp}-{self._rotations:05d}.jsonl.gz")
        try:
            with self.path.open("rb") as src, gzip.open(archive, "wb") as dst:
                shutil.copyfileobj(src, dst, length=1024 * 1024)
            self.path.unlink()
        except Exception:
            # Rotation is best-effort. If gzip fails, leave the file
            # in place and re-open it; we'll try again next check.
            pass
        self._open()
        self._rotations += 1
        self._prune_archives()

    def _prune_archives(self) -> None:
        # Match siblings of self.path.name with `.jsonl.gz` suffix.
        prefix = self.path.stem  # "logs" for "logs.jsonl"
        # Archive filenames are already lexicographically chronological.
        # Prune by name, not mtime, so copied/restored files don't reorder.
        archives = sorted(self.path.parent.glob(f"{prefix}.*.jsonl.gz"))
        excess = len(archives) - self.keep_archives
        for old in archives[: max(0, excess)]:
            try:
                old.unlink()
            except OSError:
                pass

    def force_rotate(self) -> None:
        """Test/admin hook: rotate immediately regardless of size."""
        with self._lock:
            if self._fh is not None:
                self._rotate_locked()

    # -- introspection ------------------------------------------------

    def status(self) -> dict[str, Any]:
        with self._lock:
            try:
                size = os.fstat(self._fh.fileno()).st_size if self._fh else 0
            except OSError:
                size = 0
            return {
                "path": str(self.path),
                "size": size,
                "lines": self._lines_written,
                "last_ts": self._last_ts,
                "rotations": self._rotations,
            }
