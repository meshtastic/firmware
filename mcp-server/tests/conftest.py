"""Root conftest for the MCP server test harness.

Organizes the fixture graph used by every test tier:

    session_seed ── test_profile ─┐
    hub_devices ──────────────────┴─ baked_mesh (verifies) ── baked_single (parametrized)
    hub_devices ──────────────────── no_region_profile (provisioning negative test)
    (per-test)  ──────────────────── serial_capture, device_state_dump, wait_until

CLI flags (see `pytest_addoption`):
    --force-bake       always reflash at session start, even if state matches
    --assume-baked     trust the operator; skip test_00_bake collection entirely
    --hub-profile=...  path to a YAML file mapping role → {vid, pid_contains}
    --no-teardown-rebake  skip the session-end rebake that provisioning/fleet perform

Coverage hooks:
    - Failure artifacts (serial capture, device_info, get_config) are attached
      to pytest-html reports via `pytest_runtest_makereport`.
    - Tool-surface coverage (which of the 37 MCP tools got exercised) is
      accumulated in `tests/tool_coverage.py` and written to
      `tool_coverage.json` at session end.
"""

from __future__ import annotations

import json
import os
import pathlib
import sys
import time
from typing import Any, Callable

import pytest

# Ensure the MCP server is on `sys.path` without requiring installation in
# development mode for every checkout (we DO install in .venv but this makes
# `pytest tests/` work from a fresh clone too).
_HERE = pathlib.Path(__file__).resolve().parent
_MCP_SRC = _HERE.parent / "src"
if str(_MCP_SRC) not in sys.path:
    sys.path.insert(0, str(_MCP_SRC))

# Default firmware root: the repo this mcp-server/ lives inside.
os.environ.setdefault("MESHTASTIC_FIRMWARE_ROOT", str(_HERE.parent.parent))

from meshtastic_mcp import (
    admin,
)
from meshtastic_mcp import (  # noqa: E402  (import after path setup)
    devices as devices_module,
)
from meshtastic_mcp import (
    flash,
    info,
    serial_session,
    userprefs,
)

from . import tool_coverage  # noqa: E402

# ---------- CLI options ---------------------------------------------------


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("meshtastic", "Meshtastic MCP test options")
    group.addoption(
        "--force-bake",
        action="store_true",
        help="Flash both hub roles at session start, even if devices appear baked.",
    )
    group.addoption(
        "--assume-baked",
        action="store_true",
        help="Skip `test_00_bake.py` and trust devices are already baked.",
    )
    group.addoption(
        "--hub-profile",
        default=None,
        help="YAML file mapping role → {vid, pid_contains} for non-default hardware.",
    )
    group.addoption(
        "--no-teardown-rebake",
        action="store_true",
        help="Skip session-end rebake after provisioning/fleet tests mutate state.",
    )


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    """Deselect `test_00_bake.py` when --assume-baked is passed."""
    if config.getoption("--assume-baked"):
        keep, skip = [], []
        for item in items:
            if "test_00_bake" in item.nodeid:
                skip.append(item)
            else:
                keep.append(item)
        if skip:
            for item in skip:
                item.add_marker(pytest.mark.skip(reason="skipped by --assume-baked"))


# ---------- Session-scoped fixtures ---------------------------------------


@pytest.fixture(scope="session")
def session_seed(request: pytest.FixtureRequest) -> str:
    """Deterministic PSK seed for this pytest session.

    Logged in the HTML report header so two runs can be correlated — and so a
    flaky-looking test can be reproduced exactly by passing the seed back via
    an env var (future extension).
    """
    # Pytest session `starttime` isn't directly exposed on the pytest API we
    # care about, so derive from process start time — unique enough for human
    # purposes and stable across the session.
    seed = os.environ.get("MESHTASTIC_MCP_SEED") or f"pytest-{int(time.time())}"
    return seed


@pytest.fixture(scope="session")
def test_profile(session_seed: str) -> dict[str, Any]:
    """The canonical isolated-mesh test profile for this session."""
    return userprefs.build_testing_profile(
        psk_seed=session_seed,
        channel_name="McpTest",
        channel_num=88,
        region="US",
        modem_preset="LONG_FAST",
    )


@pytest.fixture(scope="session")
def no_region_profile(session_seed: str) -> dict[str, Any]:
    """Variant of `test_profile` with the LoRa region stripped.

    Used only by the negative `unset_region_blocks_tx` test. That test MUST
    re-bake `test_profile` in its own teardown so downstream shared-state
    tests still see a correctly-configured mesh.
    """
    profile = userprefs.build_testing_profile(
        psk_seed=session_seed,
        channel_name="McpTest",
        channel_num=88,
        region="US",  # placeholder; we delete the key below
        modem_preset="LONG_FAST",
    )
    profile.pop("USERPREFS_CONFIG_LORA_REGION", None)
    return profile


@pytest.fixture(scope="session")
def hub_profile(request: pytest.FixtureRequest) -> dict[str, dict[str, Any]]:
    """Role → {vid, pid_contains} map for detecting connected hardware.

    Default covers the common nRF52840 + ESP32-S3 lab hub. Override via
    `--hub-profile=path/to/hub.yaml`. Example YAML:

        nrf52:
          vid: 0x239a
          pid_contains: null
        esp32s3:
          vid: 0x303a
          pid_contains: null
    """
    path = request.config.getoption("--hub-profile")
    if path:
        import yaml

        with open(path, "r", encoding="utf-8") as f:
            return yaml.safe_load(f)
    return {
        "nrf52": {"vid": 0x239A, "pid_contains": None},
        # ESP32-S3 can enumerate under Espressif native USB (0x303a) or via a
        # CP2102 USB-serial chip (0x10c4). Both should match for
        # Meshtastic-compatible boards.
        "esp32s3": {"vid": 0x303A, "pid_contains": None},
        "esp32s3_alt": {"vid": 0x10C4, "pid_contains": None},
    }


def _hex_to_int(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 16) if value.startswith("0x") else int(value)
    return None


@pytest.fixture(scope="session")
def hub_devices(hub_profile: dict[str, dict[str, Any]]) -> dict[str, str]:
    """Map of `role → port` for devices detected on the hub.

    Excludes `*_alt` roles from the returned map (they're additional VID
    matchers for the same logical role). If a role isn't detected, an entry is
    absent from the return value; fixtures that require specific roles should
    check presence and `pytest.skip` with an actionable message.
    """
    # include_unknown=True so non-whitelisted VIDs (e.g. CP2102 at 0x10c4) that
    # are configured as hub roles still match. The hub_profile itself gates
    # which VIDs we consider — no risk of unrelated serial ports sneaking in.
    found = devices_module.list_devices(include_unknown=True)
    # Coalesce alt roles into their base name (esp32s3_alt → esp32s3)
    resolved: dict[str, str] = {}
    for role, spec in hub_profile.items():
        target_vid = spec["vid"]
        pid_contains = spec.get("pid_contains")
        canonical = role.split("_alt", 1)[0]
        if canonical in resolved:
            continue
        for dev in found:
            vid = _hex_to_int(dev.get("vid"))
            pid = _hex_to_int(dev.get("pid"))
            if vid != target_vid:
                continue
            if pid_contains is not None and (pid is None or pid_contains not in pid):
                continue
            resolved[canonical] = dev["port"]
            break
    return resolved


@pytest.fixture(scope="session")
def baked_mesh(
    hub_devices: dict[str, str],
    test_profile: dict[str, Any],
    session_seed: str,
    request: pytest.FixtureRequest,
) -> dict[str, Any]:
    """Verifies that both roles are baked with the session `test_profile`.

    Does NOT reflash. `test_00_bake.py` is responsible for applying the bake;
    this fixture just checks the result by connecting to each device and
    comparing the live config to the expected profile.

    Raises with an actionable error if state is missing or mismatched:
        "device nrf52 at /dev/cu.X not baked with session profile —
         run test_00_bake.py first or pass --force-bake"

    Returns a per-role dict with `{port, iface_fresh: callable, my_node_num}`.
    """
    required = {"nrf52", "esp32s3"}
    missing = required - set(hub_devices)
    if missing:
        pytest.skip(
            f"hub missing required role(s): {sorted(missing)}. "
            f"Attach the hub or override with --hub-profile."
        )

    expected_region = test_profile["USERPREFS_CONFIG_LORA_REGION"]
    expected_preset = test_profile["USERPREFS_LORACONFIG_MODEM_PRESET"]
    expected_slot = test_profile["USERPREFS_LORACONFIG_CHANNEL_NUM"]
    expected_channel_name = test_profile["USERPREFS_CHANNEL_0_NAME"]

    out: dict[str, Any] = {}
    for role in ("nrf52", "esp32s3"):
        port = hub_devices[role]
        try:
            live = info.device_info(port=port, timeout_s=12.0)
        except Exception as exc:
            pytest.fail(
                f"device {role} at {port}: could not query device_info "
                f"({exc!r}). Run test_00_bake.py or pass --force-bake."
            )
        # `device_info` surfaces region/primary_channel but not modem preset
        # or channel_num directly; pull those via a separate get_config call.
        lora_cfg = admin.get_config(section="lora", port=port)["config"]["lora"]
        channel_num = int(lora_cfg.get("channel_num", 0))
        modem_preset = lora_cfg.get("modem_preset")
        region_short = live.get("region")
        primary = live.get("primary_channel")

        mismatches = []
        if region_short and not expected_region.endswith(str(region_short)):
            mismatches.append(f"region={region_short} (expected {expected_region})")
        if modem_preset and not expected_preset.endswith(str(modem_preset)):
            mismatches.append(
                f"modem_preset={modem_preset} (expected {expected_preset})"
            )
        if channel_num != expected_slot:
            mismatches.append(f"channel_num={channel_num} (expected {expected_slot})")
        if primary and primary != expected_channel_name:
            mismatches.append(
                f"primary_channel={primary!r} (expected {expected_channel_name!r})"
            )

        if mismatches:
            pytest.fail(
                f"device {role} at {port} not baked with session profile:\n  "
                + "\n  ".join(mismatches)
                + "\nRun `pytest tests/test_00_bake.py` first or pass --force-bake."
            )

        out[role] = {
            "port": port,
            "my_node_num": live.get("my_node_num"),
            "firmware_version": live.get("firmware_version"),
        }

    return out


@pytest.fixture
def baked_single(
    baked_mesh: dict[str, Any], request: pytest.FixtureRequest
) -> dict[str, Any]:
    """Function-scoped: a single verified baked device.

    Parametrize over `request.param` = role name. Defaults to "esp32s3"
    because it's typically more stable as an admin target (no UF2 transitions).
    """
    role = getattr(request, "param", "esp32s3")
    if role not in baked_mesh:
        pytest.skip(f"role {role!r} not present on the hub")
    return {"role": role, **baked_mesh[role]}


# ---------- Failure-artifact fixtures -------------------------------------


class _SerialCapture:
    """Active-session wrapper that lazily opens + closes a pio monitor."""

    def __init__(self, port: str, env: str | None = None) -> None:
        self._port = port
        self._env = env
        self._session = None
        self._last_cursor: int | None = None

    def start(self) -> None:
        self._session = serial_session.open_session(port=self._port, env=self._env)

    def snapshot(self, max_lines: int = 500) -> list[str]:
        if self._session is None:
            return []
        out = serial_session.read_session(
            self._session, max_lines=max_lines, since_cursor=0
        )
        return out.get("lines", [])

    def stop(self) -> None:
        if self._session is not None:
            try:
                serial_session.close_session(self._session)
            except Exception:
                pass
            self._session = None


@pytest.fixture
def serial_capture(hub_devices: dict[str, str], request: pytest.FixtureRequest) -> Any:
    """Returns a `_SerialCapture` factory.

    Usage:
        cap = serial_capture("esp32s3")
        cap.start()
        ... run test ...
        # on failure, serial buffer is attached via pytest_runtest_makereport
    """
    captures: list[_SerialCapture] = []

    def factory(role: str, env: str | None = None) -> _SerialCapture:
        if role not in hub_devices:
            pytest.skip(f"role {role!r} not present on the hub")
        cap = _SerialCapture(port=hub_devices[role], env=env)
        cap.start()
        captures.append(cap)
        request.node._serial_captures = captures  # type: ignore[attr-defined]
        return cap

    yield factory

    for cap in captures:
        cap.stop()


@pytest.fixture
def wait_until() -> Callable[..., Any]:
    """Exponential-backoff polling helper.

    Usage:
        wait_until(lambda: b.node_num in a.iface.nodesByNum, timeout=60)
    """

    def _impl(
        predicate: Callable[[], Any],
        timeout: float = 60.0,
        backoff_start: float = 0.5,
        backoff_max: float = 5.0,
    ) -> Any:
        deadline = time.monotonic() + timeout
        delay = backoff_start
        last: Any = None
        while time.monotonic() < deadline:
            last = predicate()
            if last:
                return last
            time.sleep(delay)
            delay = min(delay * 1.5, backoff_max)
        raise AssertionError(
            f"predicate did not return truthy within {timeout}s (last={last!r})"
        )

    return _impl


# ---------- pytest hooks: report attachments + coverage -------------------


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item: pytest.Item, call: pytest.CallInfo[Any]) -> Any:
    """On test failure, attach serial capture + device state as report artifacts."""
    outcome = yield
    report = outcome.get_result()

    if report.when != "call" or report.outcome != "failed":
        return

    extras: list[str] = []

    # Attach serial captures (if the test used `serial_capture`)
    caps = getattr(item, "_serial_captures", None)
    if caps:
        for i, cap in enumerate(caps):
            lines = cap.snapshot(max_lines=2000)
            extras.append(
                f"--- serial capture [{cap._port}] ({len(lines)} lines) ---\n"
                + "\n".join(lines[-200:])
            )

    # Dump device state for any role in hub_devices (if fixture available)
    hub_fixture = (
        item.funcargs.get("hub_devices") if hasattr(item, "funcargs") else None
    )
    if hub_fixture:
        for role, port in hub_fixture.items():
            state: dict[str, Any] = {"role": role, "port": port}
            try:
                state["device_info"] = info.device_info(port=port, timeout_s=5.0)
            except Exception as exc:
                state["device_info_error"] = repr(exc)
            try:
                state["config"] = admin.get_config(section="lora", port=port)
            except Exception as exc:
                state["config_error"] = repr(exc)
            extras.append(
                f"--- device state [{role}] ---\n{json.dumps(state, indent=2, default=str)}"
            )

    if extras:
        # Attach to pytest-html via `report.sections`; pytest-html renders these
        report.sections.append(("Meshtastic debug", "\n\n".join(extras)))


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    """Emit `tool_coverage.json` at session end."""
    out_path = pathlib.Path(__file__).parent / "tool_coverage.json"
    tool_coverage.write_report(out_path)


# Activate the tool-coverage tracker at import time so imports in fixtures are
# also counted.
tool_coverage.install()
