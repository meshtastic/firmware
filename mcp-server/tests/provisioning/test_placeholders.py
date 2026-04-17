"""Provisioning placeholders: v2 roadmap entries, skipped in v2.

Each test documents intent so `pytest --collect-only` surfaces the complete
planned suite. Implementations land in v2.1+ as hardware coverage expands.
"""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(reason="placeholder — implement in v2.1")


def test_firmware_edition_surfaces() -> None:
    """Bake `USERPREFS_FIRMWARE_EDITION=meshtastic_FirmwareEdition_BURNING_MAN`;
    verify `iface.myInfo.firmware_edition == BURNING_MAN`. Relevant for event
    firmware drops."""
    pass


def test_oem_text_in_boot_log() -> None:
    """Bake `USERPREFS_OEM_TEXT="Lab Bench"`; open serial at boot; assert the
    OEM string appears in the splash log lines."""
    pass


def test_tz_applied() -> None:
    """Bake `USERPREFS_TZ_STRING="PST8PDT,M3.2.0,M11.1.0"`; verify
    `localConfig.device.tzdef` matches and clock-display lines reflect the TZ."""
    pass


def test_full_factory_reset_regenerates_identity() -> None:
    """Before: capture `my_node_num`. Call `factory_reset(full=True)`. After
    reboot: `my_node_num` must differ (new identity), AND baked USERPREFS
    defaults (region/channel) still come back."""
    pass
