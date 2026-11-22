#!/usr/bin/env bash
# Run native PlatformIO tests inside Docker (for macOS / non-Linux hosts) through bin/run-tests.sh,
# so the verdict is its RESULT line and exit code, not pio's.
#
# Usage:
#   ./bin/test-native-docker.sh                          # run all native tests
#   ./bin/test-native-docker.sh -f test_transmit_history  # run specific test filter (FILTERED, not GREEN)
#   ./bin/test-native-docker.sh --quiet                   # print only the RESULT line
#   ./bin/test-native-docker.sh --rebuild                 # force rebuild the image
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_NAME="meshtastic-native-test"

REBUILD=false
EXTRA_ARGS=()

for arg in "$@"; do
    if [[ "$arg" == "--rebuild" ]]; then
        REBUILD=true
    else
        EXTRA_ARGS+=("$arg")
    fi
done

if $REBUILD || ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Building test image (first run may take a few minutes)..." >&2
    docker build -t "$IMAGE_NAME" -f "$ROOT_DIR/Dockerfile.test" "$ROOT_DIR" >&2
fi

# Disable BUILD_EPOCH to avoid full rebuilds between test runs (matches CI)
sed_cmd='s/-DBUILD_EPOCH=$UNIX_TIME/#-DBUILD_EPOCH=$UNIX_TIME/'

# Extra args (e.g. -f test_transmit_history, --quiet) pass through to run-tests.sh.
CMD=("./bin/run-tests.sh" "${EXTRA_ARGS[@]}")

exec docker run --rm \
    -v "$ROOT_DIR:/src:ro" \
    "$IMAGE_NAME" \
    bash -c "rm -rf /tmp/fw-test && cp -a /src /tmp/fw-test && cd /tmp/fw-test && sed -i '${sed_cmd}' platformio.ini && ${CMD[*]}"
