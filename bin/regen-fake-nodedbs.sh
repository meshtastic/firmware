#!/usr/bin/env bash
# Regenerate the fake-NodeDB fixtures: produces 250 / 500 / 1000 / 2000-node
# JSONL seed files + their compiled v25 protobufs.
#
# Layout:
#   test/fixtures/nodedb/seed_v25_<N>.jsonl   — COMMITTED, hand-editable.
#   build/fixtures/nodedb/nodes_v25_<N>.proto — .gitignored, build artifact.
#                                               Drop into /prefs/nodes.proto.
#
# Daily use:  ./bin/regen-fake-nodedbs.sh
#   - Recompiles protos from committed seeds (fresh wall-clock timestamps).
# Intentional seed bump:  REGEN_SEEDS=yes ./bin/regen-fake-nodedbs.sh
#   - Overwrites the committed JSONL files with freshly-seeded data.

set -euo pipefail
cd "$(dirname "$0")/.."

# 1) Make sure the Python protobuf bindings exist (in-tree generation; .gitignored).
if [[ ! -d bin/_generated/meshtastic ]]; then
    echo "regenerating Python protobuf bindings (one-time)..."
    ./bin/regen-py-protos.sh
fi

# 2) Pick a Python interpreter that has the meshtastic deps installed.
#    Prefer a local venv; otherwise fall back to system python3 (install the
#    `meshtastic` package there, or run this under `uv run --with meshtastic`).
PY="python3"
for cand in .venv/bin/python3; do
    if [[ -x "$cand" ]]; then
        PY="$cand"
        break
    fi
done

# 3) Pinned seeds per size — bump only when you intentionally want different
#    structural data committed. Parallel arrays so the script works on
#    macOS bash 3.2 (no `declare -A`).
SIZES=(250 500 1000 2000)
SEEDS=(20260511 20260512 20260513 20260514)

REGEN_SEEDS="${REGEN_SEEDS:-no}"

mkdir -p build/fixtures/nodedb test/fixtures/nodedb

for i in 0 1 2 3; do
    n="${SIZES[$i]}"
    seed="${SEEDS[$i]}"
    jsonl=$(printf "test/fixtures/nodedb/seed_v25_%04d.jsonl" "$n")
    proto=$(printf "build/fixtures/nodedb/nodes_v25_%04d.proto" "$n")

    if [[ "$REGEN_SEEDS" == "yes" || ! -f "$jsonl" ]]; then
        $PY bin/gen-fake-nodedb-seed.py \
            --count "$n" \
            --seed "$seed" \
            --out "$jsonl" \
            --centroid 33.1284,-107.2528 \
            --spread-km 60 \
            --position-coverage 0.85 \
            --telemetry-coverage 0.70 \
            --environment-coverage 0.25 \
            --status-coverage 0.40
        echo "  seed:   $jsonl ($(wc -c < "$jsonl") bytes)"
    fi

    $PY bin/seed-json-to-proto.py --in "$jsonl" --out "$proto"
    echo "  proto:  $proto ($(wc -c < "$proto") bytes)"
done

echo ""
echo "Done. To load on Portduino native:"
echo "  cp build/fixtures/nodedb/nodes_v25_1000.proto ~/.portduino/default/prefs/nodes.proto"
echo ""
echo "To push to a hardware device:"
echo "  Use the meshtastic-mcp tool: push_fake_nodedb(size=1000, target=\"hardware\", port=\"/dev/cu.usbmodemXXXX\", confirm=True)"
echo "  (MCP server: https://github.com/meshtastic/meshtastic-mcp)"
