"""Shared helper for mesh receive tests.

`pio device monitor` captures firmware log output, which does NOT include
decoded text message contents or telemetry payloads — those are only
accessible through `meshtastic.SerialInterface`'s pubsub mechanism.

`ReceiveCollector` opens a long-lived SerialInterface on a port, subscribes
to the pubsub topic of interest, and exposes an atomic `wait_for(predicate)`
that mesh tests use to verify end-to-end delivery.

This module also exposes two module-level helpers for forcing a device to
broadcast a fresh NodeInfo — the on-demand path that sidesteps the
firmware's 10-minute NodeInfo rate-limit. Tests doing directed PKI-encrypted
sends need BOTH endpoints to hold current pubkeys for each other:

    nudge_nodeinfo(iface)    # nudge an already-open SerialInterface
    nudge_nodeinfo_port(port) # open briefly, nudge, close

See `ReceiveCollector.broadcast_nodeinfo_ping` for the firmware-side
rationale (PKI staleness → directed sends NAK with Routing.Error=35
PKI_UNKNOWN_PUBKEY or 39 PKI_SEND_FAIL_PUBLIC_KEY).
"""

from __future__ import annotations

import threading
import time
from typing import Any, Callable


def nudge_nodeinfo(iface: Any) -> None:
    """Force the device behind ``iface`` to broadcast a fresh NodeInfo.

    Sends a ``ToRadio.Heartbeat(nonce=1)`` — the firmware's documented
    on-demand NodeInfo trigger (see `src/mesh/api/PacketAPI.cpp:74-79`
    for TCP/UDP and `src/mesh/PhoneAPI.cpp::handleToRadio` for serial,
    both routed to `NodeInfoModule::sendOurNodeInfo(..., shorterTimeout=true)`
    with the 60-s window rather than the 10-min rate-limit).

    Call on BOTH TX and RX ifaces before a directed PKI-encrypted send.
    Nudging only one side leaves the other with a stale pubkey cache and
    makes the directed send NAK with PKI_UNKNOWN_PUBKEY.
    """
    from meshtastic.protobuf import mesh_pb2  # type: ignore[import-untyped]

    tr = mesh_pb2.ToRadio()
    tr.heartbeat.nonce = 1
    iface._sendToRadio(tr)


def nudge_nodeinfo_port(port: str) -> None:
    """Open ``port`` briefly, nudge, close — for when no iface is open yet.

    Uses the meshtastic_mcp port-lock-aware `connect()` context manager
    so we don't race ReceiveCollector or other long-lived handles on
    the same port.
    """
    from meshtastic_mcp.connection import connect

    with connect(port=port) as iface:
        nudge_nodeinfo(iface)


class ReceiveCollector:
    """Listen for meshtastic packets on `port` and let tests wait for a match.

    Must be used as a context manager so the underlying SerialInterface is
    always closed (leaked interfaces hold the CDC port open and break
    subsequent tool calls).

    Usage:
        with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
            # ... send from TX ...
            assert rx.wait_for(
                lambda pkt: pkt.get("decoded", {}).get("text") == unique,
                timeout=60,
            ), f"packet not received; got {rx.snapshot()!r}"
    """

    def __init__(
        self,
        port: str,
        topic: str = "meshtastic.receive",
        capture_logs: bool = False,
    ) -> None:
        self._port = port
        self._topic = topic
        self._capture_logs = capture_logs
        self._packets: list[dict[str, Any]] = []
        self._log_lines: list[str] = []
        self._lock = threading.Lock()
        self._iface = None
        self._handler_ref = None  # keep strong ref so pubsub doesn't GC it
        self._log_handler_ref = None

    def __enter__(self) -> "ReceiveCollector":
        from meshtastic.serial_interface import (
            SerialInterface,  # type: ignore[import-untyped]
        )
        from pubsub import pub  # type: ignore[import-untyped]

        # pubsub uses weak refs by default — we stash a strong ref so the
        # handler doesn't disappear between subscribe and wait_for.
        def handler(packet: dict, interface: Any) -> None:
            with self._lock:
                self._packets.append(packet)

        self._handler_ref = handler
        pub.subscribe(handler, self._topic)

        # Firmware-side logs come through the SAME SerialInterface when
        # `config.security.debug_log_api_enabled = True`. Subscribing here
        # captures them for failure-artifact attachment without needing a
        # separate pio monitor session that would fight our port lock.
        if self._capture_logs:

            def log_handler(line: str, interface: Any) -> None:
                with self._lock:
                    self._log_lines.append(line)

            self._log_handler_ref = log_handler
            pub.subscribe(log_handler, "meshtastic.log.line")

        self._iface = SerialInterface(devPath=self._port, connectNow=True)
        # Let the config bootstrap complete so we don't miss early arrivals.
        time.sleep(1.0)
        return self

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        from pubsub import pub  # type: ignore[import-untyped]

        if self._handler_ref is not None:
            try:
                pub.unsubscribe(self._handler_ref, self._topic)
            except Exception:
                pass
        if self._log_handler_ref is not None:
            try:
                pub.unsubscribe(self._log_handler_ref, "meshtastic.log.line")
            except Exception:
                pass
        if self._iface is not None:
            try:
                self._iface.close()
            except Exception:
                pass

    def snapshot(self) -> list[dict[str, Any]]:
        """Return a thread-safe copy of the list of collected packets."""
        with self._lock:
            return list(self._packets)

    def log_snapshot(self) -> list[str]:
        """Return captured firmware log lines.

        Only populated if `capture_logs=True` AND the device has
        `security.debug_log_api_enabled=True`.
        """
        with self._lock:
            return list(self._log_lines)

    def send_text(
        self,
        text: str,
        destination_id: Any = "^all",
        want_ack: bool = False,
        channel_index: int = 0,
    ) -> Any:
        """Send a text packet through the already-open SerialInterface.

        Use this when a test also has a ReceiveCollector open on the same port
        — `admin.send_text(port=...)` would try to open a second SerialInterface
        and fail the port lock.
        """
        if self._iface is None:
            raise RuntimeError("ReceiveCollector not started; use as context manager")
        return self._iface.sendText(
            text,
            destinationId=destination_id,
            wantAck=want_ack,
            channelIndex=channel_index,
        )

    def broadcast_nodeinfo_ping(self) -> None:
        """Force the firmware on `port` to broadcast a fresh NodeInfo.

        Thin wrapper around the module-level :func:`nudge_nodeinfo` that
        also validates the context-manager invariant. Delegates so tests
        that need to nudge BOTH sides (bilateral PKI warmup) share one
        implementation — the caller just passes each iface in turn.

        Firmware-side details (rate-limit bypass, nonce==1 trigger path,
        shorterTimeout=true window) are documented on the module-level
        helper.
        """
        if self._iface is None:
            raise RuntimeError("ReceiveCollector not started; use as context manager")
        nudge_nodeinfo(self._iface)

    def wait_for(
        self,
        predicate: Callable[[dict[str, Any]], bool],
        timeout: float = 60.0,
        poll_interval: float = 0.5,
    ) -> dict[str, Any] | None:
        """Block until a received packet matches `predicate` or timeout.

        Returns the matching packet (truthy) or None (falsy).
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for pkt in self._packets:
                    try:
                        if predicate(pkt):
                            return pkt
                    except Exception:
                        continue
            time.sleep(poll_interval)
        return None
