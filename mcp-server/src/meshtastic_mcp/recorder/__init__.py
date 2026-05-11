"""Persistent device-log capture.

Singleton `Recorder` subscribes once to the meshtastic pubsub fan-out
(`meshtastic.log.line`, `meshtastic.receive.*`, `meshtastic.connection.*`)
and appends to four JSONL files under `mcp-server/.mtlog/`. Pubsub is
process-global so a single subscription captures every active interface
(serial / TCP / BLE) without any per-connection bookkeeping.

The recorder is opt-in-by-import: importing this package is a no-op; call
`get_recorder().start()` (which `server.py` does at FastMCP app init) to
begin writing. `pause()` / `resume()` exist for the rare case the user
wants a clean stretch of file (e.g. capturing a known-good baseline).
"""

from __future__ import annotations

from .recorder import Recorder, get_recorder

__all__ = ["Recorder", "get_recorder"]
