"""USB/serial device discovery.

Combines the canonical `meshtastic.util.findPorts()` allowlist/blocklist with
the richer metadata (`serial.tools.list_ports.comports()`) so callers see
VID/PID, descriptions, and manufacturer strings alongside the "is this likely
a Meshtastic device" signal.
"""

from __future__ import annotations

from typing import Any

from serial.tools import list_ports


def _to_hex(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:04x}"


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

    # Stable ordering: likely_meshtastic first, then by port path
    results.sort(key=lambda r: (not r["likely_meshtastic"], r["port"]))
    return results
