"""UI-capture transcript tailer for ``meshtastic-mcp-test-tui``.

Watches ``tests/ui_captures/<session_seed>/`` for new transcript lines
(one per ``frame_capture()`` call from the UI tier) and posts them to
the TUI. Enabled by ``MESHTASTIC_UI_TUI_CAMERA=1``.

Design mirrors ``_flashlog.py``:
- Daemon thread, cooperative stop via ``threading.Event``.
- Tolerates the captures directory not existing yet (UI tier hasn't run).
- Per-file seek state so we only forward genuinely-new lines.
"""

from __future__ import annotations

import pathlib
import threading
import time
from typing import Callable


class UiCaptureTailer(threading.Thread):
    """Recursively watch a captures root for new `transcript.md` lines.

    Invokes ``post(test_id, line)`` for each new line, where ``test_id``
    is derived from the path — the sanitized nodeid directory name.
    """

    def __init__(
        self,
        root: pathlib.Path,
        post: Callable[[str, str], None],
        stop: threading.Event,
        *,
        poll_interval: float = 0.5,
    ) -> None:
        super().__init__(daemon=True, name="uicap-tail")
        self._root = root
        self._post = post
        self._stop = stop
        self._poll_interval = poll_interval
        # path → byte offset we've already read through
        self._offsets: dict[pathlib.Path, int] = {}

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                self._scan_once()
            except Exception:
                # Best-effort tailer — never bring down the TUI because a
                # directory vanished mid-scan.
                pass
            time.sleep(self._poll_interval)

    def _scan_once(self) -> None:
        if not self._root.is_dir():
            return
        for transcript in self._root.rglob("transcript.md"):
            test_id = transcript.parent.name
            offset = self._offsets.get(transcript, 0)
            try:
                size = transcript.stat().st_size
            except OSError:
                continue
            if size < offset:
                # File truncated / rewritten — reset and re-emit.
                offset = 0
            if size == offset:
                continue
            try:
                with transcript.open("rb") as fh:
                    fh.seek(offset)
                    chunk = fh.read(size - offset).decode("utf-8", errors="replace")
            except OSError:
                continue
            for line in chunk.splitlines():
                line = line.rstrip()
                if not line or line.startswith("#"):
                    continue
                try:
                    self._post(test_id, line)
                except Exception:
                    return
            self._offsets[transcript] = size
