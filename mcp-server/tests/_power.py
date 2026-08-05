"""USB hub power control for tests — thin composition of the `uhubctl`
module + `_port_discovery.resolve_port_by_role`.

Why separate from the production module:
- `meshtastic_mcp.uhubctl.cycle` returns as soon as uhubctl exits (VBUS is
  back on, but the device hasn't finished enumerating as a CDC port yet).
- Tests that want to immediately issue a `connect(port=...)` need the NEW
  `/dev/cu.*` path, which can differ from the pre-cycle path on nRF52
  boards (CDC re-enumeration assigns a fresh `cu.usbmodemNNNN`).
- `resolve_port_by_role` already handles that wait + path-resolution for
  the `factory_reset` flow. Composing the two gives a one-call helper.

Also exposes `is_uhubctl_available()` so fixtures can skip cleanly when
uhubctl isn't installed — we never want "no uhubctl" to look like a test
failure.
"""

from __future__ import annotations

import time
from typing import Any

from meshtastic_mcp import config as config_mod
from meshtastic_mcp import uhubctl as uhubctl_mod

from ._port_discovery import resolve_port_by_role


def is_uhubctl_available() -> bool:
    """Return True iff `config.uhubctl_bin()` resolves AND the binary is callable.

    Soft-fails silently — fixtures use this to `pytest.skip` with an
    actionable message when the operator hasn't installed uhubctl.
    """
    try:
        config_mod.uhubctl_bin()
    except Exception:  # noqa: BLE001
        return False
    # Do NOT actually invoke uhubctl here — on macOS a non-sudo run would
    # fail, which is a config issue, not a tool-missing issue. That gets
    # surfaced to the user when they actually run a recovery action.
    return True


def power_on(role: str) -> dict[str, Any]:
    """Power on the hub port hosting `role`. Does NOT wait for re-enumeration.
    Use `power_cycle` or follow with `resolve_port_by_role` to block on readiness.
    """
    loc, port = uhubctl_mod.resolve_target(role)
    return uhubctl_mod.power_on(loc, port)


def power_off(role: str) -> dict[str, Any]:
    """Power off the hub port hosting `role`. The device disappears from
    `list_devices` immediately.
    """
    loc, port = uhubctl_mod.resolve_target(role)
    return uhubctl_mod.power_off(loc, port)


def power_cycle(
    role: str,
    *,
    delay_s: int = 2,
    rediscover_timeout_s: float = 30.0,
) -> str:
    """Cycle the port hosting `role`, wait for re-enumeration, return the
    new port path.

    On nRF52 the post-cycle path typically matches the pre-cycle path, but
    macOS may assign a different `/dev/cu.usbmodemNNNN` if the previous
    CDC endpoint hasn't been fully released. `resolve_port_by_role`
    handles that transparently.
    """
    loc, port = uhubctl_mod.resolve_target(role)
    uhubctl_mod.cycle(loc, port, delay_s=delay_s)
    # After uhubctl exits, VBUS is on but the device may still be in
    # bootloader init. Give it ~500 ms head-start before polling so we
    # don't spam list_devices pointlessly.
    time.sleep(0.5)
    return resolve_port_by_role(role, timeout_s=rediscover_timeout_s)


def wait_for_absence(role: str, *, timeout_s: float = 10.0) -> None:
    """Block until a device matching `role` is NOT in `list_devices`.

    Used by the recovery tier to assert power_off actually took effect.
    Raises TimeoutError on failure.
    """
    from meshtastic_mcp import devices as devices_mod

    from ._port_discovery import _ROLE_VIDS, _coerce_vid  # type: ignore[attr-defined]

    if role not in _ROLE_VIDS:
        raise ValueError(f"unknown role {role!r}")
    wanted = _ROLE_VIDS[role]
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = devices_mod.list_devices(include_unknown=True)
        if not any(_coerce_vid(d.get("vid")) in wanted for d in found):
            return
        time.sleep(0.3)
    raise TimeoutError(f"role {role!r} still visible after {timeout_s}s of power_off")


__all__ = [
    "is_uhubctl_available",
    "power_cycle",
    "power_off",
    "power_on",
    "wait_for_absence",
]
