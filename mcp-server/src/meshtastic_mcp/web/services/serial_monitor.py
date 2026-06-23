"""Live serial monitors, one per device, multiplexed onto the ``serial.<serial>``
WebSocket topic.

A monitor is reference-counted by subscriber: the first client to open a
device's Serial tab spawns a pyserial reader thread that republishes each line;
the last to leave tears it down. Direct pyserial is used (not ``pio device
monitor``, whose miniterm backend requires a controlling TTY and crashes when
run headless under the server). Because the reader holds the USB port, any
control action ``suspend``s it for the duration and ``resume``s after, so the
port is never double-opened.
"""

from __future__ import annotations

import asyncio
import logging
import threading

import serial as pyserial

from meshtastic_mcp import connection

from ..db import repo_devices as rd

log = logging.getLogger("meshtastic_mcp.web.serial_monitor")

BAUD = 115200


class _Monitor:
    def __init__(self) -> None:
        self.refs = 0
        self.suspended = False
        self.stop: threading.Event | None = None
        self.thread: threading.Thread | None = None


class SerialMonitor:
    def __init__(self, db, hub) -> None:
        self.db = db
        self.hub = hub
        self._mons: dict[str, _Monitor] = {}

    def _topic(self, serial: str) -> str:
        return f"serial.{serial}"

    async def acquire(self, serial: str) -> None:
        mon = self._mons.setdefault(serial, _Monitor())
        mon.refs += 1
        if mon.refs == 1 and not mon.suspended:
            await self._open(serial, mon)

    async def release(self, serial: str) -> None:
        mon = self._mons.get(serial)
        if mon is None:
            return
        mon.refs = max(0, mon.refs - 1)
        if mon.refs == 0:
            await self._close(mon)
            self._mons.pop(serial, None)

    async def suspend(self, serial: str) -> None:
        """Free the port for a control action (no-op if not monitored)."""
        mon = self._mons.get(serial)
        if mon is None:
            return
        mon.suspended = True
        await self._close(mon)

    async def resume(self, serial: str) -> None:
        mon = self._mons.get(serial)
        if mon is None:
            return
        mon.suspended = False
        if mon.refs > 0 and mon.thread is None:
            await self._open(serial, mon)

    async def shutdown(self) -> None:
        for mon in list(self._mons.values()):
            await self._close(mon)
        self._mons.clear()

    async def _open(self, serial: str, mon: _Monitor) -> None:
        row = await rd.get(self.db, serial)
        if row is None or row.get("kind") == "native":
            return  # native nodes are TCP — nothing to monitor on the USB bus
        port = row.get("current_port")
        if not port or connection.is_tcp_port(port):
            return
        mon.stop = threading.Event()
        mon.thread = threading.Thread(
            target=self._read_loop, args=(serial, port, mon.stop), daemon=True
        )
        mon.thread.start()

    async def _close(self, mon: _Monitor) -> None:
        if mon.stop is not None:
            mon.stop.set()
        thread = mon.thread
        mon.thread = None
        mon.stop = None
        if thread is not None:
            await asyncio.to_thread(thread.join, 2.0)

    def _read_loop(self, serial: str, port: str, stop: threading.Event) -> None:
        """Runs in a worker thread; publishes lines via the hub's thread-safe
        path. Reads with a short timeout so ``stop`` is honoured promptly."""
        topic = self._topic(serial)
        try:
            ser = pyserial.Serial(port, BAUD, timeout=0.5)
        except Exception as exc:  # noqa: BLE001
            self.hub.publish_threadsafe(topic, {"line": f"— cannot open {port}: {exc} —"})
            return
        self.hub.publish_threadsafe(topic, {"line": f"— monitor opened on {port} —"})
        buf = b""
        try:
            while not stop.is_set():
                try:
                    data = ser.read(256)
                except Exception as exc:  # noqa: BLE001
                    self.hub.publish_threadsafe(topic, {"line": f"— read error: {exc} —"})
                    break
                if not data:
                    continue
                buf += data
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    text = raw.decode("utf-8", "replace").rstrip("\r")
                    if not text:
                        continue
                    # The meshtastic CDC carries protobuf API frames interleaved
                    # with text debug logs. Drop lines that are mostly undecodable
                    # bytes (a protobuf frame) — decoded text logs render with ANSI.
                    bad = text.count("�")
                    if bad and bad > len(text) * 0.2:
                        continue
                    self.hub.publish_threadsafe(topic, {"line": text})
        finally:
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
