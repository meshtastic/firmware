"""Telemetry: device-metrics packets arrive at the peer.

Two-path verification:
  1. Listen on TX's pubsub for inbound telemetry packets originating from
     RX's node_num — if one arrives within the window, telemetry works.
  2. Fall back to checking TX's node DB for a populated `deviceMetrics`
     block on the RX record (which the firmware writes on receipt).

Both paths prove the same invariant; path 1 gives faster failure signal,
path 2 handles the case where the packet arrived before we subscribed.

Warmup note: when this test runs after `test_baked_prefs_survive_factory_reset`,
both devices have empty node-DBs. We kick a broadcast text from RX through
its own ReceiveCollector so TX learns RX exists and starts accepting its
telemetry; without it, a fresh-boot pair can take 10+ min to swap NODEINFO
before the first telemetry arrives.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp.connection import connect

from ..mesh._receive import ReceiveCollector


@pytest.mark.timeout(600)
def test_device_telemetry_broadcast(mesh_pair: dict[str, Any]) -> None:
    """Runs for every directed pair. Waits up to ~8 minutes for TX to see
    RX's device telemetry — either as a live inbound pubsub packet or as
    a populated deviceMetrics on RX's node-DB record.

    Firmware default telemetry interval is 900s; after a fresh boot the
    first device-metrics broadcast happens within ~30-120s. We warm up
    the mesh first with a cross-broadcast so NODEINFO is exchanged, then
    wait up to 7 min for a telemetry packet.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    rx_node_num = mesh_pair["rx"]["my_node_num"]

    # Open both sides' pubsub listeners up front so we capture anything that
    # arrives during the warmup exchange.
    with ReceiveCollector(tx_port, topic="meshtastic.receive.telemetry") as tx_rx:
        with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx_tx:
            # Warmup: send a broadcast from RX through its own collector so
            # TX learns about RX (NODEINFO rides along with TEXT_MESSAGE_APP).
            # Skipping this turns a 5-min wait into a 15-min wait on a fresh
            # factory-reset pair.
            rx_tx.send_text(f"warmup-{int(time.time())}")
            time.sleep(5.0)

            # Path 1: wait for a telemetry packet from RX on TX's pubsub.
            got = tx_rx.wait_for(
                lambda pkt: pkt.get("from") == rx_node_num,
                timeout=420,  # 7 min — well above the 30-120s typical first broadcast
            )
            if got is not None:
                return  # Path 1 confirmed delivery.

    # Path 2: re-query TX's node DB for a populated deviceMetrics on RX.
    # Device may have reported telemetry before we subscribed, or the
    # pubsub delivery might race with our window — re-check nodesByNum.
    with connect(port=tx_port) as iface:
        rec = (iface.nodesByNum or {}).get(rx_node_num, {})
        metrics = rec.get("deviceMetrics") or {}
        has_any = any(
            metrics.get(k) is not None
            for k in ("batteryLevel", "voltage", "channelUtilization", "airUtilTx")
        )
        assert has_any, (
            f"no telemetry from node 0x{rx_node_num:08x} within 7 min; "
            f"deviceMetrics={metrics!r}"
        )
