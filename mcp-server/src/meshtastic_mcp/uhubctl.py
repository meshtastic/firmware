"""USB hub power control via `uhubctl` — hard-recovery for wedged devices +
deliberate offline-peer simulation for mesh tests.

Why: when a Meshtastic device's serial port wedges (stuck in a boot loop,
frozen USB CDC, crashed firmware that didn't reboot), the only recovery is
a physical unplug. uhubctl toggles VBUS per-port on any hub with Per-Port
Power Switching (PPPS) support — which is most externally-powered hubs
from the last ~5 years — so the harness can power-cycle a device
programmatically.

Architecture:
- `list_hubs()` parses `uhubctl` default output into structured records.
- `find_port_for_vid(vid)` walks the hubs to find which location+port
  hosts a given USB VID.
- `resolve_target(role)` is the public entry for callers that know a role
  (`nrf52`, `esp32s3`) but not a hub location: env-var pins win, VID
  auto-detect falls back.
- `power_on`, `power_off`, `cycle` wrap the corresponding `uhubctl -a`
  invocations, routed through `hw_tools._run` so they share tee-to-flash-
  log + timeout handling with esptool / nrfutil / picotool.

Sudo policy: **fail fast**. Modern macOS + most PPPS-capable hubs work
without root, but Linux without udev rules (or old macOS with specific
driver quirks) still needs it. We run uhubctl non-root; if stderr
matches the classic permission pattern we raise `UhubctlError` with an
install hint pointing at the uhubctl docs. Auto-wrapping with `sudo`
would prompt in the middle of test runs — bad for CI.
"""

from __future__ import annotations

import os
import re
from typing import Any, Sequence

from . import config, hw_tools

# ---------- Parser ---------------------------------------------------------

# Hub descriptor line:
#   Current status for hub 1-1.3 [2109:2817 VIA Labs, Inc. USB2.0 Hub, USB 2.10, 4 ports, ppps]
_HUB_RE = re.compile(
    r"^Current status for hub (?P<location>\S+)\s+\[(?P<descriptor>.+)\]\s*$"
)

# Port line:
#   "  Port 2: 0103 power enable connect [239a:8029 RAKwireless ...]"
# The bracketed section is absent for empty ports.
_PORT_RE = re.compile(
    r"^\s+Port\s+(?P<port>\d+):\s+(?P<status>\S+)\s+(?P<flags>.*?)"
    r"(?:\s+\[(?P<device_vid>[0-9a-fA-F]{4}):(?P<device_pid>[0-9a-fA-F]{4})(?:\s+(?P<device_desc>.+))?\])?\s*$"
)


class UhubctlError(RuntimeError):
    """Raised on uhubctl-specific failures: parse errors, permission denied,
    hub-not-found, or PPPS not supported."""


# ---------- Role → VID map -------------------------------------------------

# Mirrors the default hub_profile in `mcp-server/tests/conftest.py:335`.
# Note: esp32s3 and esp32s3_alt share a logical role — we search both.
ROLE_VIDS: dict[str, tuple[int, ...]] = {
    "nrf52": (0x239A,),
    "esp32s3": (0x303A, 0x10C4),
}


def _normalize_role(role: str) -> str:
    """Collapse `esp32s3_alt` → `esp32s3` to match the tier conventions."""
    return role.split("_alt", 1)[0].lower()


# ---------- Core subprocess runner -----------------------------------------


# If uhubctl hits a permission problem — most commonly Linux without the
# udev rules, or a macOS variant where the kernel holds the hub driver —
# it prints something like "Permission denied. Try running as root".
# Linux error text varies; we match a broad substring rather than exact.
_PERM_ERROR_PATTERNS = (
    "permission denied",
    "operation not permitted",
    "try running as root",
    "need root",
    "requires root",
)


def _run_uhubctl(args: Sequence[str], *, timeout: float = 30.0) -> dict[str, Any]:
    """Invoke uhubctl with the given args. Returns `hw_tools._run`'s dict.

    Translates permission-denied failures into a `UhubctlError` with the
    install hint, so callers don't have to match stderr themselves. Other
    non-zero exits are returned as-is for the caller to interpret.
    """
    binary = config.uhubctl_bin()
    result = hw_tools._run(binary, args, timeout=timeout)  # noqa: SLF001
    if result["exit_code"] != 0:
        combined = (result.get("stderr") or "") + "\n" + (result.get("stdout") or "")
        lower = combined.lower()
        if any(pat in lower for pat in _PERM_ERROR_PATTERNS):
            raise UhubctlError(
                "uhubctl exited with a permission error. Install the udev "
                "rules on Linux, or try `sudo` as a fallback: "
                "https://github.com/mvp/uhubctl#linux-usb-permissions\n"
                f"stderr: {result.get('stderr_tail')!r}"
            )
    return result


# ---------- List / parse ---------------------------------------------------


def parse_list_output(output: str) -> list[dict[str, Any]]:
    """Parse the default `uhubctl` stdout into structured hubs.

    Each hub: {
        "location":   "1-1.3",
        "descriptor": "2109:2817 VIA Labs ...",
        "vid":        0x2109,
        "pid":        0x2817,
        "ppps":       bool,
        "ports":      [{"port": int, "status": str, "flags": str,
                         "device_vid": int | None, "device_pid": int | None,
                         "device_desc": str | None}, ...],
    }
    """
    hubs: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None

    for line in output.splitlines():
        hm = _HUB_RE.match(line)
        if hm:
            descriptor = hm.group("descriptor")
            hub_vid, hub_pid = None, None
            vid_match = re.match(r"([0-9a-fA-F]{4}):([0-9a-fA-F]{4})", descriptor)
            if vid_match:
                hub_vid = int(vid_match.group(1), 16)
                hub_pid = int(vid_match.group(2), 16)
            current = {
                "location": hm.group("location"),
                "descriptor": descriptor,
                "vid": hub_vid,
                "pid": hub_pid,
                "ppps": ", ppps" in descriptor or descriptor.endswith("ppps"),
                "ports": [],
            }
            hubs.append(current)
            continue

        pm = _PORT_RE.match(line)
        if pm and current is not None:
            device_vid = pm.group("device_vid")
            device_pid = pm.group("device_pid")
            current["ports"].append(
                {
                    "port": int(pm.group("port")),
                    "status": pm.group("status"),
                    "flags": (pm.group("flags") or "").strip(),
                    "device_vid": int(device_vid, 16) if device_vid else None,
                    "device_pid": int(device_pid, 16) if device_pid else None,
                    "device_desc": (pm.group("device_desc") or "").strip() or None,
                }
            )
    return hubs


def list_hubs() -> list[dict[str, Any]]:
    """Enumerate every hub uhubctl can see, with per-port device attachments.

    Pure read — no power state changes. Useful as a pre-flight check before
    a destructive `power_off` call.
    """
    result = _run_uhubctl([], timeout=15.0)
    if result["exit_code"] != 0:
        raise UhubctlError(
            f"uhubctl list failed (exit {result['exit_code']}): {result.get('stderr_tail')!r}"
        )
    return parse_list_output(result["stdout"])


# ---------- Lookup / resolution -------------------------------------------


def find_port_for_vid(
    vid: int, pid: int | None = None, *, only_ppps: bool = True
) -> list[tuple[str, int]]:
    """Return ALL (location, port) matches for a device VID (optionally +PID).

    `only_ppps=True` filters out hubs that don't advertise PPPS — we can't
    control them anyway. Callers that want to diagnose a missing device can
    pass `only_ppps=False` to see if the device is on a non-controllable
    hub (and raise a clearer error).
    """
    hubs = list_hubs()
    matches: list[tuple[str, int]] = []
    for hub in hubs:
        if only_ppps and not hub["ppps"]:
            continue
        for port in hub["ports"]:
            if port["device_vid"] != vid:
                continue
            if pid is not None and port["device_pid"] != pid:
                continue
            matches.append((hub["location"], port["port"]))
    return matches


def resolve_target(role: str) -> tuple[str, int]:
    """Resolve a Meshtastic role to (hub_location, port_number).

    Priority:
      1. Env vars `MESHTASTIC_UHUBCTL_LOCATION_<ROLE>` + `_PORT_<ROLE>`
         (e.g. `MESHTASTIC_UHUBCTL_LOCATION_NRF52=1-1.3`, `_PORT_NRF52=2`).
      2. VID auto-detect against `ROLE_VIDS[role]`, taking the first PPPS
         match.

    Raises `UhubctlError` on ambiguity (multiple matches) or no-match. The
    env-var path exists specifically to disambiguate when two devices share
    a VID.
    """
    role = _normalize_role(role)
    env_key_loc = f"MESHTASTIC_UHUBCTL_LOCATION_{role.upper()}"
    env_key_port = f"MESHTASTIC_UHUBCTL_PORT_{role.upper()}"
    loc = os.environ.get(env_key_loc)
    port_str = os.environ.get(env_key_port)
    if loc and port_str:
        try:
            return (loc, int(port_str))
        except ValueError as exc:
            raise UhubctlError(
                f"{env_key_port}={port_str!r} is not a valid integer"
            ) from exc

    if role not in ROLE_VIDS:
        raise UhubctlError(
            f"unknown role {role!r}; known roles: {sorted(ROLE_VIDS)}. "
            f"Set {env_key_loc} + {env_key_port} to pin manually."
        )

    matches: list[tuple[str, int]] = []
    for vid in ROLE_VIDS[role]:
        matches.extend(find_port_for_vid(vid))

    if not matches:
        vids = ", ".join(f"0x{v:04x}" for v in ROLE_VIDS[role])
        raise UhubctlError(
            f"no controllable hub hosts a device with VID in {{{vids}}} "
            f"for role={role!r}. Check the device is plugged into a "
            f"PPPS-capable hub, or pin manually via {env_key_loc} + {env_key_port}."
        )
    if len(matches) > 1:
        shown = ", ".join(f"{loc}:port{p}" for loc, p in matches)
        raise UhubctlError(
            f"ambiguous: multiple devices match role={role!r} ({shown}). "
            f"Pin the target via {env_key_loc} + {env_key_port}."
        )
    return matches[0]


# ---------- Power actions --------------------------------------------------


def _action(
    action: str,
    location: str,
    port: int,
    *,
    delay_s: int | None = None,
    timeout: float = 30.0,
) -> dict[str, Any]:
    args: list[str] = ["-a", action, "-l", location, "-p", str(port)]
    if delay_s is not None:
        args.extend(["-d", str(delay_s)])
    # Suppress verbose "before" printout so our parser doesn't have to skip it.
    args.append("-N")
    result = _run_uhubctl(args, timeout=timeout)
    if result["exit_code"] != 0:
        raise UhubctlError(
            f"uhubctl -a {action} -l {location} -p {port} failed "
            f"(exit {result['exit_code']}): {result.get('stderr_tail')!r}"
        )
    return {
        "action": action,
        "location": location,
        "port": port,
        "delay_s": delay_s,
        "duration_s": result["duration_s"],
    }


def power_on(location: str, port: int) -> dict[str, Any]:
    """Drive the port VBUS high. Device re-enumerates in 1-3 s on healthy hubs."""
    return _action("on", location, port)


def power_off(location: str, port: int) -> dict[str, Any]:
    """Drive the port VBUS low. Device disappears from `list_devices` immediately."""
    return _action("off", location, port)


def cycle(location: str, port: int, delay_s: int = 2) -> dict[str, Any]:
    """Off → wait `delay_s` → on. The common hard-reset pattern."""
    # uhubctl's own `-a cycle` handles the delay internally; we use a
    # slightly longer timeout to accommodate delay_s + enumeration.
    return _action("cycle", location, port, delay_s=delay_s, timeout=30.0 + delay_s * 2)


__all__ = [
    "ROLE_VIDS",
    "UhubctlError",
    "cycle",
    "find_port_for_vid",
    "list_hubs",
    "parse_list_output",
    "power_off",
    "power_on",
    "resolve_target",
]
