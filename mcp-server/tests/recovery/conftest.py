"""Recovery-tier gating + shared helpers.

Session-scoped guard skips the whole tier when uhubctl isn't installed.
Tests under this directory assume uhubctl is callable AND that at least
one hub role is detected on a PPPS-capable port.
"""

from __future__ import annotations

import pytest


@pytest.fixture(scope="session", autouse=True)
def _recovery_tier_guard() -> None:
    """Skip the tier when uhubctl is unavailable OR no device is on a
    PPPS-capable hub. Prints the specific reason so operators know what
    to fix."""
    from tests import _power

    if not _power.is_uhubctl_available():
        pytest.skip(
            "uhubctl not installed; recovery tier needs it. "
            "Install via `brew install uhubctl` or `apt install uhubctl`.",
            allow_module_level=True,
        )

    # Probe: can we even list hubs? (A macOS user without sudo gets a
    # permission error here — we'd rather find out once at tier-start than
    # 6 tests later.)
    from meshtastic_mcp import uhubctl

    try:
        hubs = uhubctl.list_hubs()
    except uhubctl.UhubctlError as exc:
        pytest.skip(
            f"uhubctl list failed: {exc}. Try the udev rules or `sudo` as a fallback.",
            allow_module_level=True,
        )

    if not any(h["ppps"] for h in hubs):
        pytest.skip(
            "no PPPS-capable hubs detected — recovery tier has nothing to exercise.",
            allow_module_level=True,
        )
