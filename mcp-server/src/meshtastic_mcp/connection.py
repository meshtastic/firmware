"""Context manager for meshtastic.SerialInterface connections.

Every info/admin tool goes through `connect(port)` so we have a single place
that:
  - auto-selects the port when one likely_meshtastic device is present,
  - fails fast if a serial_session is already holding the port,
  - guarantees `.close()` is called, even on exception.

The `SerialInterface` blocks on construction waiting for the node database;
that's fine for v1 since every tool is a short-lived request.
"""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator

from . import devices, registry


class ConnectionError(RuntimeError):
    pass


def resolve_port(port: str | None) -> str:
    """Pick a port: explicit > sole likely_meshtastic candidate > error."""
    if port:
        return port
    candidates = [d for d in devices.list_devices() if d["likely_meshtastic"]]
    if not candidates:
        raise ConnectionError(
            "No Meshtastic devices detected. Plug one in or pass `port` explicitly. "
            "Run `list_devices` with include_unknown=True to see all serial ports."
        )
    if len(candidates) > 1:
        ports = ", ".join(c["port"] for c in candidates)
        raise ConnectionError(
            f"Multiple Meshtastic devices detected ({ports}). "
            "Specify `port` explicitly."
        )
    return candidates[0]["port"]


@contextmanager
def connect(port: str | None = None, timeout_s: float = 8.0) -> Iterator:
    """Open a `meshtastic.SerialInterface` and always close it.

    Raises `ConnectionError` immediately if another serial session holds the
    port (a `pio device monitor` in `serial_sessions/`, for instance).
    """
    from meshtastic.serial_interface import (
        SerialInterface,  # type: ignore[import-untyped]
    )

    resolved = resolve_port(port)

    active = registry.active_session_for_port(resolved)
    if active is not None:
        raise ConnectionError(
            f"Port {resolved} is held by serial session {active.id}. "
            "Call `serial_close` first."
        )

    lock = registry.port_lock(resolved)
    if not lock.acquire(blocking=False):
        raise ConnectionError(
            f"Port {resolved} is busy — another device operation is in flight. "
            "Retry shortly."
        )

    iface = None
    try:
        iface = SerialInterface(devPath=resolved, connectNow=True, noProto=False)
        yield iface
    finally:
        if iface is not None:
            try:
                iface.close()
            except Exception:
                pass
        try:
            lock.release()
        except RuntimeError:
            pass
