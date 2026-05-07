#!/usr/bin/env python3
"""Receive WiFi Node List Report HTTP POSTs and write JSONL."""

from __future__ import annotations

import argparse
import json
import signal
import sys
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8787
DEFAULT_OUTFILE = "node-list-reports-http.jsonl"


def parse_node_id(value: str) -> str:
    value = value.strip().lower()
    if value.startswith("!"):
        return f"!{int(value[1:], 16):08x}"
    if value.startswith("0x"):
        return f"!{int(value, 16):08x}"
    return f"!{int(value, 10):08x}"


def flags_from_int(flags: int) -> dict[str, bool]:
    return {
        "new": bool(flags & 0x01),
        "updated": bool(flags & 0x02),
        "stale": bool(flags & 0x04),
        "has_names": bool(flags & 0x08),
        "has_position_hash": bool(flags & 0x10),
    }


def normalize_record(record: dict[str, Any]) -> dict[str, Any]:
    flags = record.get("flags", 0)
    if isinstance(flags, dict):
        normalized_flags = flags
    else:
        normalized_flags = flags_from_int(int(flags))

    node_num = int(record.get("num", record.get("node_num", 0)))
    node_id = record.get("node_id") or f"!{node_num:08x}"

    return {
        "node_num": node_num,
        "node_id": parse_node_id(str(node_id)),
        "age_bucket": record.get("age_bucket"),
        "hops_away": record.get("hops_away"),
        "snr_bucket": record.get("snr_bucket"),
        "flags": normalized_flags,
        "short_name": record.get("short_name"),
        "long_name": record.get("long_name"),
        "user_hash": record.get("user_hash"),
        "position_hash": record.get("position_hash"),
    }


def normalize_report(body: dict[str, Any]) -> dict[str, Any]:
    report_type = body.get("type")
    if report_type not in ("full_snapshot", "diff"):
        raise ValueError("report type must be full_snapshot or diff")

    records = body.get("records")
    if not isinstance(records, list):
        raise ValueError("records must be a list")

    sender = parse_node_id(str(body.get("from", "!00000000")))
    normalized_records = [normalize_record(record) for record in records if isinstance(record, dict)]

    return {
        "magic": "NLRW",
        "version": int(body.get("version", 1)),
        "flags": {
            "full_snapshot": report_type == "full_snapshot",
            "final_chunk": True,
        },
        "sequence": int(body.get("sequence", 0)),
        "report_id": int(body.get("sequence", 0)),
        "chunk_index": 0,
        "known_node_count": int(body.get("known_node_count", len(normalized_records))),
        "record_count": len(normalized_records),
        "records": normalized_records,
        "sender": sender,
    }


class ReceiverServer(ThreadingHTTPServer):
    outfile: Path
    expected_sender: str | None
    count: int


class Handler(BaseHTTPRequestHandler):
    server: ReceiverServer

    def do_GET(self) -> None:
        self.send_json(200, {"ok": True, "reports_written": self.server.count})

    def do_POST(self) -> None:
        content_length = self.headers.get("Content-Length")
        if content_length is None:
            self.send_json(411, {"ok": False, "error": "missing Content-Length"})
            return

        try:
            raw_body = self.rfile.read(int(content_length))
            body = json.loads(raw_body.decode("utf-8"))
            if not isinstance(body, dict):
                raise ValueError("request body must be a JSON object")
            report = normalize_report(body)
            if self.server.expected_sender and report["sender"] != self.server.expected_sender:
                self.send_json(403, {"ok": False, "error": "unexpected sender", "sender": report["sender"]})
                return
        except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as exc:
            self.send_json(400, {"ok": False, "error": str(exc)})
            return

        event = {
            "received_at": datetime.now(timezone.utc).isoformat(),
            "from": report.pop("sender"),
            "to": None,
            "transport": "http",
            "client": self.client_address[0],
            "path": self.path,
            "type": "full_snapshot" if report["flags"]["full_snapshot"] else "diff",
            "report": report,
        }

        with self.server.outfile.open("a", encoding="utf-8") as f:
            f.write(json.dumps(event, separators=(",", ":")) + "\n")

        self.server.count += 1
        print(
            f"wrote {event['type']} #{self.server.count} from={event['from']} "
            f"records={event['report']['record_count']}",
            flush=True,
        )
        self.send_json(204, None)

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"{self.address_string()} - {fmt % args}", file=sys.stderr)

    def send_json(self, status: int, body: dict[str, Any] | None) -> None:
        if body is None:
            self.send_response(status)
            self.end_headers()
            return

        encoded = json.dumps(body, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST, help="Bind address")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="HTTP listen port")
    parser.add_argument("--outfile", default=DEFAULT_OUTFILE, help="JSONL output path")
    parser.add_argument("--sender", help="Optional expected sender node ID")
    args = parser.parse_args()

    outfile = Path(args.outfile)
    outfile.parent.mkdir(parents=True, exist_ok=True)

    server = ReceiverServer((args.host, args.port), Handler)
    server.outfile = outfile
    server.expected_sender = parse_node_id(args.sender) if args.sender else None
    server.count = 0

    def stop(_signum: int, _frame: Any) -> None:
        server.shutdown()

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    print(f"listening on http://{args.host}:{args.port}; writing {outfile}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
