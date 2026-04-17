"""Flash progress log tailer for ``meshtastic-mcp-test-tui``.

``pio.py`` / ``hw_tools.py`` tee subprocess output (``pio run -t upload``,
``esptool erase_flash``, ``nrfutil dfu``, etc.) to ``tests/flash.log``
line-by-line as it arrives — controlled by the ``MESHTASTIC_MCP_FLASH_LOG``
env var that ``run-tests.sh`` sets. The TUI tails that file so the operator
sees live flash progress in the pytest pane instead of 3 minutes of silence
during ``test_00_bake``.

Separate from ``_fwlog.py`` because that one parses JSONL, this one
streams plain text lines. Same daemon-thread + EOF-backoff structure.
"""

from __future__ import annotations

import pathlib
import threading
import time
from typing import Callable


class FlashLogTailer(threading.Thread):
    """Tail a plain-text log file, publish each stripped line via ``post``.

    ``post`` is invoked with a single ``str`` for every new line. Lines are
    stripped of trailing newlines; empty lines after stripping are dropped.

    The file may not exist yet when this thread starts — it's truncated by
    ``run-tests.sh`` at session start, but if the tailer races the shell,
    we tolerate FileNotFoundError for up to ``wait_s`` seconds.
    """

    def __init__(
        self,
        path: pathlib.Path,
        post: Callable[[str], None],
        stop: threading.Event,
        *,
        wait_s: float = 30.0,
    ) -> None:
        super().__init__(daemon=True, name="flashlog-tail")
        self._path = path
        self._post = post
        self._stop = stop
        self._wait_s = wait_s

    def run(self) -> None:
        deadline = time.monotonic() + self._wait_s
        while not self._path.is_file():
            if self._stop.is_set() or time.monotonic() > deadline:
                return
            time.sleep(0.1)
        try:
            fh = self._path.open("r", encoding="utf-8", errors="replace")
        except OSError:
            return
        try:
            while not self._stop.is_set():
                line = fh.readline()
                if not line:
                    time.sleep(0.05)
                    continue
                line = line.rstrip("\r\n")
                if not line:
                    continue
                try:
                    self._post(line)
                except Exception:
                    # A post failure (e.g. closed app) is terminal for this
                    # thread but we still want to close the file handle.
                    return
        finally:
            fh.close()
