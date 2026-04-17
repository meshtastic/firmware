"""Mesh: two devices baked with the same session profile discover each other.

The fundamental "does my mesh work" test. If both devices share a PSK, LoRa
region, modem preset, and channel slot, they should hear each other's
NodeInfo packets within ~60s of boot and appear in each other's `nodesByNum`
DB.
"""

from __future__ import annotations

from typing import Any

import pytest
from meshtastic_mcp.connection import connect


@pytest.mark.timeout(180)
def test_mesh_formation_within_60s(mesh_pair: dict[str, Any], wait_until) -> None:
    """Runs for every directed role pair — so we prove `A sees B in its node
    DB` AND `B sees A in its node DB` independently. A one-sided pass can
    mask a real problem (e.g. device A's RX works but its TX is dead).
    """
    observer_port = mesh_pair["tx"]["port"]
    target_node_num = mesh_pair["rx"]["my_node_num"]
    assert (
        target_node_num is not None
    ), f"{mesh_pair['rx']['role']} my_node_num not populated"

    def target_visible_from_observer() -> bool:
        with connect(port=observer_port) as iface:
            nodes = iface.nodesByNum or {}
            return target_node_num in nodes

    wait_until(
        target_visible_from_observer,
        timeout=120,
        backoff_start=2.0,
        backoff_max=10.0,
    )
