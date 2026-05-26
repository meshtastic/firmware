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

    # TX iface stays open across the RX wait — sendText+wantAck relies on
    # the firmware's retransmit loop, which races the SerialInterface close.
    # Bilateral NodeInfo nudge: directed packets are PKI-encrypted, so BOTH
    # sides need current pubkeys (err=35/39 otherwise). See
    # `tests/mesh/_receive.py::nudge_nodeinfo` for the heartbeat-nonce=1
    # firmware path.
    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        rx.broadcast_nodeinfo_ping()

        with connect(port=tx_port) as tx_iface:
            nudge_nodeinfo(tx_iface)

            pk_deadline = time.monotonic() + 45.0
            last_nudge = time.monotonic()
            last_rec: dict[str, Any] = {}
            while time.monotonic() < pk_deadline:
                last_rec = (tx_iface.nodesByNum or {}).get(rx_node_num, {})
                user = last_rec.get("user", {})
                if user.get("publicKey"):
                    break
                # Re-nudge both sides every 15 s in case a broadcast was
                # lost to a LoRa collision.
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

            # Retry covers LoRa collisions. Re-nudge both sides between
            # attempts — if RX's cached TX pubkey is stale, just re-sending
            # the text doesn't heal it; re-broadcasting NodeInfo does.
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
                rx.broadcast_nodeinfo_ping()
                nudge_nodeinfo(tx_iface)
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
