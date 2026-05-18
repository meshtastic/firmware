"""Mesh: broadcast text from TX arrives at RX.

Uses `meshtastic.SerialInterface` pubsub on RX to detect the decoded text
packet — `pio device monitor` output doesn't include message bodies.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp import admin

from ._receive import ReceiveCollector


@pytest.mark.timeout(180)
def test_broadcast_delivers(
    mesh_pair: dict[str, Any],
) -> None:
    """Runs for every directed role pair. TX sends a unique broadcast text;
    RX must receive the decoded text via the meshtastic pubsub receive topic
    within 120s.
    """
    tx_port = mesh_pair["tx"]["port"]
    rx_port = mesh_pair["rx"]["port"]
    tx_role = mesh_pair["tx_role"]
    rx_role = mesh_pair["rx_role"]

    unique = f"mcp-{tx_role}-to-{rx_role}-{int(time.time())}"

    with ReceiveCollector(rx_port, topic="meshtastic.receive.text") as rx:
        admin.send_text(text=unique, port=tx_port)

        got = rx.wait_for(
            lambda pkt: pkt.get("decoded", {}).get("text") == unique,
            timeout=120,
        )

    assert got is not None, (
        f"broadcast {unique!r} from {tx_role} not received at {rx_role} within 120s. "
        f"RX saw {len(rx.snapshot())} text packet(s): "
        f"{[p.get('decoded', {}).get('text') for p in rx.snapshot()]!r}"
    )
