#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_dir="${repo_root}/.docker-cache"
mkdir -p "${cache_dir}/platformio" "${cache_dir}/pip" "${cache_dir}/arduino"

image_name="meshtastic-firmware-pio:local"
docker build -q -t "${image_name}" -f "${repo_root}/tools/docker/Dockerfile.pio" "${repo_root}/tools/docker" >/dev/null

device_args=()
for dev in /dev/ttyUSB* /dev/ttyACM*; do
	if [[ -e "${dev}" ]]; then
		device_args+=(--device "${dev}:${dev}" --group-add "$(stat -c '%g' "${dev}")")
	fi
done

docker run --rm \
	-u "$(id -u):$(id -g)" \
	"${device_args[@]}" \
	-e HOME=/tmp \
	-e PLATFORMIO_CORE_DIR=/work/.docker-cache/platformio \
	-e PIP_CACHE_DIR=/work/.docker-cache/pip \
	-e ARDUINO_DATA_DIR=/work/.docker-cache/arduino \
	-v "${repo_root}:/work" \
	-w /work \
	"${image_name}" \
	"$@"
