"""Tests for the fake-NodeDB fixture pipeline (bin/gen-fake-nodedb-seed.py
+ bin/seed-json-to-proto.py + mcp-server fixtures.push_fake_nodedb).

Lives under tests/unit/ because none of these touch real hardware — they
shell out to the bin/ scripts and decode the resulting protobufs in-process.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import time

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SEED_GEN = REPO_ROOT / "bin" / "gen-fake-nodedb-seed.py"
COMPILE = REPO_ROOT / "bin" / "seed-json-to-proto.py"
FIXTURES_DIR = REPO_ROOT / "test" / "fixtures" / "nodedb"

# Ensure the locally-generated Python protobuf bindings are importable.
# These live under `meshtastic_v25` (not `meshtastic`) so they don't shadow
# the PyPI `meshtastic` package that the rest of the mcp-server depends on.
_BINDINGS_DIR = REPO_ROOT / "bin" / "_generated"
if _BINDINGS_DIR.is_dir() and str(_BINDINGS_DIR) not in sys.path:
    sys.path.insert(0, str(_BINDINGS_DIR))

try:
    from meshtastic_v25.deviceonly_pb2 import (
        NodeDatabase,  # type: ignore[import-not-found]
    )
except ImportError:
    NodeDatabase = None  # type: ignore[assignment]


def _require_v25_bindings() -> None:
    if NodeDatabase is None:
        pytest.skip(
            "v25 Python protobuf bindings missing; run `./bin/regen-py-protos.sh`."
        )
    if "positions" not in NodeDatabase.DESCRIPTOR.fields_by_name:
        pytest.skip(
            "Loaded NodeDatabase predates v25 — run `./bin/regen-py-protos.sh`."
        )


def _run(cmd: list[str]) -> None:
    subprocess.check_call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


# ---------------------------------------------------------------------------
# Seed generator: deterministic for given --seed (no wall-clock dependence).
# ---------------------------------------------------------------------------
def test_seed_generator_is_deterministic(tmp_path: pathlib.Path) -> None:
    a = tmp_path / "a.jsonl"
    b = tmp_path / "b.jsonl"
    _run(
        [
            sys.executable,
            str(SEED_GEN),
            "--count",
            "100",
            "--seed",
            "42",
            "--out",
            str(a),
        ]
    )
    # Sleep so any sneaky wall-clock leak in the generator would surface as
    # a byte diff between the two runs.
    time.sleep(0.8)
    _run(
        [
            sys.executable,
            str(SEED_GEN),
            "--count",
            "100",
            "--seed",
            "42",
            "--out",
            str(b),
        ]
    )
    assert a.read_bytes() == b.read_bytes()


def test_seed_generator_meta_line(tmp_path: pathlib.Path) -> None:
    out = tmp_path / "seed.jsonl"
    _run(
        [
            sys.executable,
            str(SEED_GEN),
            "--count",
            "50",
            "--seed",
            "1",
            "--out",
            str(out),
        ]
    )
    lines = out.read_text(encoding="utf-8").splitlines()
    assert len(lines) == 51  # 1 meta + 50 nodes
    meta = json.loads(lines[0])
    assert "_meta" in meta
    assert meta["_meta"]["version"] == 25
    assert meta["_meta"]["count"] == 50
    assert meta["_meta"]["seed"] == 1


def test_seed_only_uses_active_hardware_and_roles(tmp_path: pathlib.Path) -> None:
    """Confirm no deprecated roles + no off-list HW models leak through."""
    out = tmp_path / "seed.jsonl"
    _run(
        [
            sys.executable,
            str(SEED_GEN),
            "--count",
            "500",
            "--seed",
            "7",
            "--out",
            str(out),
        ]
    )
    forbidden_roles = {"ROUTER_CLIENT", "REPEATER"}
    forbidden_hw = {
        "TLORA_V1",
        "TLORA_V2",
        "TLORA_V1_1P3",
        "TLORA_V2_1_1P6",
        "TLORA_V2_1_1P8",
        "HELTEC_V1",
        "HELTEC_V2_0",
        "HELTEC_V2_1",
        "TBEAM",
        "TBEAM_V0P7",
        "NANO_G1",
        "NANO_G1_EXPLORER",
        "NANO_G2_ULTRA",
        "STATION_G1",
        "STATION_G2",
        "PORTDUINO",
        "ANDROID_SIM",
        "DIY_V1",
        "LORA_RELAY_V1",
        "NRF52840_PCA10059",
        "NRF52_UNKNOWN",
        "DR_DEV",
        "GENIEBLOCKS",
        "M5STACK",
        "RP2040_LORA",
        "PPR",
    }
    for raw in out.read_text(encoding="utf-8").splitlines()[1:]:
        node = json.loads(raw)
        assert node["role"] not in forbidden_roles, f"deprecated role: {node['role']}"
        assert (
            node["hw_model"] not in forbidden_hw
        ), f"non-tier-1 HW: {node['hw_model']}"


# ---------------------------------------------------------------------------
# Compile step + committed seeds.
# ---------------------------------------------------------------------------
@pytest.mark.parametrize("size", [250, 500, 1000, 2000])
def test_committed_seed_compiles_and_decodes(size: int, tmp_path: pathlib.Path) -> None:
    _require_v25_bindings()
    proto = tmp_path / "out.proto"
    jsonl = FIXTURES_DIR / f"seed_v25_{size:04d}.jsonl"
    if not jsonl.is_file():
        pytest.skip(f"{jsonl} not present — run ./bin/regen-fake-nodedbs.sh")
    _run([sys.executable, str(COMPILE), "--in", str(jsonl), "--out", str(proto)])

    db = NodeDatabase()
    db.ParseFromString(proto.read_bytes())
    assert db.version == 25
    assert len(db.nodes) == size
    nums = {n.num for n in db.nodes}
    assert len(nums) == size, "node numbers must be unique"
    assert all(n.long_name and n.short_name for n in db.nodes)
    assert all(len(n.long_name) <= 24 for n in db.nodes)  # max_size:25 - NUL

    # Coverage sanity (±10pp tolerance for binomial fluctuation).
    def in_range(actual: int, expected_ratio: float, tol_pp: float = 0.10) -> bool:
        lo = max(0, int((expected_ratio - tol_pp) * size))
        hi = min(size, int((expected_ratio + tol_pp) * size))
        return lo <= actual <= hi

    assert in_range(len(db.positions), 0.85)
    assert in_range(len(db.telemetry), 0.70)
    assert in_range(len(db.environment), 0.25)
    assert in_range(len(db.status), 0.40)


def test_compile_freshens_timestamps(tmp_path: pathlib.Path) -> None:
    """Same JSONL compiled twice → identical structure, different timestamps."""
    _require_v25_bindings()
    jsonl = FIXTURES_DIR / "seed_v25_0250.jsonl"
    if not jsonl.is_file():
        pytest.skip("250-node seed not present — run ./bin/regen-fake-nodedbs.sh")
    a = tmp_path / "a.proto"
    b = tmp_path / "b.proto"
    _run([sys.executable, str(COMPILE), "--in", str(jsonl), "--out", str(a)])
    time.sleep(1.2)
    _run([sys.executable, str(COMPILE), "--in", str(jsonl), "--out", str(b)])

    da = NodeDatabase()
    db_ = NodeDatabase()
    da.ParseFromString(a.read_bytes())
    db_.ParseFromString(b.read_bytes())

    # Zero out timestamp fields and confirm everything else is byte-identical.
    for d in (da, db_):
        for n in d.nodes:
            n.last_heard = 0
        for p in d.positions:
            p.position.time = 0
    assert da.SerializeToString() == db_.SerializeToString()

    # Re-load fresh copies to confirm timestamps actually moved.
    aa = NodeDatabase()
    bb = NodeDatabase()
    aa.ParseFromString(a.read_bytes())
    bb.ParseFromString(b.read_bytes())
    aa_max = max(n.last_heard for n in aa.nodes if n.last_heard)
    bb_max = max(n.last_heard for n in bb.nodes if n.last_heard)
    assert bb_max >= aa_max
    assert bb_max - aa_max < 5  # within a few seconds


def test_compile_pinned_now_epoch_is_byte_identical(tmp_path: pathlib.Path) -> None:
    """With --now-epoch pinned, two compiles produce identical bytes."""
    _require_v25_bindings()
    jsonl = FIXTURES_DIR / "seed_v25_0250.jsonl"
    if not jsonl.is_file():
        pytest.skip("250-node seed not present")
    a = tmp_path / "a.proto"
    b = tmp_path / "b.proto"
    for o in (a, b):
        _run(
            [
                sys.executable,
                str(COMPILE),
                "--in",
                str(jsonl),
                "--now-epoch",
                "1700000000",
                "--out",
                str(o),
            ]
        )
    assert a.read_bytes() == b.read_bytes()


def test_compile_timestamps_are_recent(tmp_path: pathlib.Path) -> None:
    _require_v25_bindings()
    jsonl = FIXTURES_DIR / "seed_v25_0250.jsonl"
    if not jsonl.is_file():
        pytest.skip("250-node seed not present")
    out = tmp_path / "out.proto"
    _run([sys.executable, str(COMPILE), "--in", str(jsonl), "--out", str(out)])
    db = NodeDatabase()
    db.ParseFromString(out.read_bytes())
    now = int(time.time())
    # No timestamp older than 7 days, none in the future.
    for n in db.nodes:
        if n.last_heard:
            assert now - 7 * 86400 <= n.last_heard <= now
    # At least half should be within the last hour
    # (matches expovariate(mean=3600s)).
    recent = sum(1 for n in db.nodes if n.last_heard and n.last_heard >= now - 3600)
    assert recent >= 0.4 * len(db.nodes)


def test_compile_hand_edit_round_trip(tmp_path: pathlib.Path) -> None:
    """Edit one JSONL line, recompile, confirm edit appears in the proto."""
    _require_v25_bindings()
    src = FIXTURES_DIR / "seed_v25_0250.jsonl"
    if not src.is_file():
        pytest.skip("250-node seed not present")
    dst = tmp_path / "edited.jsonl"
    lines = src.read_text(encoding="utf-8").splitlines()

    # Find a node that already has telemetry so the index relationship is
    # easy to assert on the other side.
    edit_idx = None
    for i, raw in enumerate(lines[1:], start=1):
        node = json.loads(raw)
        if node.get("telemetry") is not None:
            edit_idx = i
            break
    assert edit_idx is not None, "expected at least one node with telemetry"

    node = json.loads(lines[edit_idx])
    target_num = int(node["num"], 16)
    node["long_name"] = "Hand Edited Node"
    node["telemetry"] = {
        "battery_level": 42,
        "voltage": 3.71,
        "channel_utilization": 0.0,
        "air_util_tx": 0.0,
        "uptime_seconds": 1,
    }
    lines[edit_idx] = json.dumps(node, ensure_ascii=False, sort_keys=True)
    dst.write_text("\n".join(lines) + "\n", encoding="utf-8")

    out = tmp_path / "out.proto"
    _run([sys.executable, str(COMPILE), "--in", str(dst), "--out", str(out)])
    db = NodeDatabase()
    db.ParseFromString(out.read_bytes())
    edited = next((n for n in db.nodes if n.num == target_num), None)
    assert edited is not None
    assert edited.long_name == "Hand Edited Node"
    tel = next((t for t in db.telemetry if t.num == target_num), None)
    assert tel is not None
    assert tel.device_metrics.battery_level == 42


# ---------------------------------------------------------------------------
# Misc smoke checks on the module surface.
# ---------------------------------------------------------------------------
def test_crc16_ccitt_matches_known_vectors() -> None:
    """Sanity-check the hand-rolled CRC16-CCITT matches well-known vectors.

    Test vectors from the XModem-CRC spec (init=0, poly=0x1021):
      crc16("123456789") = 0x31C3
      crc16("")          = 0x0000
    """
    from meshtastic_mcp.fixtures import _crc16_ccitt

    assert _crc16_ccitt(b"") == 0x0000
    assert _crc16_ccitt(b"123456789") == 0x31C3


def test_push_fake_nodedb_rejects_invalid_size() -> None:
    from meshtastic_mcp.fixtures import FixtureError, push_fake_nodedb

    with pytest.raises(FixtureError, match="size must be one of"):
        push_fake_nodedb(size=999, target="portduino")  # type: ignore[arg-type]


def test_push_fake_nodedb_hardware_requires_confirm() -> None:
    from meshtastic_mcp.fixtures import FixtureError, push_fake_nodedb

    with pytest.raises(FixtureError, match="confirm=True"):
        push_fake_nodedb(size=250, target="hardware", port="/dev/cu.fake")


def test_push_fake_nodedb_hardware_requires_port() -> None:
    from meshtastic_mcp.fixtures import FixtureError, push_fake_nodedb

    with pytest.raises(FixtureError, match="requires a port"):
        push_fake_nodedb(size=250, target="hardware", confirm=True)


def test_push_fake_nodedb_hardware_rejects_tcp_port() -> None:
    from meshtastic_mcp.fixtures import FixtureError, push_fake_nodedb

    with pytest.raises(FixtureError, match="not supported"):
        push_fake_nodedb(
            size=250, target="hardware", confirm=True, port="tcp://localhost:4403"
        )
