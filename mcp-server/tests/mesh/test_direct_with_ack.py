"""Mesh: direct text addressed to RX's node_num arrives at RX.

Uses the same pubsub receive pattern as `test_broadcast_delivers`, but sends
with `destinationId=<rx_node_num>` and `wantAck=True`. The assertion is that
the RX firmware accepted and decoded the text; the ACK is handled by the
firmware transparently (and fires automatically when wantAck is set + the
destination is the local node).
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp.connection import connect

from ._receive import ReceiveCollector


@pytest.mark.timeout(240)
def test_direct_with_ack_roundtrip(
    mesh_pair: dict[str, Any],
) -> None:
    """Runs for every directed pair. Addressed send from TX to RX's node_num
    with want_ack=True; RX must receive the decoded text via pubsub.

    Why this proves ACK: setting want_ack on a directed send causes the
    firmware to retry until an ACK is received. If RX's decoded.text fires
    once, both the outbound text AND the inbound ACK happened.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    rx_node_num = mesh_pair["rx"]["my_node_num"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]
    assert rx_node_num is not None, f"{rx_role} my_node_num missing"

    unique = f"mcp-ack-{tx_role}-to-{rx_role}-{int(time.time())}"

    # Why the TX interface stays open across the RX wait:
    #   With wantAck=True, meshtastic-python queues the packet and the firmware
    #   retransmits until it sees an ACK from the destination. Closing the
    #   SerialInterface immediately after sendText() races that retry loop —
    #   empirically the packet never reaches RX.
    #
    # Why we ping RX for a fresh NodeInfo before polling:
    #   Directed packets are PKI-encrypted with the destination's public key.
    #   After a factory_reset or reboot, a peer's entry in the sender's
    #   nodeDB can still contain that peer's OLD public key — a directed
    #   send then fails with Routing.Error=39 (PKI_SEND_FAIL_PUBLIC_KEY) or
    #   decryption fails on the receiver side. NodeInfo broadcasts are the
    #   sole source of fresh pubkeys, and firmware rate-limits them to
    #   every 10 min organically. ToRadio.heartbeat(nonce=1) bypasses that
    #   — it triggers an on-demand NodeInfo broadcast via
    #   `src/mesh/PhoneAPI.cpp::handleToRadio` (serial) and
    #   `src/mesh/api/PacketAPI.cpp::handlePacket` (TCP/UDP), both sharing
    #   the 60s shorterTimeout path in `src/modules/NodeInfoModule.cpp`.
    #   After ping, poll TX's nodesByNum until publicKey propagates, then
    #   send. A small retry loop guards against transient LoRa collisions.
    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        rx.broadcast_nodeinfo_ping()

        with connect(port=tx_port) as tx_iface:
            pk_deadline = time.monotonic() + 45.0
            last_nudge = time.monotonic()
            last_rec: dict[str, Any] = {}
            while time.monotonic() < pk_deadline:
                last_rec = (tx_iface.nodesByNum or {}).get(rx_node_num, {})
                user = last_rec.get("user", {})
                if user.get("publicKey"):
                    break
                # Re-nudge every 15s in case the first NodeInfo was lost to
                # a LoRa collision with concurrent traffic.
                if time.monotonic() - last_nudge > 15.0:
                    rx.broadcast_nodeinfo_ping()
                    last_nudge = time.monotonic()
                time.sleep(1.0)
            else:
                pytest.fail(
                    f"TX ({tx_role}) never saw RX ({rx_role}) public key "
                    f"within 45s; nodesByNum entry={last_rec!r}"
                )

            # Directed send + short retry: at most 2 attempts. Each is
            # sufficient on its own with fresh keys; the retry is purely
            # an airtime-collision safety net.
            got = None
            for attempt in range(2):
                packet = tx_iface.sendText(
                    unique,
                    destinationId=rx_node_num,
                    wantAck=True,
                )
                assert packet is not None, "sendText returned None"
                got = rx.wait_for(
                    lambda pkt: pkt.get("decoded", {}).get("text") == unique,
                    timeout=30,
                )
                if got is not None:
                    break
                time.sleep(5.0)

    assert got is not None, (
        f"directed send {unique!r} from {tx_role} to {rx_role} "
        f"(node_num 0x{rx_node_num:08x}) not received within 120s. "
        f"RX saw {len(rx.snapshot())} text packet(s): "
        f"{[p.get('decoded', {}).get('text') for p in rx.snapshot()]!r}"
    )
    # Additional: confirm the destination matches (not leaked broadcast)
    assert got.get("to") == rx_node_num, (
        f"received packet destination mismatch: to={got.get('to')}, "
        f"expected 0x{rx_node_num:08x}"
    )
