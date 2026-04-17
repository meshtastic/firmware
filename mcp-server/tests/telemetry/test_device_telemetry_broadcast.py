"""Telemetry: device metrics (battery, voltage, channel util) arrive at the peer.

After ~2× the telemetry interval, B's entry in A's node DB should carry a
populated `deviceMetrics` block. This is the happy-path "my fleet is
reporting health data" operator test.
"""

from __future__ import annotations

from typing import Any

import pytest
from meshtastic_mcp.connection import connect


@pytest.mark.timeout(360)
def test_device_telemetry_broadcast(baked_mesh: dict[str, Any], wait_until) -> None:
    """Wait up to 5 minutes for B's device telemetry to land in A's DB.

    Firmware default telemetry interval is 900s; on a fresh mesh the first
    device-metrics broadcast happens within ~30-120s of boot because devices
    broadcast once on startup. We only require that some telemetry is present,
    not that we see multiple cycles.
    """
    if "nrf52" not in baked_mesh or "esp32s3" not in baked_mesh:
        pytest.skip("both roles required")

    a_port = baked_mesh["nrf52"]["port"]
    b_node_num = baked_mesh["esp32s3"]["my_node_num"]

    def b_has_telemetry() -> bool:
        with connect(port=a_port) as iface:
            rec = (iface.nodesByNum or {}).get(b_node_num, {})
            metrics = rec.get("deviceMetrics") or {}
            # Any one of these being non-None is sufficient evidence that
            # telemetry arrived.
            return any(
                metrics.get(k) is not None
                for k in ("batteryLevel", "voltage", "channelUtilization", "airUtilTx")
            )

    wait_until(b_has_telemetry, timeout=300, backoff_start=5.0, backoff_max=15.0)
