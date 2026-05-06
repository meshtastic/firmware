#!/usr/bin/env python3
"""Receive NodeListReportModule packets over Meshtastic TCP and write JSONL."""

from __future__ import annotations

import argparse
import json
import signal
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from pubsub import pub

from meshtastic.protobuf import portnums_pb2
from meshtastic.tcp_interface import TCPInterface


MAGIC_V1 = b"NLR1"
MAGIC_V2 = b"NLR2"
HEADER_SIZE_V1 = 10
HEADER_SIZE_V2 = 16
RECORD_SIZE_V1 = 12
FIXED_RECORD_SIZE_V2 = 8
DEFAULT_SENDER = "!3369cbc8"
DEFAULT_DESTINATION = "!eb660bb3"


def parse_node_id(value: str) -> int:
    value = value.strip()
    if value.startswith("!"):
        return int(value[1:], 16)
    if value.startswith(("0x", "0X")):
        return int(value, 16)
    return int(value, 10)


def node_id(node_num: int) -> str:
    return f"!{node_num:08x}"


def signed_byte(value: int) -> int:
    return struct.unpack("b", bytes((value,)))[0]


def decode_v1_report(payload: bytes) -> dict[str, Any] | None:
    if len(payload) < HEADER_SIZE_V1 or payload[:4] != MAGIC_V1:
        return None

    flags = payload[4]
    record_count = payload[5]
    sequence, known_node_count = struct.unpack_from("<HH", payload, 6)
    available_records = max(0, (len(payload) - HEADER_SIZE_V1) // RECORD_SIZE_V1)
    record_count = min(record_count, available_records)

    records = []
    offset = HEADER_SIZE_V1
    for _ in range(record_count):
        node_num, age_bucket, hops, snr_bucket, record_flags, user_hash, position_hash = struct.unpack_from(
            "<IBBBBH H".replace(" ", ""), payload, offset
        )
        offset += RECORD_SIZE_V1
        records.append(
            {
                "node_num": node_num,
                "node_id": node_id(node_num),
                "age_bucket": age_bucket,
                "hops_away": None if hops == 0xFF else hops,
                "snr_bucket": signed_byte(snr_bucket),
                "flags": {
                    "new": bool(record_flags & 0x01),
                    "updated": bool(record_flags & 0x02),
                    "stale": bool(record_flags & 0x04),
                    "has_names": False,
                    "has_position_hash": bool(record_flags & 0x10),
                },
                "short_name": None,
                "long_name": None,
                "user_hash": user_hash if record_flags & 0x08 else None,
                "position_hash": position_hash if record_flags & 0x10 else None,
            }
        )

    return {
        "magic": MAGIC_V1.decode("ascii"),
        "version": 1,
        "flags": {
            "full_snapshot": bool(flags & 0x01),
            "final_chunk": True,
        },
        "sequence": sequence,
        "report_id": sequence,
        "chunk_index": 0,
        "known_node_count": known_node_count,
        "record_count": record_count,
        "records": records,
        "payload_len": len(payload),
    }


def decode_v2_report(payload: bytes) -> dict[str, Any] | None:
    if len(payload) < HEADER_SIZE_V2 or payload[:4] != MAGIC_V2:
        return None

    flags = payload[4]
    record_count = payload[5]
    sequence = struct.unpack_from("<H", payload, 6)[0]
    report_id = struct.unpack_from("<I", payload, 8)[0]
    chunk_index, known_node_count = struct.unpack_from("<HH", payload, 12)

    records = []
    offset = HEADER_SIZE_V2
    for _ in range(record_count):
        if offset + FIXED_RECORD_SIZE_V2 > len(payload):
            break
        node_num, record_flags, age_bucket, hops, snr_bucket = struct.unpack_from("<IBBBb", payload, offset)
        offset += FIXED_RECORD_SIZE_V2

        position_hash = None
        if record_flags & 0x10:
            if offset + 2 > len(payload):
                break
            position_hash = struct.unpack_from("<H", payload, offset)[0]
            offset += 2

        short_name = None
        long_name = None
        if record_flags & 0x08:
            if offset >= len(payload):
                break
            short_len = payload[offset]
            offset += 1
            if offset + short_len > len(payload):
                break
            short_name = payload[offset : offset + short_len].decode("utf-8", "replace") if short_len else ""
            offset += short_len

            if offset >= len(payload):
                break
            long_len = payload[offset]
            offset += 1
            if offset + long_len > len(payload):
                break
            long_name = payload[offset : offset + long_len].decode("utf-8", "replace") if long_len else ""
            offset += long_len

        records.append(
            {
                "node_num": node_num,
                "node_id": node_id(node_num),
                "age_bucket": age_bucket,
                "hops_away": None if hops == 0xFF else hops,
                "snr_bucket": snr_bucket,
                "flags": {
                    "new": bool(record_flags & 0x01),
                    "updated": bool(record_flags & 0x02),
                    "stale": bool(record_flags & 0x04),
                    "has_names": bool(record_flags & 0x08),
                    "has_position_hash": bool(record_flags & 0x10),
                },
                "short_name": short_name,
                "long_name": long_name,
                "user_hash": None,
                "position_hash": position_hash,
            }
        )

    return {
        "magic": MAGIC_V2.decode("ascii"),
        "version": 2,
        "flags": {
            "full_snapshot": bool(flags & 0x01),
            "final_chunk": bool(flags & 0x02),
        },
        "sequence": sequence,
        "report_id": report_id,
        "chunk_index": chunk_index,
        "known_node_count": known_node_count,
        "record_count": len(records),
        "records": records,
        "payload_len": len(payload),
    }


def decode_report(payload: bytes) -> dict[str, Any] | None:
    if payload.startswith(MAGIC_V2):
        return decode_v2_report(payload)
    return decode_v1_report(payload)


class Receiver:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.sender_num = parse_node_id(args.sender)
        self.destination_num = parse_node_id(args.destination)
        self.outfile = Path(args.outfile)
        self.interface: TCPInterface | None = None
        self.count = 0
        self.snapshots: dict[tuple[int, int], dict[int, list[dict[str, Any]]]] = {}

    def start(self) -> None:
        self.outfile.parent.mkdir(parents=True, exist_ok=True)
        pub.subscribe(self.on_receive, "meshtastic.receive")
        self.interface = TCPInterface(hostname=self.args.host, portNumber=self.args.port, noNodes=self.args.no_nodes)

    def close(self) -> None:
        if self.interface is not None:
            self.interface.close()
            self.interface = None

    def on_receive(self, packet: dict[str, Any], interface: TCPInterface) -> None:
        decoded = packet.get("decoded", {})
        portnum = decoded.get("portnum")
        if portnum not in ("PRIVATE_APP", portnums_pb2.PortNum.Name(portnums_pb2.PortNum.PRIVATE_APP)):
            return
        if packet.get("from") != self.sender_num:
            return
        if self.destination_num and packet.get("to") not in (self.destination_num, 0):
            return

        payload = decoded.get("payload", b"")
        if isinstance(payload, str):
            payload = payload.encode("latin1")
        report = decode_report(bytes(payload))
        if report is None:
            return

        event = self.build_event(packet, report)
        if event is None:
            return

        with self.outfile.open("a", encoding="utf-8") as f:
            f.write(json.dumps(event, separators=(",", ":")) + "\n")
        self.count += 1
        report_info = event["report"]
        print(
            f"wrote {event['type']} #{self.count} report_id={report_info['report_id']} records={report_info['record_count']}",
            flush=True,
        )

    def build_event(self, packet: dict[str, Any], report: dict[str, Any]) -> dict[str, Any] | None:
        event = {
            "received_at": datetime.now(timezone.utc).isoformat(),
            "from": node_id(packet.get("from", 0)),
            "to": node_id(packet.get("to", 0)),
            "packet_id": packet.get("id"),
            "rx_time": packet.get("rxTime") or packet.get("rx_time"),
            "rx_rssi": packet.get("rxRssi") or packet.get("rx_rssi"),
            "rx_snr": packet.get("rxSnr") or packet.get("rx_snr"),
        }

        if not report["flags"]["full_snapshot"]:
            event["type"] = "diff"
            event["report"] = report
            return event

        key = (packet.get("from", 0), report["report_id"])
        chunks = self.snapshots.setdefault(key, {})
        chunks[report["chunk_index"]] = report["records"]
        if not report["flags"]["final_chunk"]:
            return None

        max_chunk = max(chunks)
        missing_chunks = [i for i in range(max_chunk + 1) if i not in chunks]
        records = [record for i in range(max_chunk + 1) for record in chunks.get(i, [])]
        del self.snapshots[key]

        full_report = dict(report)
        full_report["record_count"] = len(records)
        full_report["records"] = records
        full_report["chunks_received"] = len(chunks)
        full_report["missing_chunks"] = missing_chunks
        event["type"] = "full_snapshot"
        event["report"] = full_report
        return event


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="TCP hostname or IP for the receiver node/API endpoint")
    parser.add_argument("--port", type=int, default=4403, help="Meshtastic TCP API port")
    parser.add_argument("--sender", default=DEFAULT_SENDER, help="Expected sender node ID")
    parser.add_argument("--destination", default=DEFAULT_DESTINATION, help="Expected destination node ID")
    parser.add_argument("--outfile", default="node-list-reports.jsonl", help="JSONL output path")
    parser.add_argument("--no-nodes", action="store_true", help="Skip NodeDB download on connect")
    args = parser.parse_args()

    receiver = Receiver(args)

    def stop(_signum: int, _frame: Any) -> None:
        receiver.close()
        sys.exit(0)

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    receiver.start()
    print(f"listening on {args.host}:{args.port}; writing {args.outfile}", flush=True)
    try:
        while True:
            time.sleep(1)
    finally:
        receiver.close()


if __name__ == "__main__":
    raise SystemExit(main())
