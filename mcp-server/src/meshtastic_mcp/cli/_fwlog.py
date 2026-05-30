"""Firmware log tail worker for ``meshtastic-mcp-test-tui``.

Complements v1's reportlog-tail worker. ``tests/conftest.py`` owns a
session-scoped autouse fixture (``_firmware_log_stream``) that mirrors
every ``meshtastic.log.line`` pubsub event to ``tests/fwlog.jsonl`` —
one JSON object per line:

    {"ts": 1729100000.123, "port": "/dev/cu.usbmodem1101", "line": "..."}

The TUI tails that file from a worker thread; each new line becomes a
:class:`FirmwareLogLine` message posted to the App. Same pattern as the
reportlog tail worker — truncate on launch, tolerate missing file for
30 s, back off at EOF.

Kept in its own module so the (large) ``test_tui.py`` stays focused on
the Textual App shell.
"""

from __future__ import annotations

import json
import pathlib
import threading
import time
from typing import Any, Callable


class FirmwareLogTailer(threading.Thread):
    """Tail ``tests/fwlog.jsonl``, publish parsed records via ``post``.

    ``post`` is the App's ``post_message`` (or any callable that accepts a
    single payload arg). We pass parsed dicts rather than constructing
    Textual Message objects here — keeps this module free of the
    textual dependency so it's unit-testable in a bare venv.

    Parameters
    ----------
    path:
        Path to ``tests/fwlog.jsonl``. The file may not exist yet at
        startup — pytest only creates it once the session fixture runs.
    post:
        Callable invoked with a dict ``{"ts", "port", "line"}`` for every
        new line parsed from the file.
    stop:
        An event the App sets to signal shutdown.
    wait_s:
        How long to poll for the file's creation before giving up. Default
        30 s; pytest collection on a cold cache can be slow.

    """

    def __init__(
        self,
        path: pathlib.Path,
        post: Callable[[dict[str, Any]], None],
        stop: threading.Event,
        *,
        wait_s: float = 30.0,
    ) -> None:
        super().__init__(daemon=True, name="fwlog-tail")
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
            fh = self._path.open("r", encoding="utf-8")
        except OSError:
            return
        try:
            while not self._stop.is_set():
                line = fh.readline()
                if not line:
                    time.sleep(0.05)
                    continue
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                # Defensive: require the three fields we rely on.
                if not isinstance(record, dict):
                    continue
                if "line" not in record:
                    continue
                self._post(record)
        finally:
            fh.close()
