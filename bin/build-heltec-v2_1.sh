#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
default_core_dir="$project_dir/../Meshtastic-firmware/.platformio-core"
core_dir="${PLATFORMIO_CORE_DIR:-$default_core_dir}"
python="$core_dir/penv/bin/python"
cache_root="${MESHTASTIC_BUILD_CACHE_DIR:-/tmp/meshtastic-build-cache}"

if [[ ! -x "$python" ]]; then
    printf 'PlatformIO Python not found: %s\nSet PLATFORMIO_CORE_DIR to your PlatformIO core directory.\n' "$python" >&2
    exit 1
fi

export PLATFORMIO_CORE_DIR="$core_dir"
export XDG_CACHE_HOME="$cache_root"
export UV_CACHE_DIR="$cache_root/uv"

cd "$project_dir"
exec "$python" -m platformio run -e heltec-v2_1 "$@"
