"""Role-to-port rediscovery after USB CDC re-enumeration.

Used by tests that mutate device identity in ways macOS treats as a
"new device" — notably ``factory_reset(full=False)`` on the nRF52840 and
any operation that kicks the device through its bootloader. Both cases
cause the kernel to re-assign the ``/dev/cu.usbmodem*`` path; a test that
captured the pre-operation port and reuses it after will fail with
``FileNotFoundError``.

The helper polls :func:`meshtastic_mcp.devices.list_devices` (the same API
``run-tests.sh`` and ``conftest.py::hub_devices`` use for initial hub
detection) filtered by the role's canonical USB VID. Returns the first
matching port — equivalent to "give me the single nRF52 (or ESP32-S3) on
the bench right now, whichever `cu.*` path it happens to be at".

Test-harness-local (not exported from ``meshtastic_mcp``): a thin wrapper
over public ``devices.list_devices`` with no extra moving parts. If a
non-test caller ever needs this, it's trivial to promote.

Caveat: the session-scoped ``hub_devices`` fixture snapshots ports at
session start and is dict-keyed — it doesn't learn about re-enumerations.
Tests that call ``resolve_port_by_role`` should use the returned port
locally for the rest of the test body rather than expecting
``hub_devices[role]`` to update.
"""

from __future__ import annotations

import time

from meshtastic_mcp import devices as devices_module

# Role → canonical VID(s). Kept in sync with:
#   - `mcp-server/run-tests.sh` (ROLE_BY_VID)
#   - `mcp-server/tests/conftest.py::hub_profile`
# If any of those change, this must too.
_ROLE_VIDS: dict[str, tuple[int, ...]] = {
    "nrf52": (0x239A,),  # Adafruit / RAK nRF52840 native USB
    "esp32s3": (0x303A, 0x10C4),  # Espressif native USB + CP2102 USB-UART
}


def _coerce_vid(raw: object) -> int | None:
    """`devices.list_devices` returns vid as either '0x239a' or an int;
    normalize to int. None on un-parseable input (matches the same fault-
    tolerance `run-tests.sh` uses for its role detection)."""
    if raw is None:
        return None
    if isinstance(raw, int):
        return raw
    if isinstance(raw, str):
        try:
            return int(raw, 16) if raw.lower().startswith("0x") else int(raw)
        except ValueError:
            return None
    return None


def resolve_port_by_role(
    role: str,
    *,
    timeout_s: float = 30.0,
    poll_start: float = 0.5,
    poll_max: float = 5.0,
) -> str:
    """Return the current ``/dev/cu.*`` path for ``role`` once one appears.

    Polls ``devices.list_devices(include_unknown=True)`` every ``poll_start``
    seconds (1.5× backoff, capped at ``poll_max``) until a device matching
    ``role``'s VID appears. Returns the first matching port.

    On timeout raises :class:`AssertionError` with the list of devices that
    WERE seen — helpful when debugging "wrong board connected" vs. "no
    board connected" vs. "still re-enumerating".

    Args:
        role: ``"nrf52"`` or ``"esp32s3"`` (keys of ``_ROLE_VIDS``).
        timeout_s: upper bound on how long to wait for the device to
            re-appear. Default 30 s — nRF52 factory_reset observed at
            2-12 s on a healthy lab hub.
        poll_start: initial poll interval in seconds. Default 0.5 s.
        poll_max: cap on poll interval after backoff. Default 5 s.

    Raises:
        AssertionError: if no matching device appears within ``timeout_s``.
        ValueError: if ``role`` is not in ``_ROLE_VIDS``.

    """
    if role not in _ROLE_VIDS:
        raise ValueError(f"unknown role {role!r}; expected one of {sorted(_ROLE_VIDS)}")
    wanted_vids = _ROLE_VIDS[role]

    deadline = time.monotonic() + timeout_s
    delay = poll_start
    last_seen: list[dict] = []
    while time.monotonic() < deadline:
        try:
            last_seen = devices_module.list_devices(include_unknown=True)
        except Exception as exc:
            # list_devices is wrapped by meshtastic_mcp.devices and
            # shouldn't raise on normal enumeration — but a kernel-level
            # USB hiccup during re-enumeration can bubble up briefly.
            # Treat as "nothing seen this round" and retry.
            last_seen = [{"error": repr(exc)}]
        for dev in last_seen:
            vid = _coerce_vid(dev.get("vid"))
            if vid is not None and vid in wanted_vids and dev.get("port"):
                return dev["port"]
        time.sleep(delay)
        delay = min(delay * 1.5, poll_max)

    # Timeout path — include what we saw so the operator can tell
    # "nothing plugged in" from "wrong VID" from "transient USB error".
    raise AssertionError(
        f"no device matching role {role!r} (VIDs "
        f"{[hex(v) for v in wanted_vids]}) appeared within {timeout_s:.0f}s. "
        f"Last enumeration: {last_seen!r}"
    )
