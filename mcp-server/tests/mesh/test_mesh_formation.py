"""Mesh: two devices baked with the same session profile discover each other.

The fundamental "does my mesh work" test. If both devices share a PSK, LoRa
region, modem preset, and channel slot, they should hear each other's
NodeInfo packets within ~60s of boot and appear in each other's `nodesByNum`
DB.
"""

from __future__ import annotations

from typing import Any

import pytest
from meshtastic_mcp import info
from meshtastic_mcp.connection import connect


@pytest.mark.timeout(180)
def test_mesh_formation_within_60s(baked_mesh: dict[str, Any], wait_until) -> None:
    """Connect to A, poll its node DB until B's node_num appears. If both
    devices were freshly baked, NodeInfo broadcast should happen within
    ~30-60s on LONG_FAST."""
    if "nrf52" not in baked_mesh or "esp32s3" not in baked_mesh:
        pytest.skip("both roles required")

    a_port = baked_mesh["nrf52"]["port"]
    b_node_num = baked_mesh["esp32s3"]["my_node_num"]
    assert b_node_num is not None, "esp32s3 my_node_num not populated"

    def b_visible_from_a() -> bool:
        with connect(port=a_port) as iface:
            nodes = iface.nodesByNum or {}
            return b_node_num in nodes

    wait_until(b_visible_from_a, timeout=120, backoff_start=2.0, backoff_max=10.0)
