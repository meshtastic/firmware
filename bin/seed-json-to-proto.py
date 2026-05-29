#!/usr/bin/env python3
"""Compile a committed seed JSONL into a binary meshtastic_NodeDatabase v25 proto.

The input is produced by `bin/gen-fake-nodedb-seed.py`. Timestamps in the JSONL
are stored as `*_offset_sec` (seconds before "now"); this script resolves them
to absolute epochs using `--now-epoch` (default: current wall clock).

Output is a raw `pb_encode`-compatible binary that can be dropped at
`/prefs/nodes.proto` on the device (Portduino prefs dir or hardware via
XModem) and loaded by `NodeDB::loadFromDisk` at boot.

Wire format reference:
  protobufs/meshtastic/deviceonly.proto  (NodeDatabase, NodeInfoLite, sat entries)
  src/mesh/NodeDB.h:467-484              (bitfield bit positions)
  src/mesh/NodeDB.cpp:1523-1524          (pb_decode entry point)
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time
from typing import Any

# Prefer the in-tree generated Python protobuf bindings (bin/_generated/meshtastic_v25/)
# because the firmware branch's protos (v25 NodeDatabase satellite arrays, slim
# NodeInfoLite) are typically newer than what the PyPI `meshtastic` package
# ships. Run `bin/regen-py-protos.sh` to (re)generate.
#
# Namespace note: the local bindings live under `meshtastic_v25` (NOT `meshtastic`)
# to avoid shadowing the PyPI `meshtastic` package — bin/regen-py-protos.sh
# post-processes the protoc output to rename the package.
_HERE = pathlib.Path(__file__).resolve().parent
_LOCAL_PROTO_DIR = _HERE / "_generated"
if _LOCAL_PROTO_DIR.is_dir():
    sys.path.insert(0, str(_LOCAL_PROTO_DIR))

try:
    from meshtastic_v25.deviceonly_pb2 import (  # type: ignore[import-not-found]
        NodeDatabase,
        NodeInfoLite,
        NodePositionEntry,
        NodeTelemetryEntry,
        NodeEnvironmentEntry,
        NodeStatusEntry,
        PositionLite,
    )
    from meshtastic_v25.mesh_pb2 import HardwareModel, Position, StatusMessage  # type: ignore[import-not-found]
    from meshtastic_v25.config_pb2 import Config  # type: ignore[import-not-found]
    from meshtastic_v25.telemetry_pb2 import DeviceMetrics, EnvironmentMetrics  # type: ignore[import-not-found]
except ImportError as local_err:
    # Fall back to the PyPI package if in-tree bindings haven't been generated.
    # Will fail the v25 assertion below if the PyPI package predates the
    # satellite-DB schema, but at least gives a clear "run regen-py-protos.sh"
    # error message instead of an opaque ImportError.
    try:
        from meshtastic.protobuf.deviceonly_pb2 import (
            NodeDatabase,
            NodeInfoLite,
            NodePositionEntry,
            NodeTelemetryEntry,
            NodeEnvironmentEntry,
            NodeStatusEntry,
            PositionLite,
        )
        from meshtastic.protobuf.mesh_pb2 import HardwareModel, Position, StatusMessage
        from meshtastic.protobuf.config_pb2 import Config
        from meshtastic.protobuf.telemetry_pb2 import DeviceMetrics, EnvironmentMetrics
    except ImportError as pypi_err:
        print(
            "ERROR: could not import meshtastic protobuf bindings.\n"
            "  In-tree generation: run `bin/regen-py-protos.sh` (requires protoc).\n"
            "  PyPI fallback: `pip install meshtastic` (may lag firmware branch).\n"
            f"  local error (meshtastic_v25): {local_err}\n"
            f"  pypi error  (meshtastic.protobuf): {pypi_err}",
            file=sys.stderr,
        )
        sys.exit(1)

# Fail loudly if bindings predate v25 (no satellite arrays).
assert (
    hasattr(NodeDatabase, "DESCRIPTOR")
    and "positions" in NodeDatabase.DESCRIPTOR.fields_by_name
), (
    "Loaded meshtastic bindings are older than v25 (NodeDatabase.positions missing). "
    "Run `bin/regen-py-protos.sh` against the in-tree protobufs/ submodule."
)

# ---------------------------------------------------------------------------
# Bitfield bit positions (mirror src/mesh/NodeDB.h:467-484).
# ---------------------------------------------------------------------------
BIT_IS_KEY_MANUALLY_VERIFIED = 0
BIT_IS_MUTED = 1
BIT_VIA_MQTT = 2
BIT_IS_FAVORITE = 3
BIT_IS_IGNORED = 4
BIT_HAS_USER = 5
BIT_IS_LICENSED = 6
BIT_IS_UNMESSAGABLE = 7
BIT_HAS_IS_UNMESSAGABLE = 8

BITFIELD_LAYOUT = (
    # JSON key            bit position
    ("is_key_manually_verified", BIT_IS_KEY_MANUALLY_VERIFIED),
    ("is_muted", BIT_IS_MUTED),
    ("via_mqtt", BIT_VIA_MQTT),
    ("is_favorite", BIT_IS_FAVORITE),
    ("is_ignored", BIT_IS_IGNORED),
    ("has_user", BIT_HAS_USER),
    ("is_licensed", BIT_IS_LICENSED),
    ("is_unmessagable", BIT_IS_UNMESSAGABLE),
    ("has_is_unmessagable", BIT_HAS_IS_UNMESSAGABLE),
)


def _pack_bitfield(bf: dict[str, bool]) -> int:
    out = 0
    for key, shift in BITFIELD_LAYOUT:
        if bf.get(key, False):
            out |= (1 << shift)
    return out


def _validate_node(node: dict[str, Any]) -> None:
    """Friendly errors so hand-editors get clear feedback."""
    if "num" not in node or not isinstance(node["num"], str):
        raise ValueError(f"node missing/invalid 'num' (must be hex string): {node!r}")
    if "long_name" not in node:
        raise ValueError(f"node {node['num']}: missing 'long_name'")
    if len(node["long_name"]) > 24:
        raise ValueError(
            f"node {node['num']}: long_name {node['long_name']!r} is "
            f"{len(node['long_name'])} chars; max 24 (nanopb max_size:25 minus NUL)"
        )
    if "short_name" in node:
        # short_name max_size:5 (incl. NUL) → 4 bytes of content.
        # Char count is irrelevant — emojis with variation selectors (e.g. ❄️ = 6 B)
        # would slip past a `len(str) > 4` check. Always measure bytes.
        b = node["short_name"].encode("utf-8")
        if len(b) > 4:
            raise ValueError(
                f"node {node['num']}: short_name {node['short_name']!r} is "
                f"{len(b)} bytes UTF-8; max 4 (nanopb max_size:5 minus NUL)"
            )
    pk = node.get("public_key_hex", "")
    if pk and len(pk) != 64:
        raise ValueError(
            f"node {node['num']}: public_key_hex must be 64 hex chars or empty; "
            f"got {len(pk)} chars"
        )
    if pk:
        try:
            bytes.fromhex(pk)
        except ValueError as e:
            raise ValueError(f"node {node['num']}: public_key_hex is not valid hex: {e}")


def _resolve_time(
    node: dict[str, Any],
    field_absolute: str,
    field_offset: str,
    now_epoch: int,
) -> int:
    """If `field_absolute` is set, use it; else compute `now_epoch - offset`."""
    if field_absolute in node and node[field_absolute] is not None:
        return int(node[field_absolute])
    offset = node.get(field_offset, 0)
    return max(0, int(now_epoch) - int(offset))


def _build_node_info_lite(node: dict[str, Any], now_epoch: int) -> NodeInfoLite:
    _validate_node(node)
    info = NodeInfoLite()
    info.num = int(node["num"], 16) if isinstance(node["num"], str) else int(node["num"])
    info.long_name = node.get("long_name", "")
    info.short_name = node.get("short_name", "")
    # Enum lookups will raise ValueError on unknown names — that's exactly what we want.
    info.hw_model = HardwareModel.Value(node.get("hw_model", "UNSET"))
    info.role = Config.DeviceConfig.Role.Value(node.get("role", "CLIENT"))
    pk_hex = node.get("public_key_hex", "")
    if pk_hex:
        info.public_key = bytes.fromhex(pk_hex)
    info.snr = float(node.get("snr", 0.0))
    info.channel = int(node.get("channel", 0))
    if "hops_away" in node:
        # `optional uint32 hops_away = 9;` — in Python protobuf, assigning the
        # field implicitly sets HasField("hops_away") to True. No has_hops_away
        # setter exists (unlike the C++ nanopb-generated header).
        info.hops_away = int(node["hops_away"])
    info.next_hop = int(node.get("next_hop", 0))
    info.last_heard = _resolve_time(node, "last_heard", "last_heard_offset_sec", now_epoch)
    info.bitfield = _pack_bitfield(node.get("bitfield", {}))
    return info


def _build_position_entry(num: int, pos: dict[str, Any], now_epoch: int) -> NodePositionEntry:
    entry = NodePositionEntry()
    entry.num = num
    pl = PositionLite()
    # Firmware stores lat/long as int32 in 1e-7 degrees.
    pl.latitude_i = int(round(float(pos["latitude"]) * 1e7))
    pl.longitude_i = int(round(float(pos["longitude"]) * 1e7))
    pl.altitude = int(pos.get("altitude", 0))
    pl.time = _resolve_time(pos, "time", "time_offset_sec", now_epoch)
    pl.location_source = Position.LocSource.Value(pos.get("location_source", "LOC_UNSET"))
    entry.position.CopyFrom(pl)
    return entry


def _build_telemetry_entry(num: int, tel: dict[str, Any]) -> NodeTelemetryEntry:
    entry = NodeTelemetryEntry()
    entry.num = num
    dm = DeviceMetrics()
    if "battery_level" in tel:
        dm.battery_level = int(tel["battery_level"])
    if "voltage" in tel:
        dm.voltage = float(tel["voltage"])
    if "channel_utilization" in tel:
        dm.channel_utilization = float(tel["channel_utilization"])
    if "air_util_tx" in tel:
        dm.air_util_tx = float(tel["air_util_tx"])
    if "uptime_seconds" in tel:
        dm.uptime_seconds = int(tel["uptime_seconds"])
    entry.device_metrics.CopyFrom(dm)
    return entry


def _build_environment_entry(num: int, env: dict[str, Any]) -> NodeEnvironmentEntry:
    entry = NodeEnvironmentEntry()
    entry.num = num
    em = EnvironmentMetrics()
    if "temperature" in env:
        em.temperature = float(env["temperature"])
    if "relative_humidity" in env:
        em.relative_humidity = float(env["relative_humidity"])
    if "barometric_pressure" in env:
        em.barometric_pressure = float(env["barometric_pressure"])
    if "iaq" in env:
        em.iaq = int(env["iaq"])
    entry.environment_metrics.CopyFrom(em)
    return entry


def _build_status_entry(num: int, status: dict[str, Any]) -> NodeStatusEntry:
    # `StatusMessage` (mesh.proto:1445) has a single `string status` field.
    entry = NodeStatusEntry()
    entry.num = num
    sm = StatusMessage()
    if "status" in status:
        sm.status = str(status["status"])
    entry.status.CopyFrom(sm)
    return entry


def compile_jsonl_to_proto(jsonl_path: pathlib.Path, now_epoch: int) -> bytes:
    """Read a seed JSONL and return the encoded NodeDatabase bytes."""
    lines = jsonl_path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise ValueError(f"{jsonl_path} is empty")
    meta_line = lines[0]
    meta_obj = json.loads(meta_line)
    meta = meta_obj.get("_meta", {})
    version = meta.get("version")
    if version != 25:
        raise ValueError(
            f"{jsonl_path}: meta version is {version!r}; this compiler "
            f"requires version=25. Regenerate the seed with the matching tooling."
        )

    db = NodeDatabase()
    db.version = 25

    for ln, raw in enumerate(lines[1:], start=2):
        raw = raw.strip()
        if not raw:
            continue
        try:
            node = json.loads(raw)
        except json.JSONDecodeError as e:
            raise ValueError(f"{jsonl_path}:{ln} JSON parse error: {e}")

        num = int(node["num"], 16) if isinstance(node["num"], str) else int(node["num"])

        # Header
        info = _build_node_info_lite(node, now_epoch)
        db.nodes.append(info)

        # Satellites (nullable)
        if node.get("position"):
            db.positions.append(_build_position_entry(num, node["position"], now_epoch))
        if node.get("telemetry"):
            db.telemetry.append(_build_telemetry_entry(num, node["telemetry"]))
        if node.get("environment"):
            db.environment.append(_build_environment_entry(num, node["environment"]))
        if node.get("status"):
            db.status.append(_build_status_entry(num, node["status"]))

    return db.SerializeToString()


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description="Compile a seed JSONL into a binary v25 NodeDatabase proto.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--in", dest="in_path", required=True, help="Input seed JSONL.")
    p.add_argument("--out", required=True, help="Output binary .proto path.")
    p.add_argument(
        "--now-epoch",
        type=int,
        default=None,
        help="Pin 'now' to this Unix epoch (for byte-identical CI). Default: time.time().",
    )
    args = p.parse_args(argv)

    in_path = pathlib.Path(args.in_path)
    if not in_path.is_file():
        print(f"input not found: {in_path}", file=sys.stderr)
        return 2

    now_epoch = args.now_epoch if args.now_epoch is not None else int(time.time())

    try:
        encoded = compile_jsonl_to_proto(in_path, now_epoch)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 3

    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(encoded)
    print(
        f"compiled {in_path} -> {out_path} ({len(encoded)} bytes, now_epoch={now_epoch})",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
