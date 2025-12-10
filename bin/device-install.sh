#!/usr/bin/env bash

PYTHON=${PYTHON:-$(which python3 python | head -n 1)}
BPS_RESET=false
MCU=""

# Constants
RESET_BAUD=1200
FIRMWARE_OFFSET=0x00
# Default littlefs* offset.
OFFSET=0x300000
# Default OTA Offset
OTA_OFFSET=0x260000

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

# Check for jq
if ! command -v jq >/dev/null 2>&1; then
    echo "Error: jq not found" >&2
    echo "Install jq with your package manager." >&2
    echo "e.g. 'apt install jq', 'dnf install jq', 'brew install jq', etc." >&2
    exit 1
fi

set -e

# Usage info
show_help() {
    cat <<EOF
Usage: $(basename "$0") [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME] [--1200bps-reset]
Flash image file to device, but first erasing and writing system information.

    -h               Display this help and exit.
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The firmware *.factory.bin file to flash.  Custom to your device type and region.
    --1200bps-reset  Attempt to place the device in correct mode. Some hardware requires this twice. (1200bps Reset)

EOF
}
# Parse arguments using a single while loop
while [ $# -gt 0 ]; do
    case "$1" in
    -h | --help)
        show_help
        exit 0
        ;;
    -p)
        ESPTOOL_CMD="$ESPTOOL_CMD --port $2"
        shift
        ;;
    -P)
        PYTHON="$2"
        shift
        ;;
    -f)
        FILENAME="$2"
        shift
        ;;
    --1200bps-reset)
        BPS_RESET=true
        ;;
    --) # Stop parsing options
        shift
        break
        ;;
    *)
        echo "Unknown argument: $1" >&2
        exit 1
        ;;
    esac
    shift # Move to the next argument
done

if [[ $BPS_RESET == true ]]; then
	$ESPTOOL_CMD --baud $RESET_BAUD --after no_reset read_flash_status
	exit 0
fi

[ -z "$FILENAME" ] && [ -n "$1" ] && {
    FILENAME="$1"
    shift
}

if [[ $(basename "$FILENAME") != firmware-*.factory.bin ]]; then
  echo "Filename must be a firmware-*.factory.bin file."
  exit 1
fi

# Extract PROGNAME from %FILENAME% for later use.
PROGNAME="${FILENAME/.factory.bin/}"
# Derive metadata filename from %PROGNAME%.
METAFILE="${PROGNAME}.mt.json"

if [[ -f "$FILENAME" && "$FILENAME" == *.factory.bin ]]; then
    # Display metadata if it exists
    if [[ -f "$METAFILE" ]]; then
        echo "Firmware metadata: ${METAFILE}"
        jq . "$METAFILE"
        # Extract relevant fields from metadata
        if [[ $(jq -r '.part' "$METAFILE") != "null" ]]; then
            OTA_OFFSET=$(jq -r '.part[] | select(.subtype == "ota_1") | .offset' "$METAFILE")
            SPIFFS_OFFSET=$(jq -r '.part[] | select(.subtype == "spiffs") | .offset' "$METAFILE")
        fi
        MCU=$(jq -r '.mcu' "$METAFILE")
    else
        echo "ERROR: No metadata file found at ${METAFILE}"
        exit 1
    fi

    # Determine OTA filename based on MCU type
    if [ "$MCU" == "esp32s3" ]; then
        OTAFILE=bleota-s3.bin
    elif [ "$MCU" == "esp32c3" ]; then
        OTAFILE=bleota-c3.bin
    else
        OTAFILE=bleota.bin
    fi

    # Set SPIFFS filename with "littlefs-" prefix.
    SPIFFSFILE="littlefs-${PROGNAME/firmware-/}.bin"

    if [[ ! -f "$FILENAME" ]]; then
        echo "Error: file ${FILENAME} wasn't found. Terminating."
        exit 1
    fi
    if [[ ! -f "$OTAFILE" ]]; then
        echo "Error: file ${OTAFILE} wasn't found. Terminating."
        exit 1
    fi
    if [[ ! -f "$SPIFFSFILE" ]]; then
        echo "Error: file ${SPIFFSFILE} wasn't found. Terminating."
        exit 1
    fi

    echo "Trying to flash ${FILENAME}, but first erasing and writing system information"
    $ESPTOOL_CMD erase-flash
    $ESPTOOL_CMD write-flash $FIRMWARE_OFFSET "${FILENAME}"
    echo "Trying to flash ${OTAFILE} at offset ${OTA_OFFSET}"
    $ESPTOOL_CMD write_flash $OTA_OFFSET "${OTAFILE}"
    echo "Trying to flash ${SPIFFSFILE}, at offset ${OFFSET}"
    $ESPTOOL_CMD write_flash $OFFSET "${SPIFFSFILE}"

else
    show_help
    echo "Invalid file: ${FILENAME}"
fi

exit 0
