#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_dir="${repo_root}/.docker-cache"
mkdir -p "${cache_dir}/platformio" "${cache_dir}/pip" "${cache_dir}/arduino"

image_name="meshtastic-firmware-pio:local"
docker build -q -t "${image_name}" -f "${repo_root}/tools/docker/Dockerfile.pio" "${repo_root}/tools/docker" >/dev/null

docker run --rm \
  -u "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e PLATFORMIO_CORE_DIR=/work/.docker-cache/platformio \
  -e PIP_CACHE_DIR=/work/.docker-cache/pip \
  -e ARDUINO_DATA_DIR=/work/.docker-cache/arduino \
  -v "${repo_root}:/work" \
  -w /work \
  "${image_name}" \
  "$@"
