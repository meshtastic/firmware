"""Mesh: broadcast text from A arrives at B.

Proves end-to-end send → receive path across a 2-device mesh. Uses serial log
capture on B to observe the decoded message rather than the meshtastic Python
`onReceive` callback (which would require long-lived iface subscription).
"""

from __future__ import annotations

import os
import time
from typing import Any

import pytest
from meshtastic_mcp import admin


@pytest.mark.timeout(120)
def test_broadcast_delivers(
    baked_mesh: dict[str, Any],
    serial_capture,
    wait_until,
) -> None:
    """Flow:
    1. Start a serial capture on B before sending.
    2. From A, send a uniquely-tagged text broadcast.
    3. Poll B's serial buffer for the unique tag.
    """
    if "nrf52" not in baked_mesh or "esp32s3" not in baked_mesh:
        pytest.skip("both roles required")

    # Capture on B (esp32s3) — pio device monitor shows decoded mesh packets
    b_env = os.environ.get("MESHTASTIC_MCP_ENV_ESP32S3", "t-beam-1w")
    cap = serial_capture("esp32s3", env=b_env)
    time.sleep(2.0)  # let monitor settle

    unique = f"mcp-test-{int(time.time())}"
    admin.send_text(
        text=unique,
        port=baked_mesh["nrf52"]["port"],
    )

    def unique_in_log() -> bool:
        return any(unique in line for line in cap.snapshot(max_lines=4000))

    wait_until(unique_in_log, timeout=90, backoff_start=2.0, backoff_max=10.0)
