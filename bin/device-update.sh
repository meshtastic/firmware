#!/bin/sh

PYTHON=${PYTHON:-$(which python3 python|head -n 1)}
CHANGE_MODE=false

# Determine the correct esptool command to use
if "$PYTHON" -m esptool version >/dev/null 2>&1; then
    ESPTOOL_CMD="$PYTHON -m esptool"
elif command -v esptool >/dev/null 2>&1; then
    ESPTOOL_CMD="esptool"
elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL_CMD="esptool.py"
else
    echo "Error: esptool not found"
    exit 1
fi

# Usage info
show_help() {
cat << EOF
Usage: $(basename "$0") [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME|FILENAME] [--change-mode]
Flash image file to device, leave existing system intact."

    -h               Display this help and exit
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The *update.bin file to flash.  Custom to your device type.
    --change-mode    Attempt to place the device in correct mode. Some hardware requires this twice. (1200bps Reset)

EOF
}

# Check for --change-mode and remove it from arguments
NEW_ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--change-mode" ]; then
        CHANGE_MODE=true
    else
        NEW_ARGS+=("$arg")
    fi
done

set -- "${NEW_ARGS[@]}"

while getopts ":hp:P:f:" opt; do
    case "${opt}" in
        h)
            show_help
            exit 0
            ;;
        p)  ESPTOOL_CMD="$ESPTOOL_CMD --port ${OPTARG}"
            ;;
        P)  PYTHON=${OPTARG}
            ;;
        f)  FILENAME=${OPTARG}
            ;;
        *)
            echo "Invalid flag."
            show_help >&2
            exit 1
            ;;
    esac
done
shift "$((OPTIND-1))"

if [ "$CHANGE_MODE" = true ]; then
	$ESPTOOL_CMD --baud 1200 --after no_reset read_flash_status
    exit 0
fi

[ -z "$FILENAME" ]  && [ -n "$1" ] && {
    FILENAME="$1"
    shift
}

if [ -f "${FILENAME}" ] && [ -z "${FILENAME##*"update"*}" ]; then
    echo "Trying to flash update ${FILENAME}"
    $ESPTOOL_CMD --baud 115200 write_flash 0x10000 "${FILENAME}"
else
    show_help
    echo "Invalid file: ${FILENAME}"
fi

exit 0
