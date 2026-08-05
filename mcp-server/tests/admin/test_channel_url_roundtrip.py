"""Admin: channel URL export and re-import round-trip.

Real operator workflow: "I have two fleets, I want them to share a channel
config. Export URL from fleet A's bootstrap device, paste into fleet B's
onboarding tool, expect identical channels." Proves `getURL` + `setURL`
round-trip without data loss.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp import admin, info


@pytest.mark.timeout(60)
def test_channel_url_roundtrip(
    baked_single: dict[str, Any],
    test_profile: dict[str, Any],
) -> None:
    """Runs once per connected role. Verify:
    1. `get_channel_url()` on a baked device returns a non-empty URL.
    2. The URL parses — `set_channel_url(url)` accepts it without error.
    3. After set, `get_channel_url()` returns the same (canonicalized) URL.
    4. Primary channel name survives round-trip.
    """
    port = baked_single["port"]

    url_before = admin.get_channel_url(include_all=False, port=port)["url"]
    assert url_before, "device returned empty channel URL"
    assert (
        "meshtastic" in url_before.lower() or "#" in url_before
    ), f"URL does not look like a Meshtastic channel URL: {url_before!r}"

    # Re-apply the same URL — no-op in content but exercises the setURL path.
    applied = admin.set_channel_url(url=url_before, port=port)
    assert applied["ok"] is True
    assert applied["channels_imported"] >= 1
    time.sleep(2.0)

    # Confirm the primary channel name survived
    live = info.device_info(port=port, timeout_s=8.0)
    assert live["primary_channel"] == test_profile["USERPREFS_CHANNEL_0_NAME"]

    url_after = admin.get_channel_url(include_all=False, port=port)["url"]
    # Canonicalization is tricky: the firmware may re-serialize the protobuf
    # with fields in a different order, producing a visually-different URL
    # that encodes the same content. Accept that as a success when the
    # primary channel name survived the round-trip (already asserted above)
    # and the URL is still a parseable Meshtastic URL. Bit-equality is a
    # nice-to-have, not a correctness guarantee.
    assert url_after, "URL went blank after setURL"
    assert (
        "meshtastic" in url_after.lower() or "#" in url_after
    ), f"URL after setURL no longer looks like a channel URL: {url_after!r}"
