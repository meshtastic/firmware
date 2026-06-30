#!/usr/bin/env python3
"""Deterministic seed-data generator for the fake NodeDB fixture pipeline.

Writes a JSONL file describing N fake-but-realistic Meshtastic peers.
The output is hand-editable and committed; a sibling compile step
(bin/seed-json-to-proto.py) turns it into a binary `meshtastic_NodeDatabase`
v25 protobuf with fresh "now-relative" timestamps.

Determinism contract:
  Same --seed -> byte-identical JSONL output, regardless of wall clock.
  All timestamps are stored as `*_offset_sec` (seconds before "now"); the
  compile step resolves them to absolute epochs at compile time.

Structural fields covered:
  * NodeInfoLite header: num, long_name, short_name, hw_model, role,
    public_key, snr, channel, hops_away, next_hop, bitfield flags
  * PositionLite: lat/long Gaussian around --centroid, altitude, source
  * DeviceMetrics: battery/voltage/util/uptime
  * EnvironmentMetrics: temp/humidity/pressure/iaq
  * StatusMessage: error_code (usually zero)

Active-board allow-list:
  hw_model values are restricted to the intersection of
    (a) variants with `custom_meshtastic_support_level = 1` in
        variants/*/*/platformio.ini, AND
    (b) values present in the `HardwareModel` enum in mesh.proto.
  See HW_MODEL_WEIGHTS below. Deprecated boards (legacy TLORA / Heltec V1-2 /
  classic TBEAM / TBEAM_V0P7 / Nano G1 / etc.) and fuzzer-only sentinels
  (PORTDUINO, ANDROID_SIM, DIY_V1, ...) are excluded.

Active-role allow-list:
  Excludes ROUTER_CLIENT (deprecated v2.3.15) and REPEATER (deprecated v2.7.11).
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import math
import pathlib
import random
import sys

# --------------------------------------------------------------------------
# Active-board allow-list (intersection of tier-1 variants + HardwareModel enum).
# Refresh by running:
#   for f in $(find variants -name 'platformio.ini' | xargs grep -lE 'custom_meshtastic_support_level = 1'); do
#       grep custom_meshtastic_hw_model_slug $f | awk -F= '{print $2}' | tr -d ' ';
#   done | sort -u | comm -12 - <(python3 -c "from meshtastic.protobuf.mesh_pb2 import HardwareModel; print('\\n'.join(HardwareModel.keys()))" | sort)
# --------------------------------------------------------------------------
HW_MODEL_WEIGHTS: dict[str, float] = {
    "HELTEC_V3": 14.0,
    "T_DECK": 9.0,
    "HELTEC_V4": 8.0,
    "RAK4631": 8.0,
    "HELTEC_MESH_POCKET": 6.0,
    "TRACKER_T1000_E": 5.0,
    "HELTEC_MESH_NODE_T114": 5.0,
    "T_DECK_PRO": 5.0,
    "LILYGO_TBEAM_S3_CORE": 4.0,
    "HELTEC_WIRELESS_PAPER": 4.0,
    "HELTEC_WSL_V3": 3.0,
    "T_ECHO": 3.0,
    "HELTEC_WIRELESS_TRACKER": 3.0,
    "HELTEC_WIRELESS_TRACKER_V2": 2.0,
    "HELTEC_VISION_MASTER_E290": 2.0,
    "HELTEC_MESH_SOLAR": 2.0,
    "SEEED_WIO_TRACKER_L1": 2.0,
    "T_LORA_PAGER": 1.5,
    "HELTEC_VISION_MASTER_E213": 1.5,
    "T_ECHO_PLUS": 1.0,
    "MUZI_BASE": 1.0,
    "WISMESH_TAP_V2": 1.0,
    "THINKNODE_M2": 1.0,
    "THINKNODE_M5": 1.0,
    "TLORA_T3_S3": 1.0,
    # Long tail (uniform low weight across remaining tier-1 boards):
    "HELTEC_V4_R8": 0.3,
    "HELTEC_VISION_MASTER_T190": 0.3,
    "HELTEC_HT62": 0.3,
    "HELTEC_MESH_NODE_T096": 0.3,
    "M5STACK_C6L": 0.3,
    "MINI_EPAPER_S3": 0.3,
    "MUZI_R1_NEO": 0.3,
    "NOMADSTAR_METEOR_PRO": 0.3,
    "RAK3312": 0.3,
    "RAK3401": 0.3,
    "SEEED_SOLAR_NODE": 0.3,
    "SEEED_WIO_TRACKER_L1_EINK": 0.3,
    "SENSECAP_INDICATOR": 0.3,
    "TBEAM_1_WATT": 0.3,
    "THINKNODE_M1": 0.3,
    "THINKNODE_M3": 0.3,
    "THINKNODE_M6": 0.3,
    "T_ECHO_LITE": 0.3,
    "WISMESH_TAG": 0.3,
    "WISMESH_TAP": 0.3,
    "XIAO_NRF52_KIT": 0.3,
    "CROWPANEL": 0.3,
}

# Non-deprecated roles only.
ROLE_WEIGHTS: dict[str, float] = {
    "CLIENT": 75.0,
    "CLIENT_MUTE": 5.0,
    "ROUTER": 7.0,
    "TRACKER": 3.0,
    "SENSOR": 2.0,
    "CLIENT_HIDDEN": 2.0,
    "ROUTER_LATE": 2.0,
    "CLIENT_BASE": 2.0,
    "TAK": 1.0,
    "TAK_TRACKER": 0.5,
    "LOST_AND_FOUND": 0.5,
}

# Name pools — 60 firsts × 60 lasts = 3600 combinations.
FIRSTS = [
    "Quick", "Brave", "Silent", "Wild", "Lone", "Bright", "Red", "Blue",
    "Green", "Black", "White", "Iron", "Steel", "Copper", "Silver", "Gold",
    "Stone", "River", "Forest", "Mountain", "Canyon", "Desert", "Storm", "Sky",
    "Solar", "Lunar", "Dawn", "Dusk", "Misty", "Frosty", "Sunny", "Shady",
    "Happy", "Sleepy", "Drowsy", "Sneaky", "Sharp", "Smooth", "Rough", "Loud",
    "Soft", "Slow", "Fast", "Tall", "Short", "Old", "New", "Tiny",
    "Giant", "Hidden", "Lost", "Found", "Wandering", "Roving", "Drifting", "Floating",
    "Burning", "Frozen", "Whispering", "Howling",
]
LASTS = [
    "Phoenix", "Lion", "Bear", "Wolf", "Hawk", "Eagle", "Fox", "Lynx",
    "Cougar", "Coyote", "Raven", "Owl", "Crow", "Falcon", "Heron", "Crane",
    "Otter", "Badger", "Bison", "Elk", "Moose", "Stag", "Doe", "Hare",
    "Marmot", "Mole", "Beaver", "Squirrel", "Mustang", "Bronco", "Pony", "Colt",
    "Cobra", "Viper", "Mamba", "Adder", "Gecko", "Iguana", "Tortoise", "Turtle",
    "Salmon", "Trout", "Bass", "Pike", "Shark", "Whale", "Dolphin", "Seal",
    "Cactus", "Yucca", "Sage", "Juniper", "Pine", "Cedar", "Aspen", "Oak",
    "Bluff", "Mesa", "Arroyo", "Ridge",
]

# Brief callsign pool for licensed-looking suffixes.
CALLSIGN_PREFIXES = ["KX", "WD", "N5", "KE", "AB", "W5", "K1", "KQ", "AE", "NM"]

# Only emojis that fit in 4 UTF-8 bytes (no variation selectors). short_name's
# nanopb max_size:5 (incl. NUL) limits content to 4 bytes. ❄️ / ☀️ would be
# 6 bytes due to U+FE0F variation selector — explicitly excluded.
EMOJI_SHORTNAMES = ["🦊", "🐺", "🦅", "🐢", "🌵", "🔥", "🌙",
                    "🌊", "🗻", "🌲", "🦌", "🐝", "🦂", "🦉",
                    "🦇", "🦋"]

# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------

NUM_RESERVED = 4         # firmware reserves 0..3 (per NodeDB constants)
NUM_MAX_EXCLUSIVE = 0x80000000  # restrict to positive int32 range for readability


def _weighted_choice(rng: random.Random, weights: dict[str, float]) -> str:
    """Deterministic weighted pick. Uses sorted keys so dict order is fixed."""
    keys = sorted(weights.keys())
    totals = [weights[k] for k in keys]
    return rng.choices(keys, weights=totals, k=1)[0]


def _gen_long_name(rng: random.Random, is_licensed: bool) -> str:
    base = f"{rng.choice(FIRSTS)} {rng.choice(LASTS)}"
    if is_licensed:
        prefix = rng.choice(CALLSIGN_PREFIXES)
        # Two trailing alpha chars after the digit; keep within 25 - len(base) - 1
        suffix = f" {prefix}{rng.randint(0,9)}{rng.choice('ABCDEFGHIJKLMNOPQRSTUVWXYZ')}{rng.choice('ABCDEFGHIJKLMNOPQRSTUVWXYZ')}"
        # nanopb max_size:25 means C string fits 24 bytes + NUL.
        if len(base) + len(suffix) <= 24:
            base = base + suffix
    # Hard cap to 24 chars (nanopb max_size:25 minus NUL).
    return base[:24]


def _gen_short_name(rng: random.Random, long_name: str) -> str:
    # 10% emoji-only short_name
    if rng.random() < 0.10:
        return rng.choice(EMOJI_SHORTNAMES)
    first_char = long_name[0].upper() if long_name else "X"
    alphanums = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    return first_char + "".join(rng.choices(alphanums, k=3))


def _gen_hops_away(rng: random.Random) -> int:
    # Geometric-ish: 0→55%, 1→25%, 2→12%, 3→5%, 4→2%, 5+→1%
    r = rng.random()
    if r < 0.55:
        return 0
    if r < 0.80:
        return 1
    if r < 0.92:
        return 2
    if r < 0.97:
        return 3
    if r < 0.99:
        return 4
    return rng.randint(5, 7)


def _gen_position(
    rng: random.Random,
    centroid_lat: float,
    centroid_lon: float,
    spread_km: float,
    last_heard_offset_sec: int,
) -> dict:
    # 1 deg ≈ 111 km at the equator; we use this as a flat approximation.
    lat = centroid_lat + rng.gauss(0.0, spread_km / 111.0)
    lon = centroid_lon + rng.gauss(0.0, spread_km / 111.0)
    altitude = max(0, round(rng.gauss(1376.0, 250.0)))  # T or C valley floor + relief
    # Position was reported up to 300s before last_heard.
    time_offset_sec = last_heard_offset_sec + rng.randint(0, 300)
    return {
        "latitude": round(lat, 6),
        "longitude": round(lon, 6),
        "altitude": altitude,
        "time_offset_sec": time_offset_sec,
        "location_source": "LOC_INTERNAL",
    }


def _gen_telemetry(rng: random.Random) -> dict:
    # 5% plugged-in (battery_level == 101); rest uniform [10..100].
    if rng.random() < 0.05:
        battery_level = 101
        voltage = 4.20
    else:
        battery_level = rng.randint(10, 100)
        voltage = round(3.3 + (battery_level / 100.0) * 0.9, 3)
    # Beta distributions for low/right-skewed metrics; randomly draw via gammavariate.
    def _beta(a: float, b: float) -> float:
        x = rng.gammavariate(a, 1.0)
        y = rng.gammavariate(b, 1.0)
        return x / (x + y)
    channel_utilization = round(_beta(2.0, 15.0) * 100.0, 2)
    air_util_tx = round(_beta(1.5, 20.0) * 10.0, 3)
    uptime_seconds = int(rng.expovariate(1.0 / 86400.0))
    return {
        "battery_level": battery_level,
        "voltage": voltage,
        "channel_utilization": channel_utilization,
        "air_util_tx": air_util_tx,
        "uptime_seconds": uptime_seconds,
    }


def _gen_environment(rng: random.Random) -> dict:
    return {
        "temperature": round(rng.gauss(22.0, 8.0), 2),
        "relative_humidity": round(min(100.0, max(0.0, rng.gauss(55.0, 20.0))), 2),
        "barometric_pressure": round(rng.gauss(1013.0, 8.0), 2),
        "iaq": int(min(500, max(0, round(rng.gauss(50.0, 30.0))))),
    }


def _gen_status(rng: random.Random) -> dict:
    # `StatusMessage` (mesh.proto:1445) has a single free-form `string status`.
    # Most peers report a healthy short status; occasional alert string.
    healthy = ["OK", "online", "active", "running", "ready", "nominal"]
    alert = ["low-batt", "no-gps", "weak-signal", "rebooted", "offline-soon"]
    if rng.random() < 0.92:
        return {"status": rng.choice(healthy)}
    return {"status": rng.choice(alert)}


def _gen_node(
    rng: random.Random,
    num: int,
    centroid_lat: float,
    centroid_lon: float,
    spread_km: float,
    coverage: dict[str, float],
    last_heard_mean_sec: int,
    last_heard_max_sec: int,
) -> dict:
    is_licensed = rng.random() < 0.05
    long_name = _gen_long_name(rng, is_licensed)
    short_name = _gen_short_name(rng, long_name)
    hw_model = _weighted_choice(rng, HW_MODEL_WEIGHTS)
    role = _weighted_choice(rng, ROLE_WEIGHTS)
    has_public_key = rng.random() < 0.92
    public_key_hex = (
        "".join(f"{rng.randint(0,255):02x}" for _ in range(32)) if has_public_key else ""
    )
    snr = round(max(-20.0, min(12.0, rng.gauss(6.0, 4.0))), 2)
    channel = 0 if rng.random() < 0.90 else rng.randint(1, 7)
    hops_away = _gen_hops_away(rng)
    next_hop = rng.randint(0, 255) if hops_away > 0 else 0
    last_heard_offset_sec = int(min(rng.expovariate(1.0 / last_heard_mean_sec), last_heard_max_sec))

    bitfield = {
        "has_user": True,
        "is_favorite": rng.random() < 0.08,
        "is_muted": rng.random() < 0.03,
        "via_mqtt": rng.random() < 0.12,
        "is_ignored": rng.random() < 0.01,
        "is_licensed": is_licensed,
        "has_is_unmessagable": True,
        "is_unmessagable": rng.random() < 0.02,
        "is_key_manually_verified": rng.random() < 0.04,
    }

    node: dict = {
        "num": f"0x{num:08x}",
        "long_name": long_name,
        "short_name": short_name,
        "hw_model": hw_model,
        "role": role,
        "public_key_hex": public_key_hex,
        "snr": snr,
        "channel": channel,
        "hops_away": hops_away,
        "next_hop": next_hop,
        "last_heard_offset_sec": last_heard_offset_sec,
        "bitfield": bitfield,
        "position": (
            _gen_position(rng, centroid_lat, centroid_lon, spread_km, last_heard_offset_sec)
            if rng.random() < coverage["position"]
            else None
        ),
        "telemetry": _gen_telemetry(rng) if rng.random() < coverage["telemetry"] else None,
        "environment": _gen_environment(rng) if rng.random() < coverage["environment"] else None,
        "status": _gen_status(rng) if rng.random() < coverage["status"] else None,
    }
    return node


def _parse_my_node_num(s: str | None) -> int | None:
    if s is None:
        return None
    s = s.strip()
    if s.startswith("0x") or s.startswith("0X"):
        return int(s, 16)
    return int(s)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description="Deterministic JSONL seed for the fake NodeDB fixture.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--count", type=int, required=True, help="Number of fake nodes to emit.")
    p.add_argument("--seed", type=int, required=True, help="Deterministic seed.")
    p.add_argument("--out", required=True, help="Output JSONL path.")
    p.add_argument(
        "--centroid",
        default="33.1284,-107.2528",
        help="LAT,LON centroid (default: Truth or Consequences, NM).",
    )
    p.add_argument("--spread-km", type=float, default=60.0, help="Gaussian std-dev in km.")
    p.add_argument("--position-coverage", type=float, default=0.85)
    p.add_argument("--telemetry-coverage", type=float, default=0.70)
    p.add_argument("--environment-coverage", type=float, default=0.25)
    p.add_argument("--status-coverage", type=float, default=0.40)
    p.add_argument("--my-node-num", default=None, help="Exclude this NodeNum from generated set (hex or dec).")
    p.add_argument("--last-heard-mean-sec", type=int, default=3600)
    p.add_argument("--last-heard-max-sec", type=int, default=7 * 86400)
    args = p.parse_args(argv)

    if args.count <= 0:
        print("--count must be positive", file=sys.stderr)
        return 2

    try:
        centroid_lat, centroid_lon = (float(s) for s in args.centroid.split(","))
    except ValueError:
        print(f"--centroid must be LAT,LON; got {args.centroid!r}", file=sys.stderr)
        return 2

    my_node_num = _parse_my_node_num(args.my_node_num)

    rng = random.Random(args.seed)

    # 1) Generate a unique deterministic set of NodeNums.
    nums: set[int] = set()
    while len(nums) < args.count:
        n = rng.randrange(NUM_RESERVED, NUM_MAX_EXCLUSIVE)
        if my_node_num is not None and n == my_node_num:
            continue
        nums.add(n)
    ordered_nums = sorted(nums)  # sort to fix output order independent of set hash

    # 2) Per-node generation (in num order, single RNG continues).
    coverage = {
        "position": args.position_coverage,
        "telemetry": args.telemetry_coverage,
        "environment": args.environment_coverage,
        "status": args.status_coverage,
    }
    nodes = [
        _gen_node(
            rng,
            n,
            centroid_lat,
            centroid_lon,
            args.spread_km,
            coverage,
            args.last_heard_mean_sec,
            args.last_heard_max_sec,
        )
        for n in ordered_nums
    ]

    # 3) Write JSONL.
    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    # `generated_at_iso` is informational; it does NOT affect determinism because
    # we derive it from the seed, not from wall clock. (Same seed -> same string.)
    generated_at = _dt.datetime.fromtimestamp(args.seed, tz=_dt.timezone.utc).isoformat().replace("+00:00", "Z")
    meta = {
        "_meta": {
            "version": 25,
            "seed": args.seed,
            "count": args.count,
            "centroid": [centroid_lat, centroid_lon],
            "spread_km": args.spread_km,
            "generated_at_iso": generated_at,
            "my_node_num_excluded": (None if my_node_num is None else f"0x{my_node_num:08x}"),
            "coverage": coverage,
            "last_heard_mean_sec": args.last_heard_mean_sec,
            "last_heard_max_sec": args.last_heard_max_sec,
        }
    }
    with out_path.open("w", encoding="utf-8") as f:
        # `ensure_ascii=False` so emoji short_names survive. `sort_keys=True` for
        # determinism (insertion order varies by Python version otherwise).
        f.write(json.dumps(meta, ensure_ascii=False, sort_keys=True) + "\n")
        for node in nodes:
            f.write(json.dumps(node, ensure_ascii=False, sort_keys=True) + "\n")

    print(f"wrote {args.count} nodes to {out_path} ({out_path.stat().st_size} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
