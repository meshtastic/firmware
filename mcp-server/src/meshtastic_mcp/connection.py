"""Context manager for meshtastic interface connections (serial + TCP).

Every info/admin tool goes through `connect(port)` so we have a single place
that:
  - auto-selects the port when one likely_meshtastic device is present,
  - fails fast if a serial_session is already holding the port,
  - guarantees `.close()` is called, even on exception.

Two transports:
  - Serial: USB-attached firmware on `/dev/cu.*` / `/dev/ttyUSB*` / `COM*`.
  - TCP: a `meshtasticd` daemon (e.g. the native macOS / Linux Portduino
    headless build) addressed as `tcp://host[:port]` (default port 4403).
    Surfaced by `devices.list_devices()` when `MESHTASTIC_MCP_TCP_HOST` is
    set, so `resolve_port(None)` auto-selects it like a USB candidate.

Both `SerialInterface` and `TCPInterface` block on construction waiting for
the node database; that's fine for v1 since every tool is a short-lived
request.
"""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator

from . import devices, registry

DEFAULT_TCP_PORT = 4403
TCP_SCHEME = "tcp://"
TCP_HOST_ENV = "MESHTASTIC_MCP_TCP_HOST"


class ConnectionError(RuntimeError):
    pass


def is_tcp_port(port: str | None) -> bool:
    return bool(port) and port.startswith(TCP_SCHEME)


def parse_tcp_port(port: str) -> tuple[str, int]:
    """Parse `tcp://host[:port]` → (host, port). Defaults to 4403."""
    rest = port[len(TCP_SCHEME) :]
    if ":" in rest:
        host, port_str = rest.rsplit(":", 1)
        return host, int(port_str)
    return rest, DEFAULT_TCP_PORT


def normalize_tcp_endpoint(endpoint: str) -> str:
    """Normalize `host`, `host:port`, or `tcp://host[:port]` → canonical
    `tcp://host:port` form. One place that owns the lock-key shape."""
    if endpoint.startswith(TCP_SCHEME):
        host, port = parse_tcp_port(endpoint)
    elif ":" in endpoint and not endpoint.startswith("/"):
        host, port_str = endpoint.rsplit(":", 1)
        port = int(port_str)
    else:
        host, port = endpoint, DEFAULT_TCP_PORT
    return f"{TCP_SCHEME}{host}:{port}"


def reject_if_tcp(port: str | None, tool_name: str) -> None:
    """Raise if `port` is a TCP endpoint — for tools that need real USB
    hardware (flash, bootloader, vendor escape hatches, serial monitor).

    Only checks the explicit arg; auto-selection via env var is the caller's
    responsibility to handle if it matters.
    """
    if is_tcp_port(port):
        raise ConnectionError(
            f"{tool_name} is not applicable to TCP/native nodes ({port}). "
            "This tool requires USB-attached hardware."
        )


def resolve_port(port: str | None) -> str:
    """Pick a port: explicit > sole likely_meshtastic candidate > error.

    A `tcp://` string passes through (after canonicalization). When `port`
    is None and no USB candidates are present, `MESHTASTIC_MCP_TCP_HOST`
    is consulted via `devices.list_devices()`.
    """
    if port:
        if is_tcp_port(port):
            return normalize_tcp_endpoint(port)
        return port
    candidates = [d for d in devices.list_devices() if d["likely_meshtastic"]]
    if not candidates:
        raise ConnectionError(
            "No Meshtastic devices detected. Plug one in, set "
            f"{TCP_HOST_ENV}=<host[:port]> for a meshtasticd daemon, "
            "or pass `port` explicitly. Run `list_devices` with "
            "include_unknown=True to see all serial ports."
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
    """Open a meshtastic interface (serial or TCP) and always close it.

    For serial: raises `ConnectionError` immediately if another serial
    session holds the port (a `pio device monitor` in `serial_sessions/`).
    For TCP: no exclusive-access requirement, so the serial-session check
    is skipped — but the `port_lock` still serializes parallel `connect()`
    calls to the same daemon endpoint.
    """
    resolved = resolve_port(port)

    if is_tcp_port(resolved):
        from meshtastic.tcp_interface import (
            TCPInterface,  # type: ignore[import-untyped]
        )

        host, tcp_port = parse_tcp_port(resolved)
        lock = registry.port_lock(resolved)
        if not lock.acquire(blocking=False):
            raise ConnectionError(
                f"TCP endpoint {resolved} is busy — another device operation "
                "is in flight. Retry shortly."
            )

        iface = None
        try:
            iface = TCPInterface(
                hostname=host,
                portNumber=tcp_port,
                connectNow=True,
                noProto=False,
            )
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
        return

    from meshtastic.serial_interface import (
        SerialInterface,  # type: ignore[import-untyped]
    )

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
