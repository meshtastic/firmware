"""Unit tests for the persistent device-log recorder.

Hardware-free: drives the Recorder through its `_on_*` handlers with
synthetic packet/line dicts, then queries via log_query. Validates
prefix parsing, telemetry variant dispatch, marker round-trip, time
window filtering, downsampling, slope estimation, and gzip rotation
+ archive pruning.
"""

from __future__ import annotations

import gzip
import json
import logging
import os
import time
from pathlib import Path

import pubsub
import pytest
from meshtastic_mcp import log_query
from meshtastic_mcp.recorder.parsers import (
    extract_telemetry,
    interface_label,
    parse_log_line,
    summarize_packet,
)
from meshtastic_mcp.recorder.recorder import Recorder
from meshtastic_mcp.recorder.rotating import _RotatingJsonl

# -- isolation: every test gets a fresh Recorder + tmp dir -----------


@pytest.fixture
def recorder(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Recorder:
    # Redirect both the Recorder and the module-level singleton lookup
    # to the same tmp dir so log_query queries the same files we write.
    monkeypatch.setenv("MESHTASTIC_MCP_LOG_DIR", str(tmp_path))
    monkeypatch.setattr(
        "meshtastic_mcp.recorder.recorder._INSTANCE", None, raising=False
    )
    r = Recorder(base_dir=tmp_path)
    r.start()
    monkeypatch.setattr("meshtastic_mcp.recorder.recorder._INSTANCE", r, raising=False)
    yield r
    r.stop()


class _FakeIface:
    devPath = "/dev/cu.fake"


# -- parsers ---------------------------------------------------------


class TestParseLogLine:
    def test_full_prefix(self) -> None:
        out = parse_log_line("INFO  | 12:34:56 12345 [Main] Booting")
        assert out["level"] == "INFO"
        assert out["tag"] == "Main"
        assert out["uptime_s"] == 12345
        assert out["msg"] == "Booting"
        assert out["clock"] == "12:34:56"

    def test_invalid_clock(self) -> None:
        out = parse_log_line("WARN  | ??:??:?? 7 [SerialConsole] Boot")
        assert out["level"] == "WARN"
        assert out["clock"] == "??:??:??"
        assert out["uptime_s"] == 7

    def test_no_thread_bracket(self) -> None:
        out = parse_log_line("DEBUG | 00:00:00 0 raw message body")
        assert out["level"] == "DEBUG"
        assert out.get("tag") is None
        assert out["msg"] == "raw message body"

    def test_bare_message(self) -> None:
        # LogRecord.message path — no level prefix at all.
        out = parse_log_line("just a bare message")
        assert "level" not in out or out.get("level") is None
        assert out["line"] == "just a bare message"

    def test_empty(self) -> None:
        assert parse_log_line("") == {"line": ""}

    def test_debug_heap_prefix_extracted(self) -> None:
        out = parse_log_line("INFO  | 12:34:56 12345 [Main] [heap 92344] Booting")
        assert out["level"] == "INFO"
        assert out["tag"] == "Main"
        assert out["heap_free"] == 92344
        assert out["msg"] == "Booting"

    def test_debug_heap_prefix_on_bare_line(self) -> None:
        # LogRecord.message path: no level prefix but still has [heap N].
        out = parse_log_line("[heap 12345] some message")
        assert out["heap_free"] == 12345
        assert out["msg"] == "some message"

    def test_thread_leak_event(self) -> None:
        out = parse_log_line(
            "HEAP  | 00:00:01 100 [Power] [heap 90000] "
            "------ Thread MeshPacket leaked heap 92344 -> 90000 (-2344) ------"
        )
        assert out["level"] == "HEAP"
        assert out["heap_free"] == 90000
        ev = out["heap_event"]
        assert ev["kind"] == "leaked"
        assert ev["thread"] == "MeshPacket"
        assert ev["before"] == 92344
        assert ev["after"] == 90000
        assert ev["delta"] == -2344

    def test_thread_freed_event(self) -> None:
        out = parse_log_line(
            "++++++ Thread Router freed heap 1000 -> 1500 (500) ++++++"
        )
        ev = out["heap_event"]
        assert ev["kind"] == "freed"
        assert ev["thread"] == "Router"
        assert ev["delta"] == 500

    def test_heap_status_periodic(self) -> None:
        out = parse_log_line(
            "HEAP  | 00:00:30 30 [Power] "
            "Heap status: 92344/200000 bytes free (-128), running 8/12 threads"
        )
        assert out["heap_free"] == 92344
        assert out["heap_total"] == 200000
        assert out["heap_delta"] == -128


class TestRecorderDebugHeapSynthesis:
    def test_log_with_heap_writes_telemetry(self, recorder: "Recorder") -> None:
        # When a log line carries [heap N], the recorder should also
        # emit a synthesized telemetry row tagged source=debug_heap.
        recorder._on_log_line(
            "INFO  | 00:00:00 1 [Main] [heap 88888] hello",
            _FakeIface(),
        )
        telem = (recorder.base_dir / "telemetry.jsonl").read_text().splitlines()
        synth = [json.loads(r) for r in telem if '"source":"debug_heap"' in r]
        assert len(synth) == 1
        assert synth[0]["fields"]["heap_free_bytes"] == 88888
        assert synth[0]["variant"] == "local"

    def test_heap_status_writes_total_too(self, recorder: "Recorder") -> None:
        recorder._on_log_line(
            "HEAP  | 00:00:30 30 [Power] "
            "Heap status: 50000/200000 bytes free (-100), running 8/12 threads",
            _FakeIface(),
        )
        telem = (recorder.base_dir / "telemetry.jsonl").read_text().splitlines()
        synth = [json.loads(r) for r in telem if '"source":"debug_heap"' in r]
        assert synth[-1]["fields"]["heap_free_bytes"] == 50000
        assert synth[-1]["fields"]["heap_total_bytes"] == 200000

    def test_no_heap_no_synthesis(self, recorder: "Recorder") -> None:
        # Plain log line (no [heap N], no Heap status) — telemetry.jsonl
        # should NOT gain a synth row.
        before = (recorder.base_dir / "telemetry.jsonl").read_text().count("\n")
        recorder._on_log_line("INFO  | 00:00:00 1 [Main] just a message", _FakeIface())
        after = (recorder.base_dir / "telemetry.jsonl").read_text().count("\n")
        assert after == before

    def test_thread_leak_event_persists_on_log_row(self, recorder: "Recorder") -> None:
        recorder._on_log_line(
            "HEAP  | 00:00:01 100 [Power] [heap 90000] "
            "------ Thread MeshPacket leaked heap 92344 -> 90000 (-2344) ------",
            _FakeIface(),
        )
        rows = [
            json.loads(r)
            for r in (recorder.base_dir / "logs.jsonl").read_text().splitlines()
            if r
        ]
        evt_rows = [r for r in rows if r.get("heap_event")]
        assert len(evt_rows) == 1
        assert evt_rows[0]["heap_event"]["thread"] == "MeshPacket"
        assert evt_rows[0]["heap_event"]["delta"] == -2344


class TestSerialTap:
    def test_serial_line_records_log_and_synthesizes_heap(
        self, recorder: "Recorder"
    ) -> None:
        recorder._on_serial_line(
            "INFO  | 00:00:00 5 [Main] [heap 88888] tap-line",
            port="/dev/cu.tap",
        )
        logs = (recorder.base_dir / "logs.jsonl").read_text().splitlines()
        telem = (recorder.base_dir / "telemetry.jsonl").read_text().splitlines()
        log_rows = [json.loads(r) for r in logs if r]
        # Find the row from this call (port=/dev/cu.tap, role=serial_session)
        tap_rows = [r for r in log_rows if r.get("port") == "/dev/cu.tap"]
        assert len(tap_rows) == 1
        assert tap_rows[0]["role"] == "serial_session"
        assert tap_rows[0]["level"] == "INFO"
        assert tap_rows[0]["tag"] == "Main"
        assert tap_rows[0]["heap_free"] == 88888
        synth = [json.loads(r) for r in telem if '"source":"debug_heap_serial"' in r]
        assert len(synth) == 1
        assert synth[0]["fields"]["heap_free_bytes"] == 88888
        assert synth[0]["role"] == "serial_session"

    def test_serial_line_thread_leak_event(self, recorder: "Recorder") -> None:
        recorder._on_serial_line(
            "HEAP  | 00:00:30 30 [Power] [heap 53484] "
            "------ Thread Router leaked heap 53612 -> 53484 (-128) ------",
            port="/dev/cu.tap",
        )
        rows = [
            json.loads(r)
            for r in (recorder.base_dir / "logs.jsonl").read_text().splitlines()
            if r
        ]
        evt = [r for r in rows if r.get("heap_event")]
        assert len(evt) == 1
        assert evt[0]["heap_event"]["thread"] == "Router"
        assert evt[0]["heap_event"]["delta"] == -128
        # Heap also synthesized.
        telem = (recorder.base_dir / "telemetry.jsonl").read_text()
        assert '"source":"debug_heap_serial"' in telem

    def test_serial_line_pause(self, recorder: "Recorder") -> None:
        recorder.pause("baseline")
        recorder._on_serial_line(
            "INFO  | 00:00:00 1 [t] [heap 1000] dropped",
            port="/dev/cu.tap",
        )
        # Only the pause event row should exist; no tap row.
        logs = (recorder.base_dir / "logs.jsonl").read_text()
        assert "dropped" not in logs

    def test_serial_line_handler_swallows_exceptions(
        self, recorder: "Recorder"
    ) -> None:
        # Hostile input — should not raise.
        recorder._on_serial_line(None, port="/dev/cu.tap")  # type: ignore[arg-type]
        recorder._on_serial_line(b"\x00\x01\x02\x03", port="/dev/cu.tap")  # type: ignore[arg-type]
        # Survived.


class TestExtractTelemetry:
    def test_local_stats_camel(self) -> None:
        pkt = {
            "decoded": {
                "telemetry": {
                    "localStats": {"heap_total_bytes": 1000, "heap_free_bytes": 600}
                }
            }
        }
        out = extract_telemetry(pkt)
        assert out is not None
        assert out["variant"] == "local"
        assert out["fields"]["heap_free_bytes"] == 600

    def test_device_metrics_snake(self) -> None:
        pkt = {
            "decoded": {
                "telemetry": {"device_metrics": {"battery_level": 88, "voltage": 4.1}}
            }
        }
        out = extract_telemetry(pkt)
        assert out is not None
        assert out["variant"] == "device"
        assert out["fields"]["battery_level"] == 88

    def test_unknown_variant_returns_none(self) -> None:
        assert extract_telemetry({"decoded": {"telemetry": {"weird": {}}}}) is None
        assert extract_telemetry({}) is None
        assert extract_telemetry({"decoded": "not-a-dict"}) is None


class TestSummarizePacket:
    def test_text_with_payload(self) -> None:
        pkt = {
            "fromId": "!abc",
            "toId": "!def",
            "decoded": {"portnum": "TEXT_MESSAGE_APP", "payload": b"hello"},
            "hopLimit": 3,
        }
        out = summarize_packet(pkt)
        assert out["from_node"] == "!abc"
        assert out["portnum"] == "TEXT_MESSAGE_APP"
        assert out["payload_size"] == 5
        assert out["payload_hex_prefix"] == "68656c6c6f"

    def test_no_decoded(self) -> None:
        out = summarize_packet({"fromId": "!abc"})
        assert out["from_node"] == "!abc"
        assert out["portnum"] is None


class TestInterfaceLabel:
    def test_serial(self) -> None:
        assert interface_label(_FakeIface()) == {
            "port": "/dev/cu.fake",
            "role": "serial",
        }

    def test_tcp(self) -> None:
        class T:
            hostname = "node.lan"
            portNumber = 4403

        assert interface_label(T()) == {"port": "tcp://node.lan:4403", "role": "tcp"}

    def test_unknown(self) -> None:
        assert interface_label(object()) == {"port": "object", "role": None}

    def test_none(self) -> None:
        assert interface_label(None) == {"port": None, "role": None}


# -- recorder write side ---------------------------------------------


class TestRecorderWrites:
    def test_log_line_is_recorded(self, recorder: Recorder) -> None:
        recorder._on_log_line("INFO  | 12:34:56 99 [T] hi", _FakeIface())
        path = recorder.base_dir / "logs.jsonl"
        rows = [json.loads(line) for line in path.read_text().splitlines() if line]
        # First row is recorder_start_event mirror? No — that's events.jsonl only.
        assert any(r.get("level") == "INFO" and r.get("tag") == "T" for r in rows)

    def test_telemetry_recorded_and_packet_double(self, recorder: Recorder) -> None:
        # _on_telemetry alone — only telemetry.jsonl
        recorder._on_telemetry(
            {
                "fromId": "!abc",
                "decoded": {"telemetry": {"localStats": {"heap_free_bytes": 600}}},
            },
            _FakeIface(),
        )
        telem_rows = (recorder.base_dir / "telemetry.jsonl").read_text().splitlines()
        assert any('"variant":"local"' in r for r in telem_rows)

    def test_packets_summary(self, recorder: Recorder) -> None:
        recorder._on_receive(
            {
                "fromId": "!abc",
                "toId": "!def",
                "decoded": {"portnum": "TEXT_MESSAGE_APP", "payload": b"hi"},
            },
            _FakeIface(),
        )
        rows = (recorder.base_dir / "packets.jsonl").read_text().splitlines()
        assert any('"portnum":"TEXT_MESSAGE_APP"' in r for r in rows)

    def test_mark_event_round_trip(self, recorder: Recorder) -> None:
        out = recorder.mark_event("checkpoint", note="midpoint")
        assert "ts" in out
        events = (recorder.base_dir / "events.jsonl").read_text().splitlines()
        logs = (recorder.base_dir / "logs.jsonl").read_text().splitlines()
        assert any('"label":"checkpoint"' in r and '"kind":"mark"' in r for r in events)
        assert any('"level":"MARK"' in r and "checkpoint" in r for r in logs)

    def test_pause_drops_writes(self, recorder: Recorder) -> None:
        before = len((recorder.base_dir / "logs.jsonl").read_text().splitlines())
        recorder.pause(reason="baseline")
        recorder._on_log_line("INFO  | 00:00:00 1 [t] swallowed", _FakeIface())
        after = len((recorder.base_dir / "logs.jsonl").read_text().splitlines())
        assert after == before
        recorder.resume()
        recorder._on_log_line("INFO  | 00:00:00 2 [t] kept", _FakeIface())
        post_resume = (recorder.base_dir / "logs.jsonl").read_text()
        assert "kept" in post_resume

    def test_pubsub_handler_swallows_exceptions(self, recorder: Recorder) -> None:
        # If the writer dies, the pubsub callback must NOT raise — that
        # would crash the meshtastic receive thread.
        bad_packet = object()  # not a dict
        recorder._on_receive(bad_packet, _FakeIface())  # type: ignore[arg-type]
        recorder._on_telemetry(bad_packet, _FakeIface())  # type: ignore[arg-type]
        recorder._on_log_line(None, _FakeIface())  # type: ignore[arg-type]
        # No assertion needed — survival is the test.


# -- log_query read side ---------------------------------------------


class TestLogQuery:
    def test_logs_window_grep_and_level(self, recorder: Recorder) -> None:
        recorder._on_log_line("INFO  | 12:00:00 1 [A] alpha", _FakeIface())
        recorder._on_log_line("WARN  | 12:00:01 2 [B] bravo failed", _FakeIface())
        recorder._on_log_line("ERROR | 12:00:02 3 [C] charlie failed", _FakeIface())

        out = log_query.logs_window(start="-1m", level="WARN|ERROR", max_lines=10)
        assert out["total_matched"] == 2
        levels = {r["level"] for r in out["lines"]}
        assert levels == {"WARN", "ERROR"}

        out2 = log_query.logs_window(start="-1m", grep=r"failed$", max_lines=10)
        assert out2["total_matched"] == 2

    def test_logs_window_invalid_regex(self, recorder: Recorder) -> None:
        recorder._on_log_line("INFO  | 12:00:00 1 [A] alpha", _FakeIface())
        with pytest.raises(ValueError, match="invalid grep regex"):
            log_query.logs_window(start="-1m", grep="(")

    def test_telemetry_timeline_slope_and_downsample(self, recorder: Recorder) -> None:
        # Synthesize a downward leak: 100 points, free_heap drops 1 byte/sample.
        base_ts = time.time() - 60
        for i in range(100):
            recorder._files["telemetry"].write(
                {
                    "ts": base_ts + i * 0.5,
                    "port": "/dev/cu.fake",
                    "role": "serial",
                    "from_node": "!abc",
                    "variant": "local",
                    "fields": {"heap_free_bytes": 10000 - i},
                }
            )

        out = log_query.telemetry_timeline(
            window="2m", variant="local", field="free_heap", max_points=10
        )
        assert out["samples"] == 100
        assert len(out["points"]) <= 10
        # Negative slope (heap dropping). Magnitude: 1 byte every 0.5s = 120/min.
        assert out["slope_per_min"] is not None
        assert out["slope_per_min"] < -100

    def test_export_bundles_slice(self, recorder: Recorder, tmp_path: Path) -> None:
        recorder._on_log_line("INFO  | 00:00:00 1 [t] one", _FakeIface())
        recorder._on_log_line("INFO  | 00:00:00 2 [t] two", _FakeIface())
        dest = tmp_path / "bundle"
        out = log_query.export(start="-1m", end="now", dest_dir=str(dest))
        assert (dest / "logs.jsonl").exists()
        assert "logs" in out["paths"]


# -- time parser -----------------------------------------------------


class TestParseTime:
    def test_relative(self) -> None:
        now = 1_000_000.0
        assert log_query._parse_time("-15m", now=now) == now - 900
        assert log_query._parse_time("-2h", now=now) == now - 7200
        assert log_query._parse_time("-1d", now=now) == now - 86400

    def test_now_and_epoch(self) -> None:
        now = 1_000_000.0
        assert log_query._parse_time("now", now=now) == now
        assert log_query._parse_time(now) == now

    def test_iso(self) -> None:
        ts = log_query._parse_time("2026-01-01T00:00:00Z")
        assert isinstance(ts, float) and ts > 1_700_000_000

    def test_naive_iso_assumes_utc(self) -> None:
        assert log_query._parse_time("2026-01-01T00:00:00") == log_query._parse_time(
            "2026-01-01T00:00:00Z"
        )

    def test_invalid(self) -> None:
        with pytest.raises(ValueError):
            log_query._parse_time("not a time")


# -- rotation --------------------------------------------------------


class TestRotation:
    def test_size_cap_rotates_and_gzips(self, tmp_path: Path) -> None:
        path = tmp_path / "rot.jsonl"
        r = _RotatingJsonl(path, max_bytes=512, keep_archives=5, check_every=1)
        for i in range(100):
            r.write({"ts": float(i), "i": i, "pad": "x" * 40})
        r.close()
        archives = sorted(tmp_path.glob("rot.*.jsonl.gz"))
        assert archives, "expected at least one rotation"
        # Archive content is valid gzip + valid JSONL
        with gzip.open(archives[0], "rt") as fh:
            first = json.loads(fh.readline())
            assert "ts" in first

    def test_archive_pruning(self, tmp_path: Path) -> None:
        path = tmp_path / "rot.jsonl"
        r = _RotatingJsonl(path, max_bytes=200, keep_archives=2, check_every=1)
        # Force several rotations.
        for _ in range(8):
            for i in range(20):
                r.write({"ts": float(i), "pad": "x" * 30})
            r.force_rotate()
        r.close()
        archives = sorted(tmp_path.glob("rot.*.jsonl.gz"))
        assert len(archives) <= 2, f"expected ≤2 kept archives, got {len(archives)}"

    def test_archive_pruning_uses_filename_order(self, tmp_path: Path) -> None:
        path = tmp_path / "rot.jsonl"
        r = _RotatingJsonl(path, keep_archives=2)
        old = tmp_path / "rot.20260101-000000-000000-00000.jsonl.gz"
        mid = tmp_path / "rot.20260101-000001-000000-00000.jsonl.gz"
        new = tmp_path / "rot.20260101-000002-000000-00000.jsonl.gz"
        for archive in (old, mid, new):
            with gzip.open(archive, "wt", encoding="utf-8") as fh:
                fh.write('{"ts":1}\n')
        # Deliberately scramble mtimes so lexicographic filename order is
        # the only stable chronological signal.
        os.utime(old, (300, 300))
        os.utime(mid, (100, 100))
        os.utime(new, (200, 200))

        r._prune_archives()
        r.close()

        archives = sorted(p.name for p in tmp_path.glob("rot.*.jsonl.gz"))
        assert archives == [mid.name, new.name]

    def test_force_rotate_when_below_threshold(self, tmp_path: Path) -> None:
        path = tmp_path / "rot.jsonl"
        r = _RotatingJsonl(path, max_bytes=10_000_000, check_every=999_999)
        r.write({"ts": 1.0, "msg": "tiny"})
        r.force_rotate()
        r.write({"ts": 2.0, "msg": "after-rotate"})
        r.close()
        archives = sorted(tmp_path.glob("rot.*.jsonl.gz"))
        assert len(archives) == 1
        assert path.exists()
        assert "after-rotate" in path.read_text()


class TestRecorderLocks:
    def test_force_rotate_all_returns_status(self, recorder: Recorder) -> None:
        out = recorder.force_rotate_all()
        assert out["running"] is True
        assert out["files"]

    def test_wire_pubsub_logs_subscription_failure(
        self,
        tmp_path: Path,
        monkeypatch: pytest.MonkeyPatch,
        caplog: pytest.LogCaptureFixture,
    ) -> None:
        class FailingPubSubMock:
            def subscribe(self, callback: object, topic: str) -> None:
                raise RuntimeError("boom")

        monkeypatch.setattr(pubsub, "pub", FailingPubSubMock())
        recorder = Recorder(base_dir=tmp_path)
        with caplog.at_level(logging.WARNING):
            recorder._wire_pubsub()
        assert (
            "Recorder failed to subscribe to meshtastic.log.line: boom" in caplog.text
        )
