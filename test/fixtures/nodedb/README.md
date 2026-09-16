# Fake NodeDB Fixtures

Deterministic JSONL seed files for the v25 `meshtastic_NodeDatabase` format
plus tooling that compiles them to binary `.proto` files and pushes them
onto a device for testing.

Centered on Truth or Consequences, NM (33.1284°N, 107.2528°W) with a 60 km
spread - change via `--centroid` and `--spread-km` if you want a different
geography.

## Pipeline

```text
   bin/gen-fake-nodedb-seed.py
              ↓  (single Random(seed); no wall-clock dependence)
   test/fixtures/nodedb/seed_v25_<N>.jsonl   ← committed, hand-editable
              ↓
   bin/seed-json-to-proto.py
              ↓  (resolves *_offset_sec → now-relative epochs at compile time)
   build/fixtures/nodedb/nodes_v25_<N>.proto  ← .gitignored, fresh timestamps
              ↓
   - Portduino: cp to ~/.portduino/<config>/prefs/nodes.proto
   - Hardware: XModem upload via the meshtastic-mcp push_fake_nodedb tool
```

## What's committed

| File                  | Size    | Purpose                                                 |
| --------------------- | ------- | ------------------------------------------------------- |
| `seed_v25_0250.jsonl` | ~200 KB | Matches ESP32-S3 high-flash MAX_NUM_NODES cap           |
| `seed_v25_0500.jsonl` | ~400 KB | Stress between caps                                     |
| `seed_v25_1000.jsonl` | ~800 KB | Large mesh stress                                       |
| `seed_v25_2000.jsonl` | ~1.6 MB | Truncation/eviction stress (exceeds every platform cap) |

## Determinism contract

**Structural fields are deterministic** given a fixed `--seed`: NodeNum,
long_name, short_name, hw_model, role, public_key, snr, channel, hops_away,
next_hop, bitfield flags, latitude/longitude/altitude, all
DeviceMetrics/EnvironmentMetrics/StatusMessage values.

**Timestamps are intentionally non-deterministic** at compile time. The JSONL
stores `*_offset_sec` (seconds before "now"); the compile step subtracts these
from current wall clock so the loaded NodeDB shows fresh "recently heard"
peers regardless of when the fixture was generated. Pass `--now-epoch T` to
the compile step to pin it for byte-identical CI artifacts.

## Active-board allow-list

`hw_model` values are restricted to the intersection of:

1. Variants with `custom_meshtastic_support_level = 1` in `variants/*/*/platformio.ini`
2. Values present in the `HardwareModel` enum in `mesh.proto`

This excludes legacy/deprecated boards (Heltec V1-V2, TLORA V1-V2, classic
TBEAM (4) and TBEAM_V0P7 (6), Nano G1, Station G1/G2, etc.) and fuzzer-only
sentinels (PORTDUINO, ANDROID_SIM, DIY_V1, LORA_RELAY_V1, etc.).

Refresh the allow-list in `bin/gen-fake-nodedb-seed.py:HW_MODEL_WEIGHTS` when
boards graduate to tier-1 (or retire). One-liner to print the current
intersection:

```bash
for f in $(find variants -name 'platformio.ini' | xargs grep -lE 'custom_meshtastic_support_level = 1'); do
    grep custom_meshtastic_hw_model_slug "$f" | awk -F= '{print $2}' | tr -d ' '
done | sort -u | comm -12 - <(
    bin/_generated/meshtastic_v25/__init__.py >/dev/null 2>&1 || ./bin/regen-py-protos.sh >&2
    python3 -c "import sys; sys.path.insert(0,'bin/_generated'); \
        from meshtastic_v25.mesh_pb2 import HardwareModel; \
        print('\n'.join(HardwareModel.keys()))" | sort
)
```

## Role allow-list

`role` is drawn from non-deprecated `Config.DeviceConfig.Role` values:

- Excluded: `ROUTER_CLIENT` (deprecated v2.3.15), `REPEATER` (deprecated v2.7.11)
- Active: CLIENT, CLIENT_MUTE, ROUTER, TRACKER, SENSOR, TAK, CLIENT_HIDDEN,
  LOST_AND_FOUND, TAK_TRACKER, ROUTER_LATE, CLIENT_BASE

## Quickstart

### Regenerate fixtures with fresh timestamps

```bash
./bin/regen-fake-nodedbs.sh
```

This recompiles all four `.proto` outputs into `build/fixtures/nodedb/` from
the committed JSONL seeds, using current wall clock for timestamps. Re-run
whenever you want "recent-looking" cached state on a freshly-booted device.

### Bump the seed (regenerate JSONL structure)

```bash
REGEN_SEEDS=yes ./bin/regen-fake-nodedbs.sh
```

This overwrites the committed JSONL files. Commit the result.

### Hand-edit a specific scenario

```bash
# Find the node you want to tweak, edit the line in place.
$EDITOR test/fixtures/nodedb/seed_v25_0250.jsonl

# Recompile and push.
./bin/regen-fake-nodedbs.sh
```

Each line of the JSONL is one node + metadata as the first line. Field schema
documented inline in `bin/gen-fake-nodedb-seed.py`. To override a specific
timestamp, replace the `last_heard_offset_sec` field with `last_heard` (an
absolute epoch); the compile step honors absolute values.

### Load onto Portduino (native macOS / linux)

```bash
cp build/fixtures/nodedb/nodes_v25_1000.proto ~/.portduino/default/prefs/nodes.proto
# Run the native binary; loadFromDisk picks it up at boot.
```

### Push to USB-attached hardware via meshtastic-mcp

```python
# From within the meshtastic-mcp tool surface
# (https://github.com/meshtastic/meshtastic-mcp):
push_fake_nodedb(
    size=500,
    target="hardware",
    port="/dev/cu.usbmodem21301",  # discover via list_devices
    confirm=True,                  # gates the destructive write + reboot
)
```

Streams the proto over XModem to `/prefs/nodes.proto`, then issues a 1-second
reboot so `loadFromDisk` picks it up on next boot. CRC16-CCITT-validated
chunks; retries each chunk up to 5× on NAK before aborting with `CAN`.

## Schema reference

See `bin/gen-fake-nodedb-seed.py` for the JSONL field reference. Key points:

- `num` is a hex string (`"0xa1b2c3d4"`)
- `public_key_hex` is 64 hex chars (32 bytes), empty for keyless nodes
- `hw_model` and `role` are enum **names**; the compile step resolves them
  via `HardwareModel.Value(name)` / `Config.DeviceConfig.Role.Value(name)`
- `bitfield` is a struct of named booleans; the compile step packs them
  per the bit positions in `src/mesh/NodeDB.h:467-484`
- `position` / `telemetry` / `environment` / `status` are nullable;
  coverage ratios at seed time decide which nodes get which
- `latitude` / `longitude` are floats in degrees (compiled to `latitude_i =
int(lat * 1e7)` matching `meshtastic_PositionLite`)
