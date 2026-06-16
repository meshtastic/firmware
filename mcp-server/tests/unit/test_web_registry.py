"""Unit tests for the web stack's SQLite registry + identity reconciliation.

No hardware, no event loop fixtures — each test drives the async helpers via
``asyncio.run`` against a tmpdir-backed Database.
"""

from __future__ import annotations

import asyncio

import pytest
from meshtastic_mcp.web.db import repo_cameras as rc
from meshtastic_mcp.web.db import repo_devices as rd
from meshtastic_mcp.web.db import repo_flash as rf
from meshtastic_mcp.web.db import repo_runs as rr
from meshtastic_mcp.web.db.database import Database
from meshtastic_mcp.web.services import control, identity


def _fresh_db(tmp_path) -> Database:
    return Database(path=tmp_path / "registry.db")


def test_port_follow_keeps_one_device_and_persists_friendly_name(tmp_path):
    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            r1 = await rd.upsert_from_discovery(
                db,
                serial_number="SER1",
                current_port="/dev/tty.usbmodem1111",
                vid="0x239a",
                pid="0x0029",
                role="nrf52",
            )
            assert r1["_is_new"] and not r1["_port_changed"]
            await rd.set_friendly_name(db, "SER1", "rak-bench")

            # Same device reappears on a new port.
            r2 = await rd.upsert_from_discovery(
                db,
                serial_number="SER1",
                current_port="/dev/tty.usbmodem2222",
                vid="0x239a",
                pid="0x0029",
                role="nrf52",
            )
            assert r2["_port_changed"] and not r2["_is_new"]
            assert r2["current_port"] == "/dev/tty.usbmodem2222"
            assert r2["friendly_name"] == "rak-bench"  # survived the port change

            devs = await rd.list_all(db)
            assert len(devs) == 1  # one row, not two
        finally:
            await db.close()

    asyncio.run(go())


def test_offline_marking_keeps_row(tmp_path):
    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            await rd.upsert_from_discovery(
                db,
                serial_number="SER1",
                current_port="/dev/x",
                vid="0x303a",
                pid="0x1",
                role="esp32s3",
            )
            newly_offline = await rd.mark_offline_except(db, set())
            assert newly_offline == ["SER1"]
            row = await rd.get(db, "SER1")
            assert row is not None and row["online"] == 0  # row stays, greyed
        finally:
            await db.close()

    asyncio.run(go())


def test_camera_assignment_survives_port_change(tmp_path):
    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            await rd.upsert_from_discovery(
                db,
                serial_number="SER1",
                current_port="/dev/a",
                vid="0x239a",
                pid="0x1",
                role="nrf52",
            )
            cid = await rc.add(db, name="cam", device_index="0")
            await rc.assign(db, cid, "SER1")
            # Device moves ports.
            await rd.upsert_from_discovery(
                db,
                serial_number="SER1",
                current_port="/dev/b",
                vid="0x239a",
                pid="0x1",
                role="nrf52",
            )
            cam = await rc.for_device(db, "SER1")
            assert cam is not None and cam["device_serial"] == "SER1"
        finally:
            await db.close()

    asyncio.run(go())


def test_role_to_serial_mapping(tmp_path):
    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            await rd.upsert_from_discovery(
                db,
                serial_number="NRF",
                current_port="/dev/a",
                vid="0x239a",
                pid="0x1",
                role="nrf52",
            )
            await rd.upsert_from_discovery(
                db,
                serial_number="ESP",
                current_port="/dev/b",
                vid="0x303a",
                pid="0x1",
                role="esp32s3",
            )
            nrf = await rd.online_by_role(db, "nrf52")
            esp = await rd.online_by_role(db, "esp32s3")
            assert nrf["serial_number"] == "NRF"
            assert esp["serial_number"] == "ESP"
            # mesh-pair nodeid → both roles → both serials resolvable
            run_id = await rr.create_run(
                db, args=[], seed="s", fw_branch="develop", fw_sha="abc", fw_dirty=False
            )
            await rr.add_result(
                db,
                run_id,
                nodeid="tests/mesh/test_x.py::test_y[nrf52]",
                tier="mesh",
                outcome="passed",
                duration_s=1.0,
                device_serial="NRF",
                longrepr=None,
            )
            hist = await rr.results_for_device(db, "NRF")
            assert len(hist) == 1 and hist[0]["fw_sha"] == "abc"
        finally:
            await db.close()

    asyncio.run(go())


def test_identity_helpers():
    assert identity.role_for_vid("0x239a") == "nrf52"
    assert identity.role_for_vid("0x303A") == "esp32s3"  # case-insensitive
    assert identity.role_for_vid("0x10c4") == "esp32s3"
    assert identity.role_for_vid(None) is None
    assert identity.env_for_role("nrf52") == "rak4631"
    assert identity.env_for_role("esp32s3") == "heltec-v3"

    # Device with a real serial → stable key; blank serial → surrogate.
    key, stable = identity.device_key(
        {"serial_number": "ABC", "vid": "0x239a", "pid": "1", "port": "/dev/x"}
    )
    assert key == "ABC" and stable is True
    key2, stable2 = identity.device_key(
        {"serial_number": None, "vid": "0x10c4", "pid": "0xea60", "port": "/dev/y"}
    )
    assert key2.startswith("noserial:") and stable2 is False
    assert identity.has_stable_id("ABC") and not identity.has_stable_id(key2)


def test_flash_timing_comparison(tmp_path):
    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            await rd.upsert_from_discovery(
                db,
                serial_number="SER1",
                current_port="/dev/a",
                vid="0x239a",
                pid="0x1",
                role="nrf52",
            )
            # A slow host rebuild, then a fast direct-artifact flash.
            await rf.record(
                db,
                device_serial="SER1",
                env="rak4631",
                fw_sha="abc",
                from_artifact=False,
                duration_s=210.0,
                ok=True,
            )
            await rf.record(
                db,
                device_serial="SER1",
                env="rak4631",
                fw_sha="abc",
                from_artifact=True,
                duration_s=10.0,
                ok=True,
            )
            cmp = await rf.comparison(db, "SER1")
            assert cmp["artifact"]["duration_s"] == 10.0
            assert cmp["rebuild"]["duration_s"] == 210.0
            assert cmp["speedup"] == 21.0
        finally:
            await db.close()

    asyncio.run(go())


def test_env_resolves_from_hw_model_not_just_role(monkeypatch):
    """A Heltec V4 (HELTEC_V4) must resolve to env heltec-v4, NOT the coarse
    esp32s3→heltec-v3 role default. Regression for the wrong-variant flash risk
    surfaced by real-hardware testing. Stubs the board catalog (no pio needed)."""
    import meshtastic_mcp.boards as boards_mod

    fake_catalog = [
        {"env": "heltec-v3", "hw_model_slug": "HELTEC_V3"},
        {"env": "heltec-v4", "hw_model_slug": "HELTEC_V4"},
        {"env": "heltec-v4-tft", "hw_model_slug": "HELTEC_V4"},  # variant
        {"env": "heltec-wsl-v3", "hw_model_slug": "HELTEC_WSL_V3"},
    ]
    monkeypatch.setattr(boards_mod, "list_boards", lambda *a, **k: fake_catalog)

    assert identity.env_for_hw_model("HELTEC_V4") == "heltec-v4"  # base, not -tft
    assert identity.env_for_hw_model("HELTEC_V3") == "heltec-v3"
    assert identity.env_for_hw_model("HELTEC_WSL_V3") == "heltec-wsl-v3"
    assert identity.env_for_hw_model("NOT_A_BOARD") is None
    assert identity.env_for_hw_model(None) is None

    # control.env_for_device prefers the device's resolved env over the role default.
    assert (
        control.env_for_device({"role": "esp32s3", "env": "heltec-v4"}) == "heltec-v4"
    )
    # No resolved env → falls back to the role default.
    assert control.env_for_device({"role": "esp32s3", "env": None}) == "heltec-v3"


def test_suite_env_overrides_from_connected_boards(tmp_path):
    """The test runner bakes the variant resolved per connected board: an online
    Heltec V4 → MESHTASTIC_MCP_ENV_ESP32S3=heltec-v4 (not the heltec-v3 default).
    Native (TCP) nodes and un-enriched devices are excluded."""
    from meshtastic_mcp.web.services.test_runner import resolve_env_overrides

    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            # esp32s3 V4 (enriched), an nrf52 (rak4631), a stale esp32 (no env),
            # and a native node — only the first two should produce overrides.
            await rd.upsert_from_discovery(
                db,
                serial_number="V4",
                current_port="/dev/a",
                vid="0x303a",
                pid="0x1",
                role="esp32s3",
            )
            await rd.update_enrichment(db, "V4", node_num=1, env="heltec-v4")
            await rd.upsert_from_discovery(
                db,
                serial_number="NRF",
                current_port="/dev/b",
                vid="0x239a",
                pid="0x1",
                role="nrf52",
            )
            await rd.update_enrichment(db, "NRF", node_num=2, env="rak4631")
            await rd.upsert_from_discovery(
                db,
                serial_number="UNENRICHED",
                current_port="/dev/c",
                vid="0x303a",
                pid="0x1",
                role="esp32s3",
            )  # no env yet → excluded
            await rd.upsert_native(db, name="sim", tcp_port=4403, online=True)

            rows = await rd.online_with_env(db)
            overrides = resolve_env_overrides(rows)
            assert overrides["MESHTASTIC_MCP_ENV_ESP32S3"] == "heltec-v4"
            assert overrides["MESHTASTIC_MCP_ENV_NRF52"] == "rak4631"
            # native nodes never become a flash/bake target
            assert not any("native" in v.lower() for v in overrides.values())
        finally:
            await db.close()

    asyncio.run(go())


def test_manual_env_override_survives_enrichment(tmp_path):
    """A user-pinned env must not be clobbered by auto-enrichment; releasing the
    pin lets hw_model resolution take over again."""

    async def go():
        db = _fresh_db(tmp_path)
        await db.connect()
        try:
            await rd.upsert_from_discovery(
                db,
                serial_number="D",
                current_port="/dev/a",
                vid="0x303a",
                pid="0x1",
                role="esp32s3",
            )
            # Auto enrichment resolves (wrongly, say) to heltec-v3.
            await rd.update_enrichment(db, "D", node_num=1, env="heltec-v3")
            assert (await rd.get(db, "D"))["env"] == "heltec-v3"

            # User pins the correct env.
            await rd.set_env(db, "D", "heltec-v4", locked=True)
            row = await rd.get(db, "D")
            assert row["env"] == "heltec-v4" and row["env_locked"] == 1

            # A later auto-enrichment must NOT overwrite the pinned env.
            await rd.update_enrichment(db, "D", node_num=1, env="heltec-v3")
            assert (await rd.get(db, "D"))["env"] == "heltec-v4"

            # Releasing the pin lets auto-detect win again.
            await rd.set_env(db, "D", None, locked=False)
            await rd.update_enrichment(db, "D", node_num=1, env="heltec-v3")
            assert (await rd.get(db, "D"))["env"] == "heltec-v3"
        finally:
            await db.close()

    asyncio.run(go())


def test_control_rejected_while_run_active(monkeypatch):
    # The central safety property: no connect()-based action while a run holds
    # the ports. Simulate an active run and assert every gate raises.
    monkeypatch.setattr(
        "meshtastic_mcp.web.services.test_runner.is_running", lambda: True
    )
    with pytest.raises(control.ControlBusy):
        control._ensure_idle()
    with pytest.raises(control.ControlBusy):
        control._ensure_port_free("/dev/whatever")
