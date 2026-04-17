"""Monitor: boot log is clean — no panic markers in the first 60 seconds.

This is the single highest-signal test for catching firmware regressions.
If a commit broke something critical at boot (stack overflow, NULL deref, HAL
misconfig), this test fails within a minute of reboot.
"""

from __future__ import annotations

import os
import re
import time
from typing import Any

import pytest
from meshtastic_mcp import admin

# Substrings that indicate a panic/assert/crash. Case-insensitive.
_PANIC_MARKERS = [
    "guru meditation",
    "corrupt heap",
    "abort()",
    "assertion failed",
    "***",  # ESP-IDF "*** something" panic prefix
    "panic",
    "stack overflow",
    "load prohibited",
    "store prohibited",
    "illegalinstr",
    "watchdog got triggered",
]


@pytest.mark.timeout(180)
def test_boot_log_no_panic(
    baked_mesh: dict[str, Any],
    serial_capture,
    wait_until,
) -> None:
    """Reboot the device, then watch ~60s of boot log for panic markers."""
    target = "esp32s3"
    if target not in baked_mesh:
        pytest.skip(f"role {target!r} not on hub")
    port = baked_mesh[target]["port"]

    env = os.environ.get("MESHTASTIC_MCP_ENV_ESP32S3", "t-beam-1w")

    # Start monitor BEFORE reboot so we catch the reset banner + early boot
    cap = serial_capture(target, env=env)
    time.sleep(1.0)

    # Trigger reboot
    admin.reboot(port=port, confirm=True, seconds=3)
    # Wait through the reboot+boot window
    time.sleep(60.0)

    lines = cap.snapshot(max_lines=4000)
    assert lines, "serial capture returned no log lines — monitor may have failed"
    blob = "\n".join(lines).lower()

    hits = [marker for marker in _PANIC_MARKERS if marker in blob]
    assert (
        not hits
    ), f"panic markers in boot log: {hits!r}\n\n" f"last 60 lines:\n" + "\n".join(
        lines[-60:]
    )
