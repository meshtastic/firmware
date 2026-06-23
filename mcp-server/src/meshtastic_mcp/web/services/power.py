"""Per-node USB power control via uhubctl.

Each bench node sits on a PPPS-capable (per-port-power-switching) hub. uhubctl
can toggle VBUS on a given ``(hub_location, port)``, but its port listing only
exposes VID:PID — not the USB serial — so two same-VID nRF52s can't be told
apart from the hub side. We therefore track the mapping explicitly on the device
row (``hub_location`` / ``hub_port``):

  * ``locate`` auto-binds a device when exactly one PPPS port matches its VID;
  * an ambiguous match (two identical boards) is surfaced for the operator to
    pick the right slot, which is then pinned and survives replug/reboot.

Power actions resolve through that mapping (falling back to a unique VID match)
and are gated by the run-safety lock like any other control action.
"""

from __future__ import annotations

import asyncio
import logging
import shutil
from pathlib import Path

from meshtastic_mcp import uhubctl

from ..db import repo_devices as rd

log = logging.getLogger("meshtastic_mcp.web.power")

_ACTIONS = {"on": uhubctl.power_on, "off": uhubctl.power_off, "cycle": uhubctl.cycle}


class AmbiguousPort(RuntimeError):
    """More than one PPPS port matches the device's VID — operator must pick."""

    def __init__(self, candidates: list[dict]) -> None:
        super().__init__("ambiguous hub port — assign one manually")
        self.candidates = candidates


class NoPort(RuntimeError):
    """No controllable (PPPS) hub port hosts this device."""


def available() -> bool:
    try:
        from meshtastic_mcp import config as mcfg

        binary = mcfg.uhubctl_bin()
        if binary and Path(str(binary)).exists():
            return True
    except Exception:  # noqa: BLE001
        pass
    return shutil.which("uhubctl") is not None


def _vid_int(device: dict) -> int | None:
    vid = device.get("vid")
    if not vid:
        return None
    try:
        return int(str(vid), 16)
    except ValueError:
        return None


def list_hubs() -> list[dict]:
    """Parsed uhubctl hubs (with PPPS flag + per-port attachments)."""
    return uhubctl.list_hubs()


def candidates_for(device: dict) -> list[dict]:
    """All PPPS ``(location, port)`` slots whose attached VID matches the
    device, as ``[{"location", "port"}]``."""
    vid = _vid_int(device)
    if vid is None:
        return []
    return [
        {"location": loc, "port": port}
        for loc, port in uhubctl.find_port_for_vid(vid)
    ]


async def locate(db, serial: str) -> dict:
    """Try to auto-bind the device to its hub port. Returns the resolved
    mapping, or the candidate list when ambiguous so the UI can prompt."""
    device = await rd.get(db, serial)
    if device is None:
        raise LookupError(serial)
    cands = await asyncio.to_thread(candidates_for, device)
    if len(cands) == 1:
        dev = await rd.set_hub_port(
            db, serial, location=cands[0]["location"], port=cands[0]["port"]
        )
        return {"located": True, "device": dev, "candidates": cands}
    return {"located": False, "device": device, "candidates": cands}


async def power_device(db, serial: str, action: str) -> dict:
    """Run a power action against the device's hub port. Resolves the pinned
    mapping first, falling back to a unique VID match; raises ``AmbiguousPort``
    / ``NoPort`` when the slot can't be determined."""
    if action not in _ACTIONS:
        raise ValueError(f"unknown power action: {action}")
    device = await rd.get(db, serial)
    if device is None:
        raise LookupError(serial)

    loc = device.get("hub_location")
    port = device.get("hub_port")
    if loc is None or port is None:
        cands = await asyncio.to_thread(candidates_for, device)
        if not cands:
            raise NoPort("no PPPS hub port hosts this device — assign one manually")
        if len(cands) > 1:
            raise AmbiguousPort(cands)
        loc, port = cands[0]["location"], cands[0]["port"]

    result = await asyncio.to_thread(_ACTIONS[action], loc, port)
    return {"location": loc, "port": port, **result}
