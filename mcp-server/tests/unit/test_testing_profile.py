"""`userprefs.build_testing_profile` / `generate_psk` correctness.

The testing-profile generator is the critical primitive for automated test
labs: it must produce deterministic PSKs for a given seed (so every device
baked in a CI run joins the same mesh) and different PSKs for different seeds
(so concurrent labs don't collide).
"""

from __future__ import annotations

import pytest
from meshtastic_mcp import userprefs


def test_generate_psk_is_32_bytes_formatted() -> None:
    psk = userprefs.generate_psk(seed="deterministic")
    # Format: "{ 0x.., 0x.., ... }" with 32 comma-separated hex bytes.
    assert psk.startswith("{ ") and psk.endswith(" }")
    bytes_part = psk.removeprefix("{ ").removesuffix(" }")
    hex_bytes = [b.strip() for b in bytes_part.split(",")]
    assert len(hex_bytes) == 32
    for b in hex_bytes:
        assert b.startswith("0x")
        int(b, 16)  # raises if not valid hex


def test_generate_psk_deterministic_under_same_seed() -> None:
    a = userprefs.generate_psk(seed="pytest-session-123")
    b = userprefs.generate_psk(seed="pytest-session-123")
    assert a == b, "same seed must produce same PSK"


def test_generate_psk_varies_with_seed() -> None:
    seeds = ["a", "b", "pytest-1", "pytest-2", "prod-fleet-alpha"]
    psks = {userprefs.generate_psk(seed=s) for s in seeds}
    assert len(psks) == len(seeds), "seed → PSK map must be injective"


def test_generate_psk_random_when_seedless() -> None:
    a = userprefs.generate_psk(seed=None)
    b = userprefs.generate_psk(seed=None)
    # Not strictly guaranteed (birthday paradox), but 256-bit randomness makes
    # a collision astronomically unlikely.
    assert a != b


def test_testing_profile_contains_expected_keys() -> None:
    profile = userprefs.build_testing_profile(psk_seed="ci-run-1")

    required = {
        "USERPREFS_CONFIG_LORA_REGION",
        "USERPREFS_LORACONFIG_MODEM_PRESET",
        "USERPREFS_LORACONFIG_CHANNEL_NUM",
        "USERPREFS_CHANNELS_TO_WRITE",
        "USERPREFS_CHANNEL_0_NAME",
        "USERPREFS_CHANNEL_0_PSK",
        "USERPREFS_CHANNEL_0_PRECISION",
        "USERPREFS_CONFIG_LORA_IGNORE_MQTT",
        "USERPREFS_MQTT_ENABLED",
        "USERPREFS_CHANNEL_0_UPLINK_ENABLED",
        "USERPREFS_CHANNEL_0_DOWNLINK_ENABLED",
    }
    assert required <= set(profile.keys())

    # Defaults from the plan
    assert profile["USERPREFS_CONFIG_LORA_REGION"].endswith("_US")
    assert profile["USERPREFS_LORACONFIG_MODEM_PRESET"].endswith("_LONG_FAST")
    assert profile["USERPREFS_LORACONFIG_CHANNEL_NUM"] == 88
    assert profile["USERPREFS_CHANNEL_0_NAME"] == "McpTest"


def test_testing_profile_rejects_unknown_region() -> None:
    with pytest.raises(ValueError, match="Unknown region"):
        userprefs.build_testing_profile(region="ATLANTIS")


def test_testing_profile_rejects_unknown_modem_preset() -> None:
    with pytest.raises(ValueError, match="Unknown modem_preset"):
        userprefs.build_testing_profile(modem_preset="WARP_9")


def test_testing_profile_rejects_oversized_channel_name() -> None:
    with pytest.raises(ValueError, match="11-char max"):
        userprefs.build_testing_profile(channel_name="WayTooLongChannelName")


def test_testing_profile_rejects_oversized_short_name() -> None:
    with pytest.raises(ValueError, match="≤4 chars"):
        userprefs.build_testing_profile(short_name="TOOLONG")


def test_disable_mqtt_false_drops_mqtt_keys() -> None:
    profile = userprefs.build_testing_profile(psk_seed="x", disable_mqtt=False)
    # When disable_mqtt is False, the MQTT-gating keys should NOT be in the
    # profile (device uses firmware defaults, whatever those are).
    assert "USERPREFS_MQTT_ENABLED" not in profile
    assert "USERPREFS_CHANNEL_0_UPLINK_ENABLED" not in profile


def test_disable_position_adds_gps_disabled() -> None:
    profile = userprefs.build_testing_profile(psk_seed="x", disable_position=True)
    assert profile["USERPREFS_CONFIG_GPS_MODE"].endswith("_DISABLED")
    assert profile["USERPREFS_CONFIG_SMART_POSITION_ENABLED"] is False


def test_owner_names_included_when_provided() -> None:
    profile = userprefs.build_testing_profile(
        psk_seed="x", long_name="Lab Bench 1", short_name="LB1"
    )
    assert profile["USERPREFS_CONFIG_OWNER_LONG_NAME"] == "Lab Bench 1"
    assert profile["USERPREFS_CONFIG_OWNER_SHORT_NAME"] == "LB1"


def test_psk_seed_isolation_across_ci_runs() -> None:
    """The core claim: two test labs running concurrently with different
    session seeds produce different PSKs — their meshes cannot decode each
    other's traffic."""
    lab_a = userprefs.build_testing_profile(psk_seed="lab-A-nightly")
    lab_b = userprefs.build_testing_profile(psk_seed="lab-B-nightly")
    assert lab_a["USERPREFS_CHANNEL_0_PSK"] != lab_b["USERPREFS_CHANNEL_0_PSK"]
