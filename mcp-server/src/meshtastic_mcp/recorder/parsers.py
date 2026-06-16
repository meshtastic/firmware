"""Best-effort parsers for log lines and telemetry packets.

Two flavors of log line cross our pubsub subscription:
  1. Text-mode path (debug_log_api disabled): the meshtastic Python lib
     accumulates bytes between protobuf frames and emits the full
     firmware-formatted line, e.g.
         "INFO  | 12:34:56 12345 [Main] Booting"
     — level, HH:MM:SS, uptime seconds, thread bracket, then message.
  2. LogRecord protobuf path (debug_log_api enabled): the lib calls
     `_handleLogLine(record.message)` with ONLY the message body. The
     level/source/time fields on the LogRecord are dropped before
     pubsub fan-out. We get e.g. just "Booting".

Both arrive on `meshtastic.log.line`. The parser tries to recover a
level + thread when the prefix is present and falls back to level=None
otherwise. Consumers who want level filtering on protobuf-mode hosts
should grep the raw `line` field instead.

Telemetry: `meshtastic.receive.telemetry` packets carry one of several
metric variants in `packet["decoded"]["telemetry"]`. We flatten the
chosen variant into a {field: value} dict so callers don't have to
know the protobuf shape.
"""

from __future__ import annotations

import re
from typing import Any

# Match: LEVEL | HH:MM:SS UPTIME [Thread] message
# HH:MM:SS may be ??:??:?? when RTC isn't valid. The level alternation
# below is the canonical list — DebugConfiguration.h's MESHTASTIC_LOG_LEVEL_*
# macros must stay in sync with these strings.
_LINE_RE = re.compile(
    r"""
    ^
    (?P<level>DEBUG|INFO\ |WARN\ |ERROR|CRIT\ |TRACE|HEAP\ )
    \s*\|\s*
    (?P<clock>(?:\d{2}:\d{2}:\d{2})|(?:\?{2}:\?{2}:\?{2}))
    \s+
    (?P<uptime>\d+)
    \s+
    (?:\[(?P<thread>[^\]]+)\]\s+)?
    (?P<msg>.*)
    $
    """,
    re.VERBOSE,
)

# DEBUG_HEAP build prepends `[heap N] ` to every message body, AFTER the
# thread bracket. See src/RedirectablePrint.cpp:175.
_HEAP_PREFIX_RE = re.compile(r"^\[heap\s+(?P<heap>\d+)\]\s+(?P<rest>.*)$")

# OSThread leak/free detection. See src/concurrency/OSThread.cpp:89-91.
# Format: "------ Thread NAME leaked heap A -> B (delta) ------"
#         "++++++ Thread NAME freed heap A -> B (delta) ++++++"
_THREAD_HEAP_RE = re.compile(
    r"""
    ^[\-+]+\s*
    Thread\s+(?P<thread>\S+)\s+
    (?P<kind>leaked|freed)\s+heap\s+
    (?P<before>-?\d+)\s*->\s*(?P<after>-?\d+)\s+
    \((?P<delta>-?\d+)\)
    """,
    re.VERBOSE,
)

# Power.cpp:908 periodic heap status (DEBUG_HEAP only).
# Format: "Heap status: FREE/TOTAL bytes free (DELTA), running R/N threads"
_HEAP_STATUS_RE = re.compile(
    r"""
    Heap\s+status:\s+
    (?P<free>\d+)\s*/\s*(?P<total>\d+)\s+bytes\s+free
    (?:\s+\((?P<delta>-?\d+)\))?
    """,
    re.VERBOSE,
)


_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_HEAP_BRACKET_RE = re.compile(r"^heap\s+(?P<heap>\d+)$")


def parse_log_line(line: str) -> dict[str, Any]:
    """Best-effort decompose a raw firmware log line.

    Returns a dict with at least `line` (the original, unmodified — ANSI
    codes preserved for fidelity). Adds `level`, `tag`, `clock`,
    `uptime_s`, and `msg` when the full prefix is present.

    Handles two firmware quirks:
      - LogRecord.message can carry ANSI color escapes from RedirectablePrint
        (the BLE/StreamAPI path inherited the colored body in some builds).
        We strip ANSI before regex matching so the prefix survives.
      - DEBUG_HEAP injects `[heap N]` after the thread bracket. When NO
        thread name is set, the heap takes the thread bracket position —
        looks like `[heap 12345] msg`. We detect that shape and move it
        out of `tag` and into `heap_free`.

    DEBUG_HEAP-build extras (when `[heap N]` is injected): `heap_free`
    (bytes), and when a `Thread X leaked|freed heap` line is recognized,
    `heap_event` = {kind, thread, before, after, delta}.

    Never raises.
    """
    out: dict[str, Any] = {"line": line}
    if not line:
        return out

    # Strip ANSI escapes BEFORE any regex matching. The original `line`
    # stays in `out["line"]` for fidelity / future grep.
    clean = _ANSI_RE.sub("", line)

    m = _LINE_RE.match(clean)
    msg: str | None = None
    if m:
        level = m.group("level").rstrip()
        out["level"] = level
        out["clock"] = m.group("clock")
        try:
            out["uptime_s"] = int(m.group("uptime"))
        except (TypeError, ValueError):
            out["uptime_s"] = None
        thread = m.group("thread")
        if thread:
            # If "thread" is actually the heap prefix taking the bracket
            # position (DEBUG_HEAP build, no thread set), capture heap
            # and leave tag unset.
            hb = _HEAP_BRACKET_RE.match(thread.strip())
            if hb:
                try:
                    out["heap_free"] = int(hb.group("heap"))
                except (TypeError, ValueError):
                    pass
            else:
                out["tag"] = thread
        msg = m.group("msg")
        out["msg"] = msg
    else:
        # No prefix — bare LogRecord.message body. Inspect the whole
        # line for DEBUG_HEAP-style content; the heap-prefix and
        # thread-leak patterns can survive on either path.
        msg = clean

    # DEBUG_HEAP per-line heap prefix: `[heap 92344] message`.
    # Sits AFTER the thread bracket and BEFORE the message body, but
    # for bare LogRecord lines it's at the start. Match it at the
    # head of `msg`.
    if msg:
        hp = _HEAP_PREFIX_RE.match(msg)
        if hp:
            try:
                out["heap_free"] = int(hp.group("heap"))
            except (TypeError, ValueError):
                pass
            else:
                # Strip the prefix from `msg` so a grep on the message
                # body doesn't have to know about it.
                out["msg"] = hp.group("rest")
                msg = hp.group("rest")

        # Thread-level leak/free detection.
        thr = _THREAD_HEAP_RE.search(msg)
        if thr:
            try:
                out["heap_event"] = {
                    "kind": thr.group("kind"),
                    "thread": thr.group("thread"),
                    "before": int(thr.group("before")),
                    "after": int(thr.group("after")),
                    "delta": int(thr.group("delta")),
                }
            except (TypeError, ValueError):
                pass

        # Power.cpp periodic "Heap status: F/T bytes free (D), running ..."
        hs = _HEAP_STATUS_RE.search(msg)
        if hs:
            try:
                out["heap_free"] = int(hs.group("free"))
                out["heap_total"] = int(hs.group("total"))
                if hs.group("delta") is not None:
                    out["heap_delta"] = int(hs.group("delta"))
            except (TypeError, ValueError):
                pass

    return out


# -- Telemetry ----------------------------------------------------------

# Order matters: meshtastic-python decoded packets use the protobuf
# `oneof variant` field name (snake_case) as the dict key.
_TELEMETRY_VARIANTS = (
    ("device_metrics", "device"),
    ("local_stats", "local"),
    ("environment_metrics", "environment"),
    ("power_metrics", "power"),
    ("air_quality_metrics", "airQuality"),
    ("health_metrics", "health"),
    ("host_metrics", "host"),
)


def extract_telemetry(packet: dict[str, Any]) -> dict[str, Any] | None:
    """Pull the telemetry variant + flat fields out of a `meshtastic.receive.telemetry`
    packet. Returns None when the shape isn't what we expect — so the
    caller can fall back to a generic packets.jsonl row.
    """
    if not isinstance(packet, dict):
        return None
    decoded = packet.get("decoded")
    if not isinstance(decoded, dict):
        return None
    telem = decoded.get("telemetry")
    if not isinstance(telem, dict):
        return None
    # The Python lib produces dict-of-camelCase keys via MessageToDict.
    # Try both camelCase and snake_case to be robust to lib version drift.
    for snake, label in _TELEMETRY_VARIANTS:
        camel = _snake_to_camel(snake)
        for key in (snake, camel):
            value = telem.get(key)
            if isinstance(value, dict):
                return {
                    "variant": label,
                    "fields": {k: _scalarize(v) for k, v in value.items()},
                    "time": telem.get("time"),
                }
    return None


def _snake_to_camel(name: str) -> str:
    parts = name.split("_")
    return parts[0] + "".join(p.title() for p in parts[1:])


def _scalarize(value: Any) -> Any:
    """Keep telemetry fields JSON-friendly. Lists/dicts pass through
    untouched; bytes -> hex string; protobuf enums occasionally arrive
    as ints (fine) or strings (also fine)."""
    if isinstance(value, (bytes, bytearray, memoryview)):
        return bytes(value).hex()
    return value


# -- Generic packet summary ---------------------------------------------


def summarize_packet(
    packet: dict[str, Any], *, payload_hex_len: int = 64
) -> dict[str, Any]:
    """Reduce a packet dict to a stable, queryable summary. Drops the
    full payload bytes — the recorder records summaries, not pcaps.
    """
    if not isinstance(packet, dict):
        return {"raw_type": type(packet).__name__}
    decoded = packet.get("decoded") if isinstance(packet.get("decoded"), dict) else {}
    portnum = decoded.get("portnum") if isinstance(decoded, dict) else None
    payload = decoded.get("payload") if isinstance(decoded, dict) else None
    payload_hex = None
    payload_size = None
    if isinstance(payload, (bytes, bytearray, memoryview)):
        b = bytes(payload)
        payload_size = len(b)
        payload_hex = b[:payload_hex_len].hex() if b else ""
    elif isinstance(payload, str):
        # Some decoded payloads (text messages) come as decoded strings.
        payload_size = len(payload)
        payload_hex = None  # not bytes
    return {
        "from_node": packet.get("fromId") or packet.get("from"),
        "to_node": packet.get("toId") or packet.get("to"),
        "portnum": portnum,
        "hop_limit": packet.get("hopLimit"),
        "want_ack": packet.get("wantAck"),
        "rx_rssi": packet.get("rxRssi"),
        "rx_snr": packet.get("rxSnr"),
        "channel": packet.get("channel"),
        "id": packet.get("id"),
        "payload_size": payload_size,
        "payload_hex_prefix": payload_hex,
    }


# -- Interface identification ------------------------------------------


def interface_label(interface: Any) -> dict[str, Any]:
    """Stable identifier for the meshtastic interface that emitted an event.

    Used as the `port`/`role` tag on every recorded row. SerialInterface
    has `devPath`; TCPInterface has `hostname`+`portNumber`; BLEInterface
    has `address`. Falls back to the class name when none of those exist.
    """
    if interface is None:
        return {"port": None, "role": None}
    dev_path = getattr(interface, "devPath", None)
    if dev_path:
        return {"port": str(dev_path), "role": "serial"}
    hostname = getattr(interface, "hostname", None)
    if hostname:
        port_num = getattr(interface, "portNumber", None)
        endpoint = f"tcp://{hostname}:{port_num}" if port_num else f"tcp://{hostname}"
        return {"port": endpoint, "role": "tcp"}
    address = getattr(interface, "address", None)
    if address:
        return {"port": str(address), "role": "ble"}
    return {"port": type(interface).__name__, "role": None}
