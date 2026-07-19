#!/usr/bin/env bash
# Regenerate Python protobuf bindings from the in-tree `protobufs/` submodule
# into `bin/_generated/`. Called by bin/regen-fake-nodedbs.sh; also useful as
# a standalone refresh after any change to a .proto file.
#
# Output is .gitignored — bindings are a build artifact.
#
# Namespace rewrite:
# The .proto files declare `package meshtastic;`, which makes protoc emit
# imports like `from meshtastic import mesh_pb2`. That conflicts with the
# PyPI `meshtastic` package (which the meshtastic-mcp tooling relies on for its
# SerialInterface/BLEInterface transport). We post-process the generated
# files to live under `meshtastic_v25` instead — both the directory layout
# and all internal imports — so they coexist cleanly with the PyPI package.

set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v protoc >/dev/null 2>&1; then
    echo "ERROR: protoc not found in PATH." >&2
    echo "  macOS: brew install protobuf" >&2
    echo "  Ubuntu/Debian: apt install protobuf-compiler" >&2
    exit 1
fi

OUT=bin/_generated
LOCAL_NS=meshtastic_v25

rm -rf "$OUT"
mkdir -p "$OUT"

# 1) Generate from the in-tree protos. nanopb.proto first so its descriptor
#    is available for the [(nanopb).*] options on other messages.
protoc \
    --proto_path=protobufs \
    --python_out="$OUT" \
    protobufs/nanopb.proto \
    protobufs/meshtastic/*.proto

# 2) Move the generated `meshtastic/` directory to `meshtastic_v25/`.
mv "$OUT/meshtastic" "$OUT/$LOCAL_NS"

# 3) Rewrite internal imports: any reference to `meshtastic.X_pb2` or
#    `from meshtastic import X_pb2` becomes `meshtastic_v25.*`.
python3 bin/_rewrite_proto_namespace.py "$OUT/$LOCAL_NS" "$LOCAL_NS"

# 4) Make the package importable.
touch "$OUT/__init__.py"
touch "$OUT/$LOCAL_NS/__init__.py"

echo "regenerated Python protobuf bindings -> $OUT/$LOCAL_NS/ (namespace: $LOCAL_NS)" >&2
