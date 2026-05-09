"""Process-global recorder singleton.

Subscribes once to the meshtastic pubsub fan-out and writes four append-only
JSONL streams under `mcp-server/.mtlog/`. The pubsub fan-out is
process-global — a single subscription captures every active interface
without per-connection bookkeeping.

Files:
  logs.jsonl      — every `meshtastic.log.line` event (best-effort prefix
                    parsed for level/tag/uptime; raw `line` always preserved)
  telemetry.jsonl — `meshtastic.receive.telemetry` packets, flattened by
                    variant (device / local / environment / power / etc.)
  packets.jsonl   — every other `meshtastic.receive.*` packet, summarized
                    (portnum, hops, RSSI/SNR, payload size + 64-byte hex)
  events.jsonl    — connection lifecycle, node-DB updates, and manual
                    `mark_event` rows. Lower volume; useful for aligning
                    timelines.

Pause/resume: `pause()` flips a flag; subscriptions stay registered. The
write methods short-circuit when paused, so we don't lose ordering when
resumed (we just have a gap). No queueing.
"""

from __future__ import annotations

import logging
import os
import threading
import time
from pathlib import Path
from typing import Any

from . import parsers
from .rotating import _RotatingJsonl

_DEFAULT_DIR = Path(__file__).resolve().parents[3] / ".mtlog"
log = logging.getLogger(__name__)


class Recorder:
    """Singleton write-side of the persistent log capture system."""

    def __init__(self, base_dir: Path | None = None) -> None:
        self.base_dir = Path(base_dir) if base_dir else _DEFAULT_DIR
        self._lock = threading.RLock()
        self._started = False
        self._paused = False
        self._pause_reason: str | None = None
        self._started_at: float | None = None
        self._handlers: list[tuple[str, Any]] = []
        self._files: dict[str, _RotatingJsonl] = {}

    # -- lifecycle ----------------------------------------------------

    def start(self) -> None:
        """Idempotent. Safe to call from FastMCP app startup."""
        with self._lock:
            if self._started:
                return
            self.base_dir.mkdir(parents=True, exist_ok=True)
            self._files = {
                "logs": _RotatingJsonl(self.base_dir / "logs.jsonl"),
                "telemetry": _RotatingJsonl(self.base_dir / "telemetry.jsonl"),
                "packets": _RotatingJsonl(self.base_dir / "packets.jsonl"),
                "events": _RotatingJsonl(self.base_dir / "events.jsonl"),
            }
            self._wire_pubsub()
            self._started = True
            self._started_at = time.time()
        # Write the recorder_start marker after the initialization block.
        # `_write_event()` re-checks recorder state via `_files_snapshot()`,
        # so keeping this out of the setup block avoids nested lifecycle work.
        self._write_event(kind="recorder_start", label="recorder_started")

    def stop(self) -> None:
        with self._lock:
            if not self._started:
                return
            self._unwire_pubsub()
            for f in self._files.values():
                f.close()
            self._files = {}
            self._started = False

    def pause(self, reason: str | None = None) -> None:
        # Write the pause marker BEFORE flipping the flag — `_write_event`
        # short-circuits when paused, so the order matters for this event
        # to actually land in events.jsonl.
        self._write_event(
            kind="recorder_pause",
            label="paused",
            note=reason,
        )
        with self._lock:
            self._paused = True
            self._pause_reason = reason

    def resume(self) -> None:
        # Mirror of `pause()`: clear the flag first, then write the marker
        # so it isn't suppressed by the still-paused short-circuit.
        with self._lock:
            self._paused = False
            self._pause_reason = None
        self._write_event(kind="recorder_resume", label="resumed")

    # -- pubsub wiring ------------------------------------------------

    def _wire_pubsub(self) -> None:
        from pubsub import pub  # type: ignore[import-untyped]

        # Subscribers — one per topic. Each pubsub publisher sends
        # keyword args matching its handler's signature; pubsub
        # introspects the function signature to route args.
        bindings = [
            ("meshtastic.log.line", self._on_log_line),
            ("meshtastic.serial.line", self._on_serial_line),
            ("meshtastic.receive", self._on_receive),
            ("meshtastic.receive.telemetry", self._on_telemetry),
            ("meshtastic.connection.established", self._on_connection_established),
            ("meshtastic.connection.lost", self._on_connection_lost),
            ("meshtastic.node.updated", self._on_node_updated),
        ]
        for topic, handler in bindings:
            try:
                pub.subscribe(handler, topic)
                self._handlers.append((topic, handler))
            except Exception as exc:
                # If pubsub refuses one binding (signature mismatch on
                # an old lib version), log it and keep the rest.
                log.warning("Recorder failed to subscribe to %s: %s", topic, exc)

    def _unwire_pubsub(self) -> None:
        from pubsub import pub  # type: ignore[import-untyped]

        for topic, handler in self._handlers:
            try:
                pub.unsubscribe(handler, topic)
            except Exception:
                pass
        self._handlers.clear()

    # -- handlers -----------------------------------------------------
    #
    # Pubsub callbacks must never raise. Every handler is wrapped in a
    # try/except that swallows so a bug here can't take down the
    # SerialInterface receive thread.
    #
    # Threading: handlers fire on whatever thread the meshtastic library
    # dispatches from (varies by interface), while `stop()` clears
    # `self._files` under `self._lock`. We snapshot `_files` under the
    # lock at the top of each handler so a concurrent stop can't
    # KeyError us mid-write. The actual file write goes through
    # `_RotatingJsonl` which has its own lock.

    def _files_snapshot(self) -> dict[str, _RotatingJsonl] | None:
        """Atomic-ish view of `self._files`. Returns None when the recorder
        is paused or stopped, so handlers can early-exit cleanly without
        racing `stop()`'s clear."""
        with self._lock:
            if not self._started or self._paused:
                return None
            return dict(self._files)

    def _on_log_line(self, line: str, interface: Any = None) -> None:
        files = self._files_snapshot()
        if files is None:
            return
        try:
            tags = parsers.interface_label(interface)
            parsed = parsers.parse_log_line(str(line))
            ts = time.time()
            record: dict[str, Any] = {
                "ts": ts,
                "port": tags["port"],
                "role": tags["role"],
                "level": parsed.get("level"),
                "tag": parsed.get("tag"),
                "uptime_s": parsed.get("uptime_s"),
                "line": parsed["line"],
            }
            # DEBUG_HEAP enrichments (only present when the firmware
            # was built with -DDEBUG_HEAP=1). Surface as first-class
            # fields so logs_window can grep/filter on them and so
            # heap_free synthesizes a telemetry point below.
            if "heap_free" in parsed:
                record["heap_free"] = parsed["heap_free"]
            if "heap_total" in parsed:
                record["heap_total"] = parsed["heap_total"]
            if "heap_delta" in parsed:
                record["heap_delta"] = parsed["heap_delta"]
            heap_event = parsed.get("heap_event")
            if heap_event:
                record["heap_event"] = heap_event
            files["logs"].write(record)

            # If the line carried a heap snapshot, also write it as a
            # synthesized LocalStats-shaped row so telemetry_timeline
            # picks it up at log cadence (much higher resolution than
            # the ~60 s LocalStats packet). Tagged source=debug_heap so
            # consumers can filter if mixing scales is unwanted.
            heap_free = parsed.get("heap_free")
            if isinstance(heap_free, int):
                fields: dict[str, Any] = {"heap_free_bytes": heap_free}
                heap_total = parsed.get("heap_total")
                if isinstance(heap_total, int):
                    fields["heap_total_bytes"] = heap_total
                files["telemetry"].write(
                    {
                        "ts": ts,
                        "port": tags["port"],
                        "role": tags["role"],
                        "from_node": None,
                        "variant": "local",
                        "fields": fields,
                        "source": "debug_heap",
                    }
                )
        except Exception:
            pass

    def _on_serial_line(self, line: str, port: str | None = None) -> None:
        """Text-mode passive tap. Fired from `serial_session._drain` when a
        `pio device monitor` subprocess is running.

        Same parse + heap-synthesis path as `_on_log_line`, but receives
        the raw text-formatted line (full level/clock/uptime/thread/`[heap N]`/
        body). On DEBUG_HEAP builds in text mode this gives us per-log-line
        heap data — far higher cadence than LocalStats, and works without
        protobuf API mode (no SerialInterface required).
        """
        files = self._files_snapshot()
        if files is None:
            return
        try:
            parsed = parsers.parse_log_line(str(line))
            ts = time.time()
            record: dict[str, Any] = {
                "ts": ts,
                "port": port,
                "role": "serial_session",
                "level": parsed.get("level"),
                "tag": parsed.get("tag"),
                "uptime_s": parsed.get("uptime_s"),
                "line": parsed["line"],
            }
            if "heap_free" in parsed:
                record["heap_free"] = parsed["heap_free"]
            if "heap_total" in parsed:
                record["heap_total"] = parsed["heap_total"]
            if "heap_delta" in parsed:
                record["heap_delta"] = parsed["heap_delta"]
            heap_event = parsed.get("heap_event")
            if heap_event:
                record["heap_event"] = heap_event
            files["logs"].write(record)

            # Synthesize a heap_free telemetry sample whenever the line
            # carries one — same logic as _on_log_line, tagged source so
            # consumers can distinguish text-mode tap from protobuf path.
            heap_free = parsed.get("heap_free")
            if isinstance(heap_free, int):
                fields: dict[str, Any] = {"heap_free_bytes": heap_free}
                heap_total = parsed.get("heap_total")
                if isinstance(heap_total, int):
                    fields["heap_total_bytes"] = heap_total
                files["telemetry"].write(
                    {
                        "ts": ts,
                        "port": port,
                        "role": "serial_session",
                        "from_node": None,
                        "variant": "local",
                        "fields": fields,
                        "source": "debug_heap_serial",
                    }
                )
        except Exception:
            pass

    def _on_telemetry(self, packet: dict[str, Any], interface: Any = None) -> None:
        files = self._files_snapshot()
        if files is None:
            return
        try:
            tags = parsers.interface_label(interface)
            extracted = parsers.extract_telemetry(packet)
            if extracted is None:
                # Couldn't extract a known variant — fall through to the
                # generic `_on_receive` path, which will still fire for
                # this packet via the parent topic.
                return
            record = {
                "ts": time.time(),
                "port": tags["port"],
                "role": tags["role"],
                "from_node": packet.get("fromId") or packet.get("from"),
                "variant": extracted["variant"],
                "fields": extracted["fields"],
                "device_time": extracted.get("time"),
            }
            files["telemetry"].write(record)
        except Exception:
            pass

    def _on_receive(self, packet: dict[str, Any], interface: Any = None) -> None:
        # Generic-receive fires for EVERY packet. Telemetry packets get
        # recorded twice (here and in _on_telemetry) — that's intentional:
        # packets.jsonl is the universal record, telemetry.jsonl is the
        # structured timeseries view.
        files = self._files_snapshot()
        if files is None:
            return
        try:
            tags = parsers.interface_label(interface)
            summary = parsers.summarize_packet(packet)
            record = {
                "ts": time.time(),
                "port": tags["port"],
                "role": tags["role"],
                **summary,
            }
            files["packets"].write(record)
        except Exception:
            pass

    def _on_connection_established(self, interface: Any = None) -> None:
        self._write_event(
            kind="connection_established",
            interface=interface,
        )

    def _on_connection_lost(self, interface: Any = None) -> None:
        self._write_event(
            kind="connection_lost",
            interface=interface,
        )

    def _on_node_updated(
        self, node: dict[str, Any] | None = None, interface: Any = None
    ) -> None:
        # Lower-volume than packets but informative — node ID, hops away,
        # last heard. Skip the user dict if absent.
        try:
            user = (node or {}).get("user") if isinstance(node, dict) else None
            self._write_event(
                kind="node_updated",
                interface=interface,
                data={
                    "num": (node or {}).get("num"),
                    "id": (user or {}).get("id"),
                    "short": (user or {}).get("shortName"),
                    "long": (user or {}).get("longName"),
                    "hops_away": (node or {}).get("hopsAway"),
                    "snr": (node or {}).get("snr"),
                    "last_heard": (node or {}).get("lastHeard"),
                },
            )
        except Exception:
            pass

    # -- public write helpers -----------------------------------------

    def mark_event(
        self,
        label: str,
        note: str | None = None,
        data: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """User-facing marker. Writes to events.jsonl AND emits a
        synthetic logs.jsonl row tagged level=MARK so timelines align.
        """
        ts = self._write_event(kind="mark", label=label, note=note, data=data)
        # Mirror into logs so a single logs_window grep finds it.
        files = self._files_snapshot()
        if files is not None:
            try:
                files["logs"].write(
                    {
                        "ts": ts,
                        "port": None,
                        "role": "marker",
                        "level": "MARK",
                        "tag": "mark_event",
                        "line": f"[mark] {label}" + (f" — {note}" if note else ""),
                    }
                )
            except Exception:
                pass
        return {"ts": ts, "label": label}

    def _write_event(
        self,
        *,
        kind: str,
        label: str | None = None,
        note: str | None = None,
        interface: Any = None,
        data: dict[str, Any] | None = None,
    ) -> float:
        ts = time.time()
        # Lifecycle markers (recorder_start, recorder_pause, recorder_resume)
        # arrive at choreographed moments — `pause()` writes BEFORE flipping
        # the flag and `resume()` writes AFTER clearing it, so those calls
        # see _paused=False here. Other event kinds short-circuit when
        # paused via the snapshot guard below.
        files = self._files_snapshot()
        if files is None:
            return ts
        try:
            tags = parsers.interface_label(interface)
            files["events"].write(
                {
                    "ts": ts,
                    "kind": kind,
                    "label": label,
                    "note": note,
                    "port": tags["port"],
                    "role": tags["role"],
                    "data": data,
                }
            )
        except Exception:
            pass
        return ts

    # -- introspection ------------------------------------------------

    def status(self) -> dict[str, Any]:
        with self._lock:
            return {
                "running": self._started,
                "paused": self._paused,
                "pause_reason": self._pause_reason,
                "started_at": self._started_at,
                "base_dir": str(self.base_dir),
                "files": {name: f.status() for name, f in self._files.items()},
            }

    def force_rotate_all(self) -> dict[str, Any]:
        """Test/admin hook: rotate every stream right now."""
        with self._lock:
            files = list(self._files.values())
        for f in files:
            f.force_rotate()
        # `status()` re-acquires `self._lock`; release before calling it.
        return self.status()


# -- module-level singleton accessor ------------------------------------

_INSTANCE_LOCK = threading.Lock()
_INSTANCE: Recorder | None = None


def get_recorder() -> Recorder:
    """Return the process-global Recorder. Created on first call.

    Honors `MESHTASTIC_MCP_LOG_DIR` env var for the base directory
    (used by tests to redirect to a tmpdir).
    """
    global _INSTANCE
    with _INSTANCE_LOCK:
        if _INSTANCE is None:
            override = os.environ.get("MESHTASTIC_MCP_LOG_DIR")
            base = Path(override) if override else None
            _INSTANCE = Recorder(base_dir=base)
        return _INSTANCE
