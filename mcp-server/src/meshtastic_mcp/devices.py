"""USB/serial + TCP device discovery.

Combines the canonical `meshtastic.util.findPorts()` allowlist/blocklist with
the richer metadata (`serial.tools.list_ports.comports()`) so callers see
VID/PID, descriptions, and manufacturer strings alongside the "is this likely
a Meshtastic device" signal.

If `MESHTASTIC_MCP_TCP_HOST=<host[:port]>` is set, a synthetic entry for the
`meshtasticd` daemon at that endpoint is prepended to the result, so
`resolve_port(None)` auto-selects it like a USB candidate.
"""

from __future__ import annotations

import os
from typing import Any

from serial.tools import list_ports


def _to_hex(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:04x}"


def _tcp_endpoint_from_env() -> dict[str, Any] | None:
    """Synthesize a TCP device entry from MESHTASTIC_MCP_TCP_HOST, if set.

    If the env var is malformed (non-integer port, path-like host, etc.),
    return an entry with `likely_meshtastic=False` and the parser error in
    the description, rather than raising — `list_devices` is the diagnostic
    tool a user reaches for when their env var isn't working, so it must
    not crash on misconfiguration.
    """
    host = os.environ.get("MESHTASTIC_MCP_TCP_HOST")
    if not host:
        return None
    # Lazy import to avoid a circular dependency (connection imports devices).
    from . import connection

    try:
        port = connection.normalize_tcp_endpoint(host)
        description = "meshtasticd (TCP)"
        likely = True
    except connection.ConnectionError as e:
        # Surface the raw env-var value plus the parser's reason so the
        # user can see exactly what they set and why it was rejected.
        # Don't double the scheme if the user already prefixed `tcp://`.
        port = host if host.startswith(connection.TCP_SCHEME) else f"tcp://{host}"
        description = f"meshtasticd (TCP) — invalid MESHTASTIC_MCP_TCP_HOST: {e}"
        likely = False
    return {
        "port": port,
        "vid": None,
        "pid": None,
        "description": description,
        "manufacturer": None,
        "product": None,
        "serial_number": None,
        "likely_meshtastic": likely,
        "blacklisted": False,
    }


def list_devices(include_unknown: bool = False) -> list[dict[str, Any]]:
    """Return enriched info for serial ports, flagging Meshtastic candidates.

    `likely_meshtastic` is True when the port's USB VID matches the Meshtastic
    allowlist (`0x239a` Adafruit/RAK, `0x303a` Espressif). When no allowlisted
    ports are present, ports whose VID is NOT in the blocklist (J-Link, ST-LINK,
    PPK2, etc.) are surfaced as `likely_meshtastic=False` candidates.

    With `include_unknown=False` (default), we return only ports that are
    plausibly Meshtastic. With `include_unknown=True`, every serial port the
    OS knows about is returned (useful for debugging "why isn't my board
    detected").
    """
    # Import lazily so the module loads even without the `meshtastic` package
    # (useful for introspection / schema generation).
    from meshtastic import util as mt_util  # type: ignore[import-untyped]

    meshtastic_ports: set[str] = set(mt_util.findPorts(eliminate_duplicates=True))
    whitelist = getattr(mt_util, "whitelistVids", {})
    blacklist = getattr(mt_util, "blacklistVids", {})

    results: list[dict[str, Any]] = []
    for info in list_ports.comports():
        port_path = info.device
        vid = info.vid
        in_whitelist = vid is not None and vid in whitelist
        in_blacklist = vid is not None and vid in blacklist

        likely = port_path in meshtastic_ports and in_whitelist
        # If no allowlisted ports were found, findPorts falls back to
        # everything-not-in-blacklist; treat those as plausible candidates
        # but not "likely".
        fallback_candidate = port_path in meshtastic_ports and not in_whitelist

        if not likely and not fallback_candidate and not include_unknown:
            continue

        results.append(
            {
                "port": port_path,
                "vid": _to_hex(vid),
                "pid": _to_hex(info.pid),
                "description": info.description or None,
                "manufacturer": info.manufacturer or None,
                "product": info.product or None,
                "serial_number": info.serial_number or None,
                "likely_meshtastic": likely,
                "blacklisted": in_blacklist,
            }
        )

    # Append the TCP endpoint (if env var set) and sort everything together.
    tcp_entry = _tcp_endpoint_from_env()
    if tcp_entry is not None:
        results.append(tcp_entry)

    # Stable ordering: likely_meshtastic first; within rank, TCP wins over
    # USB (explicit env-var configuration takes precedence over USB
    # enumeration); then by port path. A misconfigured TCP entry has
    # likely_meshtastic=False and lands among the other ignored entries —
    # it does NOT pre-empt real USB devices at the top of the list.
    results.sort(
        key=lambda r: (
            not r["likely_meshtastic"],
            not r["port"].startswith("tcp://"),
            r["port"],
        )
    )

    return results
