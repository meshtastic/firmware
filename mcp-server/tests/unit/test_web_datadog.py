"""Datadog forwarder: scrub, payload mapping (dashboard-compatible), cursor
reader, and config persistence. Pure/deterministic — no network."""

from __future__ import annotations

import asyncio
import json

from meshtastic_mcp.web.db import repo_settings as rs
from meshtastic_mcp.web.db.database import Database
from meshtastic_mcp.web.services import datadog as dd
from meshtastic_mcp.web.services.scrub import Scrubber


def test_scrub_modes():
    assert Scrubber("off").scrub("lat=37.774929") == "lat=37.774929"
    assert (
        Scrubber("coarse").scrub("lat=37.774929 lon=-122.419416")
        == "lat=37.8 lon=-122.4"
    )
    assert Scrubber("redact").scrub("latitude=37.774929") == "latitude=<scrubbed>"
    # protobuf 1e-7 integer coords
    assert Scrubber("coarse").scrub("latitude_i=377749290") == "latitude_i=377000000"
    assert Scrubber("redact").scrub("lon_i=-1224194150") == "lon_i=<scrubbed>"
    # NMEA body dropped
    assert Scrubber("coarse").scrub("$GPGGA,123519,4807.038,N") == "$GPGGA,<scrubbed>"
    # non-coordinate decimals untouched (no false positive on "latency")
    assert Scrubber("coarse").scrub("latency=12.5ms") == "latency=12.5ms"


def test_log_payload_is_dashboard_compatible():
    rec = {
        "ts": 1700000000.5,
        "port": "/dev/cu.x",
        "level": "ERROR",
        "tag": "Router",
        "line": "\x1b[31mradio fault\x1b[0m",
        "heap_free": 1234,
    }
    m = dd.log_to_dd(
        rec,
        host="bench1",
        base_tags=["collector:bench"],
        port_tags={"/dev/cu.x": ["role:esp32s3", "env:heltec-v4"]},
        scrubber=Scrubber("off"),
        ship_debug=False,
    )
    assert m["ddsource"] == "meshtastic-firmware"
    assert m["service"] == "meshtastic-firmware"
    assert m["hostname"] == "bench1"
    assert m["message"] == "radio fault"  # ANSI stripped
    assert m["status"] == "error" and m["level"] == "ERROR"
    assert m["timestamp"] == 1700000000500  # ms
    assert m["heap_free"] == 1234
    tags = set(m["ddtags"].split(","))
    assert {
        "collector:bench",
        "port:/dev/cu.x",
        "role:esp32s3",
        "env:heltec-v4",
        "level:error",
        "thread:router",
    } <= tags


def test_log_debug_skipped_but_panics_always_ship():
    base = dict(host="h", base_tags=[], port_tags={}, scrubber=Scrubber("off"))
    assert (
        dd.log_to_dd({"level": "DEBUG", "line": "x"}, ship_debug=False, **base) is None
    )
    assert (
        dd.log_to_dd({"level": "DEBUG", "line": "x"}, ship_debug=True, **base)
        is not None
    )
    # an un-leveled line (panic/backtrace) always ships
    assert (
        dd.log_to_dd(
            {"level": None, "line": "Guru Meditation"}, ship_debug=False, **base
        )
        is not None
    )


def test_metric_mapping():
    metrics = dd.telemetry_to_metrics(
        {
            "ts": 1700000000,
            "port": "/dev/cu.x",
            "variant": "device",
            "fields": {
                "battery_level": 101,
                "air_util_tx": 1.5,
                "ok": True,
                "name": "z",
            },
        },
        host="bench1",
        base_tags=["collector:bench"],
        port_tags={"/dev/cu.x": ["role:esp32s3"]},
    )
    names = {x["metric"] for x in metrics}
    assert names == {
        "mesh.device.battery_level",
        "mesh.device.air_util_tx",
    }  # bool/str dropped
    one = metrics[0]
    assert one["type"] == 3  # GAUGE
    assert one["resources"] == [{"type": "host", "name": "bench1"}]
    assert "collector:bench" in one["tags"] and "variant:device" in one["tags"]
    assert "role:esp32s3" in one["tags"]


def test_cursor_reader_partial_line_and_truncation(tmp_path):
    p = tmp_path / "logs.jsonl"
    p.write_text(
        json.dumps({"a": 1}) + "\n" + json.dumps({"a": 2}) + "\n" + '{"a": 3}'
    )  # last: no newline
    rows, cand = dd._read_live(p, {}, 100)
    assert [r["a"] for r in rows] == [1, 2]  # partial 3rd line left for next cycle
    # resume from candidate → no rows until the partial line completes
    rows2, cand2 = dd._read_live(p, cand, 100)
    assert rows2 == []
    p.write_text(
        json.dumps({"a": 1})
        + "\n"
        + json.dumps({"a": 2})
        + "\n"
        + json.dumps({"a": 3})
        + "\n"
    )
    rows3, _ = dd._read_live(p, cand, 100)
    assert [r["a"] for r in rows3] == [3]
    # a cursor pos beyond EOF (truncation/rotation) resets to 0
    rows4, _ = dd._read_live(p, {"ino": cand["ino"], "pos": 10_000_000}, 100)
    assert [r["a"] for r in rows4] == [1, 2, 3]


def test_browser_intake_origin():
    assert (
        dd._browser_intake_origin("us5.datadoghq.com")
        == "https://browser-intake-us5-datadoghq.com"
    )
    assert (
        dd._browser_intake_origin("datadoghq.eu")
        == "https://browser-intake-datadoghq.eu"
    )


def test_config_masks_key_and_persists(tmp_path):
    async def go():
        db = Database(path=tmp_path / "r.db")
        await db.connect()
        try:
            cfg = dd.DDConfig(
                enabled=True, api_key="abcd1234efgh5678", site="us5.datadoghq.com"
            )
            masked = cfg.masked()
            assert "api_key" not in masked
            assert masked["has_key"] is True and masked["key_hint"] == "5678"
            assert masked["is_client_token"] is False
            # client token detection
            assert dd.DDConfig(api_key="pubXYZ").masked()["is_client_token"] is True
            # persistence round-trip via settings store
            from dataclasses import asdict

            await rs.set_json(db, "datadog", asdict(cfg))
            back = dd.DDConfig.from_dict(await rs.get_json(db, "datadog"))
            assert back.enabled and back.api_key == "abcd1234efgh5678"
            assert back.site == "us5.datadoghq.com"
        finally:
            await db.close()

    asyncio.run(go())
