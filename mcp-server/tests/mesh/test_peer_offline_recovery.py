"""Isolation test for peer-offline-then-back mid-conversation.

Verifies the mesh stack's behavior when a peer is physically powered
off mid-send via uhubctl, then powered back on.

Flow (parametrized over every directed mesh_pair):
  1. Bilateral PKI warmup (same pattern as test_direct_with_ack).
  2. TX sends a broadcast text "msg-1" — RX confirms receipt via pubsub.
  3. Power OFF RX via uhubctl. The RX device disappears from the OS.
  4. TX sends a directed text "msg-2" with wantAck=True. Firmware retries
     internally for ~30s before giving up. Assertion: the packet object
     was accepted by the TX stack (non-None) — we don't assert an ACK
     since there's no peer to send one.
  5. Power ON RX. Wait for re-enumeration + boot.
  6. Bilateral PKI re-nudge — RX's in-RAM PKI cache was wiped on reboot,
     so the first directed send may err=35 without a fresh NodeInfo ping.
  7. TX sends a directed "msg-3" — RX receives it via pubsub, confirming
     the mesh recovered.

Skips cleanly if uhubctl isn't installed (via the `power_cycle` fixture's
auto-skip). Skips for pair directions where RX isn't power-controllable
(e.g. a USB-IF hub that doesn't support PPPS for its port).
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp.connection import connect
from tests import _power
from tests._port_discovery import resolve_port_by_role

from ._receive import ReceiveCollector, nudge_nodeinfo


@pytest.mark.timeout(360)
def test_peer_offline_then_recovers(
    mesh_pair: dict[str, Any],
    power_cycle,  # noqa: ARG001 — forces uhubctl-availability skip
    hub_devices: dict[str, str],
) -> None:
    tx_port = mesh_pair["tx"]["port"]
    rx_node_num = mesh_pair["rx"]["my_node_num"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]

    unique_pre = f"peer-offline-pre-{tx_role}-to-{rx_role}-{int(time.time())}"
    unique_post = f"peer-offline-post-{tx_role}-to-{rx_role}-{int(time.time())}"

    # Step 1 + 2: warm up + confirm baseline delivery works before the test.
    with ReceiveCollector(
        mesh_pair["rx"]["port"], topic="meshtastic.receive.text"
    ) as rx:
        rx.broadcast_nodeinfo_ping()
        with connect(port=tx_port) as tx_iface:
            nudge_nodeinfo(tx_iface)
            # Wait for bilateral PKI (RX pubkey in TX's nodesByNum).
            deadline = time.monotonic() + 45.0
            last_nudge = time.monotonic()
            while time.monotonic() < deadline:
                rec = (tx_iface.nodesByNum or {}).get(rx_node_num, {})
                if rec.get("user", {}).get("publicKey"):
                    break
                if time.monotonic() - last_nudge > 15.0:
                    rx.broadcast_nodeinfo_ping()
                    nudge_nodeinfo(tx_iface)
                    last_nudge = time.monotonic()
                time.sleep(1.0)
            else:
                pytest.skip(
                    f"bilateral PKI never completed ({tx_role}→{rx_role}); "
                    "can't run the offline test without a warm baseline"
                )

            tx_iface.sendText(unique_pre, destinationId=rx_node_num, wantAck=True)
            got = rx.wait_for(
                lambda pkt: pkt.get("decoded", {}).get("text") == unique_pre,
                timeout=30,
            )
            assert got is not None, (
                f"baseline directed send ({tx_role}→{rx_role}) didn't land — "
                "skipping offline test to avoid false positive"
            )

    # Step 3: power off RX. uhubctl skips the test with a clear message if
    # the RX role isn't on a controllable hub.
    try:
        _power.power_off(rx_role)
    except Exception as exc:  # noqa: BLE001
        pytest.skip(f"can't power-control {rx_role!r}: {exc}")

    try:
        _power.wait_for_absence(rx_role, timeout_s=10.0)
    except TimeoutError:
        _power.power_on(rx_role)  # restore hub state before failing
        resolve_port_by_role(rx_role, timeout_s=30.0)
        pytest.fail(f"{rx_role!r} didn't disappear after power_off")

    # Step 4: send to a peer that isn't there. Firmware will retry
    # internally. We don't wait for an ACK (there won't be one); we just
    # confirm TX's stack accepts the packet without crashing.
    try:
        with connect(port=tx_port) as tx_iface:
            packet = tx_iface.sendText(
                f"while-offline-{rx_role}",
                destinationId=rx_node_num,
                wantAck=True,
            )
            assert packet is not None
            # Give firmware a moment to do a retry or two while RX is down.
            time.sleep(5.0)
    except Exception as exc:  # noqa: BLE001 — TX should survive the peer being gone
        # Restore RX before reraising so the bench state is sane.
        _power.power_on(rx_role)
        resolve_port_by_role(rx_role, timeout_s=30.0)
        raise AssertionError(f"TX crashed when sending to offline peer: {exc}") from exc

    # Step 5: power RX back on + rediscover.
    _power.power_on(rx_role)
    time.sleep(0.5)
    new_rx_port = resolve_port_by_role(rx_role, timeout_s=30.0)
    hub_devices[rx_role] = new_rx_port

    # Step 6 + 7: bilateral re-warmup + directed send that should now work.
    with ReceiveCollector(new_rx_port, topic="meshtastic.receive.text") as rx:
        # RX rebooted → its PKI cache is gone. Re-warm.
        rx.broadcast_nodeinfo_ping()
        with connect(port=tx_port) as tx_iface:
            nudge_nodeinfo(tx_iface)
            time.sleep(3.0)

            got = None
            for _attempt in range(3):
                packet = tx_iface.sendText(
                    unique_post,
                    destinationId=rx_node_num,
                    wantAck=True,
                )
                assert packet is not None
                got = rx.wait_for(
                    lambda pkt: pkt.get("decoded", {}).get("text") == unique_post,
                    timeout=30,
                )
                if got is not None:
                    break
                rx.broadcast_nodeinfo_ping()
                nudge_nodeinfo(tx_iface)
                time.sleep(5.0)

    assert got is not None, (
        f"post-recovery directed send {unique_post!r} ({tx_role}→{rx_role}) "
        "never landed — recovery path may be broken"
    )
