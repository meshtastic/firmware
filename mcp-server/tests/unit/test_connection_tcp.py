"""TCP transport plumbing in connection.py + devices.py.

Pure-Python tests — no real device or daemon required. Mocks `TCPInterface`
when exercising `connect()`.
"""

from __future__ import annotations

from unittest.mock import patch

import pytest
from meshtastic_mcp import connection, devices

# ---------- helpers --------------------------------------------------------


class TestIsTcpPort:
    def test_tcp_scheme(self) -> None:
        assert connection.is_tcp_port("tcp://localhost") is True
        assert connection.is_tcp_port("tcp://localhost:4403") is True
        assert connection.is_tcp_port("tcp://192.168.1.50:9999") is True

    def test_serial_paths(self) -> None:
        assert connection.is_tcp_port("/dev/cu.usbmodem1234") is False
        assert connection.is_tcp_port("/dev/ttyUSB0") is False
        assert connection.is_tcp_port("COM3") is False

    def test_empty_or_none(self) -> None:
        assert connection.is_tcp_port(None) is False
        assert connection.is_tcp_port("") is False


class TestParseTcpPort:
    def test_default_port(self) -> None:
        assert connection.parse_tcp_port("tcp://localhost") == ("localhost", 4403)

    def test_explicit_port(self) -> None:
        assert connection.parse_tcp_port("tcp://localhost:9999") == (
            "localhost",
            9999,
        )

    def test_ip_with_port(self) -> None:
        assert connection.parse_tcp_port("tcp://192.168.1.50:4403") == (
            "192.168.1.50",
            4403,
        )


class TestNormalizeTcpEndpoint:
    def test_bare_host(self) -> None:
        assert connection.normalize_tcp_endpoint("localhost") == "tcp://localhost:4403"

    def test_host_port(self) -> None:
        assert (
            connection.normalize_tcp_endpoint("localhost:5000")
            == "tcp://localhost:5000"
        )

    def test_full_url(self) -> None:
        assert (
            connection.normalize_tcp_endpoint("tcp://1.2.3.4") == "tcp://1.2.3.4:4403"
        )
        assert (
            connection.normalize_tcp_endpoint("tcp://1.2.3.4:9999")
            == "tcp://1.2.3.4:9999"
        )

    def test_idempotent(self) -> None:
        once = connection.normalize_tcp_endpoint("localhost:4403")
        twice = connection.normalize_tcp_endpoint(once)
        assert once == twice == "tcp://localhost:4403"

    def test_path_like_endpoint_rejected(self) -> None:
        # Serial port paths and Windows drive paths are common config typos
        # (someone passes a serial path to MESHTASTIC_MCP_TCP_HOST). Reject
        # rather than producing a nonsense `tcp:///dev/cu.foo:4403` URL.
        with pytest.raises(connection.ConnectionError, match="path separator"):
            connection.normalize_tcp_endpoint("/dev/cu.foo")
        with pytest.raises(connection.ConnectionError):
            connection.normalize_tcp_endpoint("tcp:///dev/cu.foo:4403")
        with pytest.raises(connection.ConnectionError):
            connection.normalize_tcp_endpoint(r"C:\Windows\System32")

    def test_non_integer_port_rejected(self) -> None:
        with pytest.raises(connection.ConnectionError, match="not an integer"):
            connection.normalize_tcp_endpoint("tcp://host:notaport")
        with pytest.raises(connection.ConnectionError, match="not an integer"):
            connection.normalize_tcp_endpoint("host:notaport")

    def test_empty_host_rejected(self) -> None:
        with pytest.raises(connection.ConnectionError, match="empty host"):
            connection.normalize_tcp_endpoint("tcp://:4403")

    def test_port_out_of_range_rejected(self) -> None:
        with pytest.raises(connection.ConnectionError, match="out of range"):
            connection.normalize_tcp_endpoint("tcp://host:0")
        with pytest.raises(connection.ConnectionError, match="out of range"):
            connection.normalize_tcp_endpoint("tcp://host:65536")
        with pytest.raises(connection.ConnectionError, match="out of range"):
            connection.normalize_tcp_endpoint("host:99999")


class TestParseTcpPortValidation:
    def test_missing_scheme_rejected(self) -> None:
        # parse_tcp_port is a low-level helper that requires the scheme.
        # Misuse should fail loudly rather than silently mis-parsing.
        with pytest.raises(connection.ConnectionError, match="expected"):
            connection.parse_tcp_port("localhost:4403")

    def test_negative_port_rejected(self) -> None:
        with pytest.raises(connection.ConnectionError, match="out of range"):
            connection.parse_tcp_port("tcp://host:-1")


# ---------- reject_if_tcp --------------------------------------------------


class TestRejectIfTcp:
    def test_rejects_tcp(self) -> None:
        with pytest.raises(connection.ConnectionError, match="not applicable"):
            connection.reject_if_tcp("tcp://localhost", "esptool_chip_info")

    def test_passes_through_serial(self) -> None:
        connection.reject_if_tcp("/dev/cu.usbmodem1", "esptool_chip_info")  # no raise

    def test_passes_through_none(self) -> None:
        # None means "auto-detect"; not the explicit-arg case we guard.
        connection.reject_if_tcp(None, "esptool_chip_info")  # no raise


# ---------- resolve_port ---------------------------------------------------


class TestResolvePort:
    def test_explicit_serial_passthrough(self) -> None:
        assert connection.resolve_port("/dev/cu.usbmodem999") == "/dev/cu.usbmodem999"

    def test_explicit_tcp_normalized(self) -> None:
        assert connection.resolve_port("tcp://localhost") == "tcp://localhost:4403"

    def test_no_port_no_devices_errors(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)
        with patch.object(devices, "list_devices", return_value=[]):
            with pytest.raises(
                connection.ConnectionError, match="No Meshtastic devices"
            ):
                connection.resolve_port(None)

    def test_no_port_one_candidate_selected(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)
        fake = [{"port": "/dev/cu.usbmodem1", "likely_meshtastic": True}]
        with patch.object(devices, "list_devices", return_value=fake):
            assert connection.resolve_port(None) == "/dev/cu.usbmodem1"

    def test_no_port_multiple_candidates_errors(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)
        fake = [
            {"port": "/dev/cu.usbmodem1", "likely_meshtastic": True},
            {"port": "/dev/cu.usbmodem2", "likely_meshtastic": True},
        ]
        with patch.object(devices, "list_devices", return_value=fake):
            with pytest.raises(connection.ConnectionError, match="Multiple"):
                connection.resolve_port(None)

    def test_env_var_surfaces_tcp_via_devices(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "localhost")
        # Don't patch list_devices — let the real env-var path run, but stub
        # the USB enumeration to keep the test hermetic.
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            assert connection.resolve_port(None) == "tcp://localhost:4403"


# ---------- devices.list_devices TCP entry --------------------------------


class TestDevicesTcpEntry:
    def test_no_env_var_no_tcp_entry(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            ds = devices.list_devices()
        assert all(not d["port"].startswith("tcp://") for d in ds)

    def test_env_var_adds_tcp_entry(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "myhost:9999")
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            ds = devices.list_devices()
        tcp = [d for d in ds if d["port"].startswith("tcp://")]
        assert len(tcp) == 1
        assert tcp[0]["port"] == "tcp://myhost:9999"
        assert tcp[0]["likely_meshtastic"] is True
        assert tcp[0]["description"] == "meshtasticd (TCP)"

    def test_tcp_entry_first_in_results(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "localhost")
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            ds = devices.list_devices()
        assert ds, "expected at least the TCP entry"
        assert ds[0]["port"].startswith("tcp://")

    def test_invalid_env_var_does_not_break_list_devices(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        # `list_devices` is the diagnostic tool reached for when an env var
        # isn't working — it must not throw on misconfiguration.
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "host:notaport")
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            ds = devices.list_devices(include_unknown=True)
        tcp = [d for d in ds if "TCP" in (d["description"] or "")]
        assert len(tcp) == 1
        assert tcp[0]["likely_meshtastic"] is False
        assert "invalid MESHTASTIC_MCP_TCP_HOST" in tcp[0]["description"]
        assert "not an integer" in tcp[0]["description"]

    def test_invalid_env_var_excluded_from_resolve_port_autodetect(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        # `likely_meshtastic=False` keeps the bad TCP entry out of the
        # auto-select path — `resolve_port(None)` should still report
        # "no Meshtastic devices" rather than picking a broken endpoint.
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "host:notaport")
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            with pytest.raises(connection.ConnectionError, match="No Meshtastic"):
                connection.resolve_port(None)

    def test_invalid_env_var_does_not_double_tcp_scheme(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        # If a user mistakenly sets `MESHTASTIC_MCP_TCP_HOST=tcp://host:bad`,
        # the diagnostic entry must surface the raw value as-is rather than
        # producing `tcp://tcp://host:bad`.
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "tcp://host:notaport")
        with patch("meshtastic_mcp.devices.list_ports.comports", return_value=[]):
            ds = devices.list_devices(include_unknown=True)
        tcp = [d for d in ds if "TCP" in (d["description"] or "")]
        assert len(tcp) == 1
        assert tcp[0]["port"] == "tcp://host:notaport"
        assert "tcp://tcp://" not in tcp[0]["port"]

    def test_invalid_env_var_does_not_pre_empt_real_usb_devices(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        # Sort ordering: a misconfigured TCP env var must NOT take position 0
        # ahead of real USB candidates. Position 0 is reserved for the highest
        # rank (likely_meshtastic=True), with TCP-before-USB as a tiebreaker
        # within rank.
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "host:notaport")

        # Stub a USB Meshtastic candidate (Espressif VID, port present in
        # findPorts).
        class FakeInfo:
            def __init__(self, device: str, vid: int, pid: int) -> None:
                self.device = device
                self.vid = vid
                self.pid = pid
                self.description = "Heltec V3"
                self.manufacturer = "Espressif"
                self.product = "USB JTAG/serial"
                self.serial_number = "abc"

        fake_port = FakeInfo("/dev/cu.usbmodem4201", 0x303A, 0x1001)
        with patch(
            "meshtastic_mcp.devices.list_ports.comports", return_value=[fake_port]
        ), patch(
            "meshtastic.util.findPorts",
            return_value=["/dev/cu.usbmodem4201"],
        ):
            ds = devices.list_devices(include_unknown=True)

        assert ds, "expected at least the USB + TCP entries"
        # Real USB candidate must be at position 0 — it's likely_meshtastic.
        assert ds[0]["port"] == "/dev/cu.usbmodem4201"
        assert ds[0]["likely_meshtastic"] is True
        # The malformed TCP entry exists but lands among the unlikely entries.
        tcp = [d for d in ds if "TCP" in (d["description"] or "")]
        assert len(tcp) == 1
        assert tcp[0]["likely_meshtastic"] is False
        assert ds.index(tcp[0]) > 0

    def test_likely_tcp_entry_wins_tiebreak_over_usb(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        # Conversely, a *valid* TCP env var should sort ahead of USB
        # candidates of equal likely_meshtastic rank — explicit env-var
        # configuration is a precedence signal.
        monkeypatch.setenv("MESHTASTIC_MCP_TCP_HOST", "localhost:4403")

        class FakeInfo:
            def __init__(self, device: str, vid: int, pid: int) -> None:
                self.device = device
                self.vid = vid
                self.pid = pid
                self.description = "Heltec V3"
                self.manufacturer = "Espressif"
                self.product = "USB JTAG/serial"
                self.serial_number = "abc"

        fake_port = FakeInfo("/dev/cu.usbmodem4201", 0x303A, 0x1001)
        with patch(
            "meshtastic_mcp.devices.list_ports.comports", return_value=[fake_port]
        ), patch(
            "meshtastic.util.findPorts",
            return_value=["/dev/cu.usbmodem4201"],
        ):
            ds = devices.list_devices()

        assert ds[0]["port"] == "tcp://localhost:4403"
        assert ds[0]["likely_meshtastic"] is True


# ---------- connect() routing ---------------------------------------------


class TestConnectRoutesTcp:
    def test_connect_uses_tcp_interface_for_tcp_port(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """Verify the TCP branch instantiates `TCPInterface(hostname, portNumber)`
        and never touches `SerialInterface`."""
        # Make sure the env var doesn't leak in and confuse resolve_port.
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)

        with patch("meshtastic.tcp_interface.TCPInterface") as mock_tcp, patch(
            "meshtastic.serial_interface.SerialInterface"
        ) as mock_serial:
            mock_tcp.return_value.close.return_value = None
            with connection.connect(port="tcp://example.com:1234", timeout_s=12.0):
                pass

        mock_tcp.assert_called_once_with(
            hostname="example.com",
            portNumber=1234,
            connectNow=True,
            noProto=False,
            timeout=12,
        )
        mock_serial.assert_not_called()

    def test_connect_plumbs_timeout_to_serial_interface(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """Verify the serial branch also propagates `timeout_s` so callers
        passing a custom timeout to `device_info` / `list_nodes` / etc. don't
        silently get the library default."""
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)

        with patch("meshtastic.serial_interface.SerialInterface") as mock_serial, patch(
            "meshtastic.tcp_interface.TCPInterface"
        ) as mock_tcp:
            mock_serial.return_value.close.return_value = None
            with connection.connect(port="/dev/cu.fake", timeout_s=20.0):
                pass

        mock_serial.assert_called_once_with(
            devPath="/dev/cu.fake",
            connectNow=True,
            noProto=False,
            timeout=20,
        )
        mock_tcp.assert_not_called()

    def test_connect_releases_lock_on_tcp_failure(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        monkeypatch.delenv("MESHTASTIC_MCP_TCP_HOST", raising=False)
        with patch("meshtastic.tcp_interface.TCPInterface") as mock_tcp:
            mock_tcp.side_effect = RuntimeError("boom")
            with pytest.raises(RuntimeError, match="boom"):
                with connection.connect(port="tcp://locktest:4403"):
                    pass

        # Lock should be released — a second connect attempt must not fail
        # with "busy".
        with patch("meshtastic.tcp_interface.TCPInterface") as mock_tcp:
            mock_tcp.return_value.close.return_value = None
            with connection.connect(port="tcp://locktest:4403"):
                pass
