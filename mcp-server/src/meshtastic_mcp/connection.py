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
    """Parse `tcp://host[:port]` → (host, port). Defaults to 4403.

    Validates host shape (non-empty, no path separators) and port range
    (1..65535). Raises `ConnectionError` on malformed input — never lets
    a raw `ValueError` bubble up to a tool surface.
    """
    if not port.startswith(TCP_SCHEME):
        raise ConnectionError(
            f"Invalid TCP endpoint {port!r}: expected '{TCP_SCHEME}host[:port]'."
        )
    rest = port[len(TCP_SCHEME) :]
    if ":" in rest:
        host, port_str = rest.rsplit(":", 1)
        try:
            tcp_port = int(port_str)
        except ValueError as e:
            raise ConnectionError(
                f"Invalid TCP endpoint {port!r}: port {port_str!r} is not an integer."
            ) from e
    else:
        host, tcp_port = rest, DEFAULT_TCP_PORT
    if not host:
        raise ConnectionError(f"Invalid TCP endpoint {port!r}: empty host.")
    if any(c in host for c in ("/", "\\")):
        raise ConnectionError(
            f"Invalid TCP endpoint {port!r}: host {host!r} contains a path "
            "separator. TCP hostnames cannot contain '/' or '\\' — did you "
            "pass a serial port path or a Windows drive path by mistake?"
        )
    if not (1 <= tcp_port <= 65535):
        raise ConnectionError(
            f"Invalid TCP endpoint {port!r}: port {tcp_port} out of range "
            "(must be 1..65535)."
        )
    return host, tcp_port


def normalize_tcp_endpoint(endpoint: str) -> str:
    r"""Normalize `host`, `host:port`, or `tcp://host[:port]` → canonical
    `tcp://host:port` form. One place that owns the lock-key shape.

    Defers all validation to `parse_tcp_port`, so path-like inputs
    (`/dev/cu.foo`, `C:\Windows\…`), empty hosts, non-integer ports,
    and out-of-range ports raise `ConnectionError` here too.
    """
    if endpoint.startswith(TCP_SCHEME):
        canonical = endpoint
    elif ":" in endpoint:
        canonical = f"{TCP_SCHEME}{endpoint}"
    else:
        canonical = f"{TCP_SCHEME}{endpoint}:{DEFAULT_TCP_PORT}"
    host, port = parse_tcp_port(canonical)
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

    `timeout_s` is plumbed through to both `SerialInterface(timeout=...)`
    and `TCPInterface(timeout=...)`. The meshtastic library uses the value
    as the reply-wait deadline for `localNode.waitForConfig()` during
    construction and for any subsequent admin RPC. `int()`-converted at
    the boundary because the upstream API expects whole seconds.
    """
    resolved = resolve_port(port)
    timeout = int(timeout_s)

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
                timeout=timeout,
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
        iface = SerialInterface(
            devPath=resolved,
            connectNow=True,
            noProto=False,
            timeout=timeout,
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
