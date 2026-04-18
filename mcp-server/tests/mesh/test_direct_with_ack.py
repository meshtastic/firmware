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

from ._receive import ReceiveCollector, nudge_nodeinfo


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
    # Why we ping BOTH RX and TX for a fresh NodeInfo before polling:
    #   Directed packets are PKI-encrypted with the destination's public key.
    #   The ENCRYPT path needs TX to hold RX's current pubkey; the DECRYPT
    #   path needs RX to hold TX's current pubkey. After a factory_reset or
    #   reboot, either side's nodeDB entry for the other can still carry
    #   a stale pubkey — directed sends then NAK with Routing.Error=35
    #   (PKI_UNKNOWN_PUBKEY, receiver can't decrypt) or 39
    #   (PKI_SEND_FAIL_PUBLIC_KEY, sender has no pubkey at all). NodeInfo
    #   broadcasts are the sole source of fresh pubkeys and the firmware
    #   rate-limits them to every 10 min. ToRadio.heartbeat(nonce=1)
    #   bypasses that via the 60-s shorterTimeout path
    #   (`src/mesh/PhoneAPI.cpp::handleToRadio` for serial,
    #   `src/mesh/api/PacketAPI.cpp::handlePacket` for TCP/UDP, both
    #   calling `NodeInfoModule::sendOurNodeInfo(..., true)`).
    #
    #   Earlier revisions of this test only nudged RX — which covers the
    #   common case of a recently-baked RX whose TX doesn't know its new
    #   key yet. But when the OPPOSITE side is the one with stale state
    #   (RX holds an old TX pubkey), the test would silently fail with
    #   err=35 in the firmware log. Bilateral nudge eliminates that blind
    #   spot. Poll TX's nodesByNum for RX's publicKey as a proxy for "the
    #   exchange has propagated"; a matching symmetry on RX's side is
    #   implied by the firmware's NodeInfo-on-receipt update path.
    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        rx.broadcast_nodeinfo_ping()

        with connect(port=tx_port) as tx_iface:
            # Bilateral warmup: nudge TX to broadcast too, so RX's nodeDB
            # also gets refreshed with TX's current pubkey.
            nudge_nodeinfo(tx_iface)

            pk_deadline = time.monotonic() + 45.0
            last_nudge = time.monotonic()
            last_rec: dict[str, Any] = {}
            while time.monotonic() < pk_deadline:
                last_rec = (tx_iface.nodesByNum or {}).get(rx_node_num, {})
                user = last_rec.get("user", {})
                if user.get("publicKey"):
                    break
                # Re-nudge every 15s in case the first NodeInfo broadcast
                # was lost to a LoRa collision with concurrent traffic. Both
                # sides re-broadcast for the same reason they were nudged
                # initially — stale pubkeys can live on either side.
                if time.monotonic() - last_nudge > 15.0:
                    rx.broadcast_nodeinfo_ping()
                    nudge_nodeinfo(tx_iface)
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
            for _attempt in range(2):
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
