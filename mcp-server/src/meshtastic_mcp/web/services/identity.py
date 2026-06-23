"""Device identity reconciliation.

Two jobs: (1) map a USB VID to a coarse *role* and a role to a default pio
*env*, and resolve the precise env from a board's hw_model when we know it;
(2) derive a *stable key* for a device so a unit with a real serial number is
tracked across ports, while a serial-less unit gets a (port-derived) surrogate
that is explicitly NOT stable.
"""

from __future__ import annotations

import hashlib

from meshtastic_mcp import boards

# USB vendor IDs we recognise → coarse role. Case-insensitive; compared as the
# lowercased hex string (e.g. "0x239a").
_VID_ROLE = {
    "0x239a": "nrf52",  # Adafruit / RAK nRF52840
    "0x303a": "esp32s3",  # Espressif native USB
    "0x10c4": "esp32s3",  # Silicon Labs CP210x UART bridge
    "0x1a86": "esp32s3",  # WCH CH340/CH9102 UART bridge
}

# Coarse role → the default pio env to fall back on when we can't resolve the
# exact board variant from its hw_model.
_ROLE_ENV = {
    "nrf52": "rak4631",
    "esp32s3": "heltec-v3",
}

_NOSERIAL_PREFIX = "noserial:"


def role_for_vid(vid: str | None) -> str | None:
    if not vid:
        return None
    return _VID_ROLE.get(vid.lower())


def env_for_role(role: str | None) -> str | None:
    if not role:
        return None
    return _ROLE_ENV.get(role)


def env_for_hw_model(hw_model: str | None) -> str | None:
    """Resolve the exact pio env for a hardware model slug (e.g. ``HELTEC_V4``).

    Prefers the *base* env (``heltec-v4``) over decorated variants
    (``heltec-v4-tft``): when several envs declare the same hw_model slug, the
    one whose name is the canonical slugification wins, else the shortest.
    Returns None for an unknown slug.
    """
    if not hw_model:
        return None
    target = hw_model.upper()
    candidates = [
        b["env"]
        for b in boards.list_boards()
        if (b.get("hw_model_slug") or "").upper() == target and b.get("env")
    ]
    if not candidates:
        return None
    canonical = hw_model.lower().replace("_", "-")
    if canonical in candidates:
        return canonical
    return min(candidates, key=len)


def device_key(d: dict) -> tuple[str, bool]:
    """Return ``(key, is_stable)`` for a discovered-device dict.

    A real serial number is a stable key. Without one, we synthesise a
    ``noserial:<hash>`` surrogate from vid/pid/port so the device is still
    addressable within a session — but it is NOT stable across replug.
    """
    serial = d.get("serial_number")
    if serial:
        return str(serial), True
    raw = f"{d.get('vid')}:{d.get('pid')}:{d.get('port')}"
    digest = hashlib.sha1(raw.encode()).hexdigest()[:12]
    return f"{_NOSERIAL_PREFIX}{digest}", False


def has_stable_id(key: str | None) -> bool:
    return bool(key) and not str(key).startswith(_NOSERIAL_PREFIX)
