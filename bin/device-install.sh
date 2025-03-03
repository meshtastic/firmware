#!/bin/bash

PYTHON=${PYTHON:-$(which python3 python | head -n 1)}
WEB_APP=false
TFT8=false
TFT16=false
TFT_BUILD=false

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

set -e

# Usage info
show_help() {
    cat <<EOF
Usage: $(basename $0) [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME] [--web] [--tft] [--tft-16mb]
Flash image file to device, but first erasing and writing system information"

    -h               Display this help and exit
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The .bin file to flash.  Custom to your device type and region.
    --web            Flash WEB APP.
    --tft            Flash MUI 8mb
    --tft-16mb       Flash MUI 16mb

EOF
}
# Parse arguments using a single while loop
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        -p)
            ESPTOOL_PORT="$2"
            shift # Shift past the option argument
            ;;
        -P)
            PYTHON="$2"
            shift
            ;;
        -f)
            FILENAME="$2"
            shift
            ;;
        --web)
            WEB_APP=true
            ;;
        --tft)
            TFT8=true
            ;;
        --tft-16mb)
            TFT16=true
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

[ -z "$FILENAME" -a -n "$1" ] && {
    FILENAME=$1
    shift
}

# Check if FILENAME contains "-tft-" and either TFT8 or TFT16 is 1 (--tft, -tft-16mb)
if [[ "${FILENAME//-tft-/}" != "$FILENAME" ]]; then
    TFT_BUILD=true
    if [[ "$TFT8" != true && "$TFT16" != true ]]; then
        echo "Error: Either --tft or --tft-16mb must be set to use a TFT build."
        exit 1
    fi
    if [[ "$TFT8" == true && "$TFT16" == true ]]; then
        echo "Error: Both --tft and --tft-16mb must NOT be set at the same time."
        exit 1
    fi
fi

# Extract BASENAME from %FILENAME% for later use.
BASENAME="${FILENAME/firmware-/}"

if [ -f "${FILENAME}" ] && [ -n "${FILENAME##*"update"*}" ]; then
    # Default littlefs* offset (--web).
    OFFSET=0x300000

    # Default OTA Offset
    OTA_OFFSET=0x260000

    # littlefs* offset for MUI 8mb (--tft) and OTA OFFSET.
    if [ "$TFT8" = true ]; then
        if [ "$TFT_BUILD" = true ]; then
            OFFSET=0x670000
            OTA_OFFSET=0x340000
        else
            echo "Ignoring --tft, not a TFT Build."
        fi
    fi

    # littlefs* offset for MUI 16mb (--tft-16mb) and OTA OFFSET.
    if [ "$TFT16" = true ]; then
        if [ "$TFT_BUILD" = true ]; then
            OFFSET=0xc90000
            OTA_OFFSET=0x650000
        else
            echo "Ignoring --tft-16mb, not a TFT Build."
        fi
    fi

    echo "Trying to flash ${FILENAME}, but first erasing and writing system information"
    $ESPTOOL_CMD erase_flash
    $ESPTOOL_CMD write_flash 0x00 "${FILENAME}"
    # Account for S3 board's different OTA partition
    if [ -n "${FILENAME##*"s3"*}" ] && [ -n "${FILENAME##*"-v3"*}" ] && [ -n "${FILENAME##*"t-deck"*}" ] && [ -n "${FILENAME##*"wireless-paper"*}" ] && [ -n "${FILENAME##*"wireless-tracker"*}" ] && [ -n "${FILENAME##*"station-g2"*}" ] && [ -n "${FILENAME##*"unphone"*}" ]; then
        if [ -n "${FILENAME##*"esp32c3"*}" ]; then
            $ESPTOOL_CMD write_flash $OTA_OFFSET bleota.bin
        else
            $ESPTOOL_CMD write_flash $OTA_OFFSET bleota-c3.bin
        fi
    else
        $ESPTOOL_CMD write_flash $OTA_OFFSET bleota-s3.bin
    fi

    # Check if WEB_APP (--web) is enabled and add "littlefswebui-" to BASENAME else "littlefs-".
    if [ "$WEB_APP" = true ]; then
        # Check it the file exist before trying to write it.
        if [ -f "littlefswebui-${BASENAME}" ]; then
            $ESPTOOL_CMD write_flash $OFFSET "littlefswebui-${BASENAME}"
        else
            echo "Error: file "littlefswebui-${BASENAME}" wasn't found, littlefs not written."
            exit 1
        fi
    else
        # Check it the file exist before trying to write it.
        if [ -f "littlefs-${BASENAME}" ]; then
            $ESPTOOL_CMD write_flash $OFFSET "littlefs-${BASENAME}"
        else
            echo "Error: file "littlefs-${BASENAME}" wasn't found, littlefs not written."
            exit 1
        fi
    fi

else
    show_help
    echo "Invalid file: ${FILENAME}"
fi

exit 0
