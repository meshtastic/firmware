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


MAGIC = b"NLR1"
HEADER_SIZE = 10
RECORD_SIZE = 12
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


def decode_report(payload: bytes) -> dict[str, Any] | None:
    if len(payload) < HEADER_SIZE or payload[:4] != MAGIC:
        return None

    flags = payload[4]
    record_count = payload[5]
    sequence, known_node_count = struct.unpack_from("<HH", payload, 6)
    available_records = max(0, (len(payload) - HEADER_SIZE) // RECORD_SIZE)
    record_count = min(record_count, available_records)

    records = []
    offset = HEADER_SIZE
    for _ in range(record_count):
        node_num, age_bucket, hops, snr_bucket, record_flags, user_hash, position_hash = struct.unpack_from(
            "<IBBBBH H".replace(" ", ""), payload, offset
        )
        offset += RECORD_SIZE
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
                    "has_user_hash": bool(record_flags & 0x08),
                    "has_position_hash": bool(record_flags & 0x10),
                },
                "user_hash": user_hash if record_flags & 0x08 else None,
                "position_hash": position_hash if record_flags & 0x10 else None,
            }
        )

    return {
        "magic": MAGIC.decode("ascii"),
        "flags": {
            "full_snapshot": bool(flags & 0x01),
        },
        "sequence": sequence,
        "known_node_count": known_node_count,
        "record_count": record_count,
        "records": records,
        "payload_len": len(payload),
    }


class Receiver:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.sender_num = parse_node_id(args.sender)
        self.destination_num = parse_node_id(args.destination)
        self.outfile = Path(args.outfile)
        self.interface: TCPInterface | None = None
        self.count = 0

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

        event = {
            "received_at": datetime.now(timezone.utc).isoformat(),
            "from": node_id(packet.get("from", 0)),
            "to": node_id(packet.get("to", 0)),
            "packet_id": packet.get("id"),
            "rx_time": packet.get("rxTime") or packet.get("rx_time"),
            "rx_rssi": packet.get("rxRssi") or packet.get("rx_rssi"),
            "rx_snr": packet.get("rxSnr") or packet.get("rx_snr"),
            "report": report,
        }
        with self.outfile.open("a", encoding="utf-8") as f:
            f.write(json.dumps(event, separators=(",", ":")) + "\n")
        self.count += 1
        print(f"wrote report #{self.count} sequence={report['sequence']} records={report['record_count']}", flush=True)


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
