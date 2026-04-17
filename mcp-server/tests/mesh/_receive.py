"""Shared helper for mesh receive tests.

`pio device monitor` captures firmware log output, which does NOT include
decoded text message contents or telemetry payloads — those are only
accessible through `meshtastic.SerialInterface`'s pubsub mechanism.

`ReceiveCollector` opens a long-lived SerialInterface on a port, subscribes
to the pubsub topic of interest, and exposes an atomic `wait_for(predicate)`
that mesh tests use to verify end-to-end delivery.
"""

from __future__ import annotations

import threading
import time
from typing import Any, Callable


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

        Why this exists: firmware rate-limits NodeInfo broadcasts to every
        10 min (and 12 h for reply suppression). After a reboot, an existing
        cooldown window can leave peers with a stale nodesByNum entry that
        lacks `publicKey`, which makes directed PKI-encrypted sends fail
        with Routing.Error=39 (PKI_SEND_FAIL_PUBLIC_KEY). But a ToRadio
        `Heartbeat` with `nonce == 1` is treated as a special "nodeinfo
        ping" trigger in `src/mesh/api/PacketAPI.cpp:74-79`:

            if (mr->heartbeat.nonce == 1) {
                nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, true, 0, true);
            }

        The trailing `true` puts it on the 60-second shorterTimeout path
        rather than the 10-minute one — so tests can force a fresh NodeInfo
        broadcast (with public key) on demand.
        """
        from meshtastic.protobuf import mesh_pb2  # type: ignore[import-untyped]

        if self._iface is None:
            raise RuntimeError("ReceiveCollector not started; use as context manager")
        tr = mesh_pb2.ToRadio()
        tr.heartbeat.nonce = 1
        self._iface._sendToRadio(tr)

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
