"""`boards.py` filter and enumeration correctness.

Runs against the real `pio project config` output of this firmware repo —
validates that filter predicates match expected envs and don't drift if
variants get reorganized.
"""

from __future__ import annotations

import pytest
from meshtastic_mcp import boards


def test_list_boards_returns_many() -> None:
    all_boards = boards.list_boards()
    assert len(all_boards) >= 50, "expected at least 50 PlatformIO envs"


def test_tbeam_is_canonical_esp32() -> None:
    """The default env in platformio.ini is `tbeam`; it must always be present
    and flagged as esp32."""
    rec = boards.get_board("tbeam")
    assert rec["architecture"] == "esp32"
    assert rec["hw_model_slug"] == "TBEAM"
    assert rec["actively_supported"] is True
    assert rec["board"] == "ttgo-tbeam"


def test_filter_by_architecture() -> None:
    esp32s3 = boards.list_boards(architecture="esp32s3")
    assert len(esp32s3) >= 1
    assert all(b["architecture"] == "esp32s3" for b in esp32s3)


def test_filter_by_actively_supported() -> None:
    supported = boards.list_boards(actively_supported_only=True)
    unsupported = [b for b in boards.list_boards() if not b["actively_supported"]]
    assert supported, "at least one board should be actively supported"
    assert all(b["actively_supported"] for b in supported)
    # Quick sanity: the set difference is non-empty in this repo (there are
    # boards marked actively_supported=false).
    assert unsupported, "expected at least one actively_supported=false board"


def test_filter_by_query_substring_matches_display_name() -> None:
    heltec = boards.list_boards(query="heltec")
    assert heltec, "expected at least one Heltec env"
    # Case-insensitive across display_name, env name, or hw_model_slug
    for b in heltec:
        blob = " ".join(
            filter(
                None,
                [
                    b.get("display_name") or "",
                    b["env"],
                    b.get("hw_model_slug") or "",
                ],
            )
        ).lower()
        assert "heltec" in blob


def test_get_board_unknown_env_raises() -> None:
    with pytest.raises(KeyError, match="Unknown env"):
        boards.get_board("definitely-not-a-real-env")


def test_get_board_surfaces_raw_config() -> None:
    rec = boards.get_board("tbeam")
    assert "raw_config" in rec
    assert "custom_meshtastic_architecture" in rec["raw_config"]
    assert rec["raw_config"]["custom_meshtastic_architecture"] == "esp32"
