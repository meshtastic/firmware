"""Cross-run history for ``meshtastic-mcp-test-tui``.

Persists one JSON object per pytest run to
``mcp-server/tests/.history/runs.jsonl``. The TUI reads the last N
entries on launch to render a duration sparkline in the header — a
quick read on whether the suite is slowing down over time.

Schema (keep small; the file can grow for months):

    {"run": 42, "ts": 1729100000.0, "duration_s": 387.2,
     "passed": 52, "failed": 0, "skipped": 23, "exit_code": 0,
     "seed": "mcp-user-host"}
"""

from __future__ import annotations

import json
import pathlib
import time
from dataclasses import asdict, dataclass
from typing import Iterable

# Sparkline glyphs, low → high. 8 levels is the Unicode convention.
_SPARK_BLOCKS = "▁▂▃▄▅▆▇█"


@dataclass
class RunRecord:
    run: int
    ts: float
    duration_s: float
    passed: int
    failed: int
    skipped: int
    exit_code: int
    seed: str


class HistoryStore:
    """Append-only JSONL store with bounded read.

    Writes are fsynced after each append (the file is tiny; fsync cost
    is negligible and protects against truncation on a crash).
    """

    def __init__(self, path: pathlib.Path, *, keep_last: int = 50) -> None:
        self._path = path
        self._keep_last = keep_last

    def append(self, record: RunRecord) -> None:
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            with self._path.open("a", encoding="utf-8") as fh:
                fh.write(json.dumps(asdict(record)) + "\n")
                fh.flush()
        except Exception:
            # Non-fatal: history is cosmetic.
            pass

    def read_recent(self) -> list[RunRecord]:
        """Return the last ``keep_last`` records in chronological order."""
        if not self._path.is_file():
            return []
        try:
            lines = self._path.read_text(encoding="utf-8").splitlines()
        except OSError:
            return []
        out: list[RunRecord] = []
        # Parse tail-first so we don't waste work on a huge history.
        for line in lines[-self._keep_last :]:
            line = line.strip()
            if not line:
                continue
            try:
                raw = json.loads(line)
            except json.JSONDecodeError:
                continue
            try:
                out.append(RunRecord(**raw))
            except TypeError:
                # Schema drift; skip the record rather than crash.
                continue
        return out

    def record_run(
        self,
        *,
        run: int,
        duration_s: float,
        passed: int,
        failed: int,
        skipped: int,
        exit_code: int,
        seed: str,
    ) -> RunRecord:
        rec = RunRecord(
            run=run,
            ts=time.time(),
            duration_s=float(duration_s),
            passed=int(passed),
            failed=int(failed),
            skipped=int(skipped),
            exit_code=int(exit_code),
            seed=seed,
        )
        self.append(rec)
        return rec


def sparkline(values: Iterable[float], *, width: int = 20) -> str:
    """Render a Unicode block-character sparkline from the last ``width`` values.

    Returns an empty string for empty input so the header handles
    "no history yet" gracefully.
    """
    buf = [v for v in values if v >= 0][-width:]
    if not buf:
        return ""
    lo, hi = min(buf), max(buf)
    if hi - lo < 1e-9:
        return _SPARK_BLOCKS[len(_SPARK_BLOCKS) // 2] * len(buf)
    n = len(_SPARK_BLOCKS) - 1
    out = []
    for v in buf:
        idx = int(round((v - lo) / (hi - lo) * n))
        out.append(_SPARK_BLOCKS[max(0, min(n, idx))])
    return "".join(out)
