"""Pin the `uhubctl` default-output parser against canned real-world samples.

uhubctl's output format has been stable since v2.x but occasionally adds
new hub-descriptor fields (e.g. the `, ppps` marker). The parser uses loose
regexes to tolerate additions; this test keeps us honest.

Samples captured from:
- v2.6.0 on macOS (Homebrew) — two USB2 hubs, one populated with an
  nRF52 and a CP2102, plus chained USB3 hubs.
- v2.5.0 on Linux (hypothetical — reconstructed from the project README).
"""

from __future__ import annotations

import pytest
from meshtastic_mcp.uhubctl import (
    ROLE_VIDS,
    UhubctlError,
    parse_list_output,
)

# Actual `uhubctl` stdout on the developer's macOS bench, Apr 2026.
_SAMPLE_MACOS_V26 = """\
Current status for hub 1-1.3 [2109:2817 VIA Labs, Inc. USB2.0 Hub, USB 2.10, 4 ports, ppps]
  Port 1: 0100 power
  Port 2: 0103 power enable connect [239a:8029 RAKwireless WisCore RAK4631 Board 920456B1E6972262]
  Port 3: 0103 power enable connect [10c4:ea60 Silicon Labs CP2102 USB to UART Bridge Controller 0001]
  Port 4: 0100 power
Current status for hub 1-2.3 [2109:0817 VIA Labs, Inc. USB3.0 Hub, USB 3.10, 4 ports, ppps]
  Port 1: 02a0 power 5gbps Rx.Detect
  Port 2: 02a0 power 5gbps Rx.Detect
  Port 3: 02a0 power 5gbps Rx.Detect
  Port 4: 02a0 power 5gbps Rx.Detect
Current status for hub 1-1 [2109:2817 VIA Labs, Inc. USB2.0 Hub, USB 2.10, 4 ports, ppps]
  Port 1: 0100 power
  Port 2: 0100 power
  Port 3: 0503 power highspeed enable connect [2109:2817 VIA Labs, Inc. USB2.0 Hub, USB 2.10, 4 ports, ppps]
  Port 4: 0100 power
"""


# Minimal Linux-style sample (fewer hubs, shows a non-PPPS hub).
_SAMPLE_LINUX_NONPPPS = """\
Current status for hub 2-1.4 [05e3:0608 GenesysLogic USB2.1 Hub, USB 2.10, 4 ports]
  Port 1: 0507 power highspeed suspend enable connect [239a:0029 Adafruit Feather Bootloader]
  Port 2: 0100 power
  Port 3: 0100 power
  Port 4: 0100 power
"""


class TestParseListOutput:
    def test_parses_macos_sample_hub_count(self) -> None:
        hubs = parse_list_output(_SAMPLE_MACOS_V26)
        assert len(hubs) == 3

    def test_parses_hub_location_and_vid(self) -> None:
        hubs = parse_list_output(_SAMPLE_MACOS_V26)
        via_hub = hubs[0]
        assert via_hub["location"] == "1-1.3"
        assert via_hub["vid"] == 0x2109
        assert via_hub["pid"] == 0x2817
        assert via_hub["ppps"] is True

    def test_parses_port_with_device(self) -> None:
        hubs = parse_list_output(_SAMPLE_MACOS_V26)
        nrf52_hub = hubs[0]
        port2 = next(p for p in nrf52_hub["ports"] if p["port"] == 2)
        assert port2["device_vid"] == 0x239A
        assert port2["device_pid"] == 0x8029
        assert "RAKwireless" in port2["device_desc"]

    def test_empty_port_has_no_device(self) -> None:
        hubs = parse_list_output(_SAMPLE_MACOS_V26)
        nrf52_hub = hubs[0]
        port1 = next(p for p in nrf52_hub["ports"] if p["port"] == 1)
        assert port1["device_vid"] is None
        assert port1["device_pid"] is None
        assert port1["device_desc"] is None

    def test_ports_count(self) -> None:
        hubs = parse_list_output(_SAMPLE_MACOS_V26)
        for hub in hubs:
            assert len(hub["ports"]) == 4  # each sample hub has 4 ports

    def test_non_ppps_hub_flagged(self) -> None:
        hubs = parse_list_output(_SAMPLE_LINUX_NONPPPS)
        assert len(hubs) == 1
        assert hubs[0]["ppps"] is False

    def test_handles_empty_input(self) -> None:
        assert parse_list_output("") == []

    def test_handles_malformed_lines_gracefully(self) -> None:
        # Lines that don't match HUB_RE or PORT_RE are ignored silently.
        garbage = "uhubctl: warning: something weird\n" + _SAMPLE_LINUX_NONPPPS
        hubs = parse_list_output(garbage)
        assert len(hubs) == 1


class TestRoleVids:
    def test_nrf52_mapped(self) -> None:
        assert 0x239A in ROLE_VIDS["nrf52"]

    def test_esp32s3_covers_both_vids(self) -> None:
        # Espressif native USB + CP2102 USB-UART on heltec-v3 boards.
        assert 0x303A in ROLE_VIDS["esp32s3"]
        assert 0x10C4 in ROLE_VIDS["esp32s3"]


class TestResolveTargetErrorPaths:
    def test_unknown_role_raises(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from meshtastic_mcp.uhubctl import resolve_target

        # Clear any env-var pinning that might make this pass accidentally.
        for key in (
            "MESHTASTIC_UHUBCTL_LOCATION_FLUX",
            "MESHTASTIC_UHUBCTL_PORT_FLUX",
        ):
            monkeypatch.delenv(key, raising=False)
        with pytest.raises(UhubctlError, match="unknown role"):
            resolve_target("flux")

    def test_invalid_env_port_raises(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from meshtastic_mcp.uhubctl import resolve_target

        monkeypatch.setenv("MESHTASTIC_UHUBCTL_LOCATION_NRF52", "1-1.3")
        monkeypatch.setenv("MESHTASTIC_UHUBCTL_PORT_NRF52", "not-an-int")
        with pytest.raises(UhubctlError, match="not a valid integer"):
            resolve_target("nrf52")

    def test_env_var_pinning_wins(self, monkeypatch: pytest.MonkeyPatch) -> None:
        from meshtastic_mcp.uhubctl import resolve_target

        # Env-var pinning should NOT require uhubctl to be running / installed.
        monkeypatch.setenv("MESHTASTIC_UHUBCTL_LOCATION_NRF52", "9-9.9")
        monkeypatch.setenv("MESHTASTIC_UHUBCTL_PORT_NRF52", "7")
        assert resolve_target("nrf52") == ("9-9.9", 7)

    def test_normalize_role_strips_alt_suffix(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        from meshtastic_mcp.uhubctl import resolve_target

        # esp32s3_alt collapses to esp32s3 for env-var lookup.
        monkeypatch.setenv("MESHTASTIC_UHUBCTL_LOCATION_ESP32S3", "2-2")
        monkeypatch.setenv("MESHTASTIC_UHUBCTL_PORT_ESP32S3", "3")
        assert resolve_target("esp32s3_alt") == ("2-2", 3)
