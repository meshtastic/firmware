#!/usr/bin/env bash

set -euo pipefail

ENV_NAME="rak4631"
PORT=""
BAUD=1200
COMMAND_BAUD=115200
VOLUME_NAME="RAK4631"
TIMEOUT_SECS=20
TOUCH_ATTEMPTS=3

show_help() {
cat <<'EOF'
Usage: upload-rak4631-uf2.sh [-e ENV] [-p PORT] [-v VOLUME] [-t SECONDS]

Build artifact uploader for RAK4631-class boards using the UF2 bootloader volume.
This avoids the flaky serial DFU handoff that can leave the board silent after
`pio run -t upload` on some macOS setups.

Options:
  -e ENV       PlatformIO environment name. Default: rak4631
  -p PORT      Serial device path used to trigger UF2 bootloader mode
  -v VOLUME    Bootloader volume name. Default: RAK4631
  -t SECONDS   Timeout while waiting for bootloader mount/reboot. Default: 20
  -h           Show this help

Examples:
  bin/upload-rak4631-uf2.sh
  bin/upload-rak4631-uf2.sh -p /dev/cu.usbmodem2101
  bin/upload-rak4631-uf2.sh -e rak4631
EOF
}

detect_port() {
    local candidate
    for candidate in /dev/cu.usbmodem* /dev/ttyACM* /dev/ttyUSB*; do
        if [[ -e "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

find_mountpoint() {
    local volume="$1"
    local candidate

    for candidate in \
        "/Volumes/$volume" \
        "/run/media/${USER:-}/$volume" \
        "/media/${USER:-}/$volume"; do
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

configure_port_touch() {
    local port="$1"
    if [[ "$OSTYPE" == darwin* ]]; then
        stty -f "$port" "$BAUD" hupcl clocal cread >/dev/null 2>&1 || true
    else
        stty -F "$port" "$BAUD" hupcl clocal cread >/dev/null 2>&1 || true
    fi
}

configure_port_command() {
    local port="$1"
    if [[ "$OSTYPE" == darwin* ]]; then
        stty -f "$port" "$COMMAND_BAUD" raw -echo clocal cread >/dev/null 2>&1 || true
    else
        stty -F "$port" "$COMMAND_BAUD" raw -echo clocal cread >/dev/null 2>&1 || true
    fi
}

open_close_port() {
    local port="$1"

    exec 3<>"$port" || return 1
    exec 3>&-
    return 0
}

send_console_command() {
    local port="$1"
    local command="$2"

    configure_port_command "$port"
    exec 3>"$port" || return 1
    printf '%s\r\n' "$command" >&3 || {
        exec 3>&-
        return 1
    }
    exec 3>&-
    return 0
}

wait_for_mount() {
    local volume="$1"
    local timeout="$2"
    local start now mountpoint

    start=$(date +%s)
    while true; do
        if mountpoint="$(find_mountpoint "$volume")"; then
            printf '%s\n' "$mountpoint"
            return 0
        fi

        now=$(date +%s)
        if (( now - start >= timeout )); then
            return 1
        fi
        sleep 1
    done
}

wait_for_unmount() {
    local volume="$1"
    local timeout="$2"
    local start now

    start=$(date +%s)
    while true; do
        if ! find_mountpoint "$volume" >/dev/null 2>&1; then
            return 0
        fi

        now=$(date +%s)
        if (( now - start >= timeout )); then
            return 1
        fi
        sleep 1
    done
}

trigger_bootloader() {
    local port="$1"
    local volume="$2"
    local timeout="$3"
    local attempt
    local per_attempt_timeout
    local mountpoint

    if (( timeout < TOUCH_ATTEMPTS )); then
        per_attempt_timeout=1
    else
        per_attempt_timeout=$(( timeout / TOUCH_ATTEMPTS ))
    fi

    for ((attempt = 1; attempt <= TOUCH_ATTEMPTS; attempt++)); do
        echo "Triggering UF2 bootloader via 1200 bps touch (attempt $attempt/$TOUCH_ATTEMPTS)"
        configure_port_touch "$port"
        open_close_port "$port" || true

        if mountpoint="$(wait_for_mount "$volume" "$per_attempt_timeout")"; then
            printf '%s\n' "$mountpoint"
            return 0
        fi

        sleep 1
    done

    return 1
}

request_dfu_mode() {
    local port="$1"
    local volume="$2"
    local timeout="$3"
    local mountpoint

    echo "Requesting DFU mode from running firmware"
    if ! send_console_command "$port" "dqdfu"; then
        return 1
    fi

    if mountpoint="$(wait_for_mount "$volume" "$timeout")"; then
        printf '%s\n' "$mountpoint"
        return 0
    fi

    return 1
}

while getopts ":e:p:v:t:h" opt; do
    case "$opt" in
        e) ENV_NAME="$OPTARG" ;;
        p) PORT="$OPTARG" ;;
        v) VOLUME_NAME="$OPTARG" ;;
        t) TIMEOUT_SECS="$OPTARG" ;;
        h)
            show_help
            exit 0
            ;;
        :)
            echo "Missing argument for -$OPTARG" >&2
            exit 1
            ;;
        *)
            echo "Unknown option: -$OPTARG" >&2
            exit 1
            ;;
    esac
done

PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
UF2_PATH=$(find "$PROJECT_ROOT/.pio/build/$ENV_NAME" -maxdepth 1 -name 'firmware-*.uf2' | sort | tail -n 1)

if [[ -z "$UF2_PATH" || ! -f "$UF2_PATH" ]]; then
    echo "UF2 artifact not found for env '$ENV_NAME'. Build first with: pio run -e $ENV_NAME" >&2
    exit 1
fi

if [[ -z "$PORT" ]]; then
    if ! PORT="$(detect_port)"; then
        echo "Could not auto-detect serial port. Use -p PORT." >&2
        exit 1
    fi
fi

echo "Using UF2: $UF2_PATH"
echo "Using port: $PORT"

if ! MOUNTPOINT="$(find_mountpoint "$VOLUME_NAME")"; then
    if ! MOUNTPOINT="$(request_dfu_mode "$PORT" "$VOLUME_NAME" "$TIMEOUT_SECS")"; then
        if ! MOUNTPOINT="$(trigger_bootloader "$PORT" "$VOLUME_NAME" "$TIMEOUT_SECS")"; then
        echo "Bootloader volume '$VOLUME_NAME' did not appear. Double-tap reset and retry." >&2
        exit 1
        fi
    fi
fi

echo "Copying UF2 to $MOUNTPOINT"
set +e
cp "$UF2_PATH" "$MOUNTPOINT/"
COPY_STATUS=$?
set -e

if (( COPY_STATUS != 0 )); then
    echo "UF2 copy returned a non-zero status; checking for expected reboot..."
fi

if wait_for_unmount "$VOLUME_NAME" "$TIMEOUT_SECS"; then
    echo "Board rebooted after UF2 copy"
else
    echo "Bootloader volume did not disappear after copy. Verify the board manually." >&2
    exit 1
fi

echo "RAK4631 UF2 upload complete"