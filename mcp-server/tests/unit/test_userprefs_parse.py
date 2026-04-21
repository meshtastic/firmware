"""Unit tests for `userprefs.py`: jsonc parse, type inference, round-trip
write, and the `temporary_overrides` context manager's byte-for-byte restore.

None of these require hardware. They validate the contract that the flash/
testing-profile tools rely on — if these fail, the provisioning tier will
produce confusing mismatches.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from meshtastic_mcp import userprefs


@pytest.fixture
def sample_jsonc(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Write a minimal userPrefs.jsonc into tmp_path and point config at it."""
    content = """{
  "USERPREFS_CONFIG_LORA_REGION": "meshtastic_Config_LoRaConfig_RegionCode_US",
  "USERPREFS_LORACONFIG_CHANNEL_NUM": "88",
  // "USERPREFS_CHANNEL_0_NAME": "McpTest",
  "USERPREFS_CHANNEL_0_PSK": "{ 0x01, 0x02, 0x03 }",
  // "USERPREFS_MQTT_ENABLED": "0",
  "USERPREFS_CONFIG_LORA_IGNORE_MQTT": "true"
}
"""
    # Fake firmware root with a userPrefs.jsonc + platformio.ini (needed for
    # `config.firmware_root()`'s walk-up detection).
    (tmp_path / "platformio.ini").write_text("[platformio]\n", encoding="utf-8")
    jsonc = tmp_path / "userPrefs.jsonc"
    jsonc.write_text(content, encoding="utf-8")
    monkeypatch.setenv("MESHTASTIC_FIRMWARE_ROOT", str(tmp_path))
    return jsonc


def test_read_state_separates_active_and_commented(sample_jsonc: Path) -> None:
    state = userprefs.read_state()
    assert set(state["active"]) == {
        "USERPREFS_CONFIG_LORA_REGION",
        "USERPREFS_LORACONFIG_CHANNEL_NUM",
        "USERPREFS_CHANNEL_0_PSK",
        "USERPREFS_CONFIG_LORA_IGNORE_MQTT",
    }
    assert set(state["commented"]) == {
        "USERPREFS_CHANNEL_0_NAME",
        "USERPREFS_MQTT_ENABLED",
    }


def test_infer_type_matches_platformio_custom_py() -> None:
    # Mirrors the branch order in `bin/platformio-custom.py:222-235`.
    assert userprefs.infer_type("{ 0x01, 0x02 }") == "brace"
    assert userprefs.infer_type("88") == "number"
    assert userprefs.infer_type("-1.5") == "number"
    assert userprefs.infer_type("true") == "bool"
    assert userprefs.infer_type("false") == "bool"
    assert userprefs.infer_type("meshtastic_Config_DeviceConfig_Role_ROUTER") == "enum"
    assert userprefs.infer_type("plain string value") == "string"
    assert userprefs.infer_type(None) == "unknown"


def test_temporary_overrides_restores_byte_for_byte(sample_jsonc: Path) -> None:
    """The context manager MUST leave the file bit-identical on exit, even on
    exception — this is the safety guarantee build/flash tools rely on."""
    original = sample_jsonc.read_bytes()

    with userprefs.temporary_overrides({"USERPREFS_CHANNEL_0_NAME": "OverrideTest"}):
        # During the context, the override is written.
        during = userprefs.read_state()
        assert "USERPREFS_CHANNEL_0_NAME" in during["active"]
        assert during["active"]["USERPREFS_CHANNEL_0_NAME"] == "OverrideTest"

    # After: byte-identical restore.
    assert sample_jsonc.read_bytes() == original


def test_temporary_overrides_restores_after_exception(sample_jsonc: Path) -> None:
    original = sample_jsonc.read_bytes()

    with pytest.raises(RuntimeError, match="simulated"):
        with userprefs.temporary_overrides({"USERPREFS_CHANNEL_0_NAME": "Failing"}):
            raise RuntimeError("simulated mid-build failure")

    assert sample_jsonc.read_bytes() == original


def test_temporary_overrides_none_is_noop(sample_jsonc: Path) -> None:
    original = sample_jsonc.read_bytes()
    with userprefs.temporary_overrides(None) as effective:
        # No file write, and `effective` still reflects the active set.
        assert "USERPREFS_CONFIG_LORA_REGION" in effective
    assert sample_jsonc.read_bytes() == original


def test_temporary_overrides_rejects_non_userprefs_keys(sample_jsonc: Path) -> None:
    with pytest.raises(ValueError, match="USERPREFS_"):
        with userprefs.temporary_overrides({"RANDOM_KEY": "value"}):
            pass


def test_build_manifest_surfaces_all_keys(sample_jsonc: Path) -> None:
    """Manifest should union the jsonc set with firmware-src consumers.

    In the sample tmpdir there's no `src/` so `consumed_by` is empty for all
    entries; that's fine — the manifest still lists every jsonc key.
    """
    manifest = userprefs.build_manifest()
    keys = {e["key"] for e in manifest["entries"]}
    # All 6 keys from sample_jsonc should be present.
    assert "USERPREFS_CONFIG_LORA_REGION" in keys
    assert "USERPREFS_CHANNEL_0_NAME" in keys  # commented but still listed
    assert manifest["active_count"] == 4
    assert manifest["commented_count"] == 2
