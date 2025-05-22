#!/bin/bash

PYTHON=${PYTHON:-$(which python3 python | head -n 1)}
WEB_APP=false
TFT_BUILD=false
MCU=""

# Variant groups
BIGDB_8MB=(
	"picomputer-s3"
	"unphone"
	"seeed-sensecap-indicator"
	"crowpanel-esp32s3"
	"heltec_capsule_sensor_v3"
	"heltec-v3"
	"heltec-vision-master-e213"
	"heltec-vision-master-e290"
	"heltec-vision-master-t190"
	"heltec-wireless-paper"
	"heltec-wireless-tracker"
	"heltec-wsl-v3"
	"icarus"
	"seeed-xiao-s3"
	"tbeam-s3-core"
	"tracksenger"
)
BIGDB_16MB=(
	"t-deck"
	"mesh-tab"
	"t-energy-s3"
	"dreamcatcher"
	"ESP32-S3-Pico"
	"m5stack-cores3"
	"station-g2"
	"t-eth-elite"
	"t-watch-s3"
)
S3_VARIANTS=(
	"s3"
	"-v3"
	"t-deck"
	"wireless-paper"
	"wireless-tracker"
	"station-g2"
	"unphone"
	"t-eth-elite"
 	"mesh-tab"
	"dreamcatcher"
	"ESP32-S3-Pico"
	"seeed-sensecap-indicator"
	"heltec_capsule_sensor_v3"
	"vision-master"
	"icarus"
	"tracksenger"
	"elecrow-adv"
)

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
Usage: $(basename $0) [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME] [--web]
Flash image file to device, but first erasing and writing system information.

    -h               Display this help and exit.
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The firmware .bin file to flash.  Custom to your device type and region.
    --web            Enable WebUI. (Default: false)

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
	--web)
		WEB_APP=true
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

if [[ $FILENAME != firmware-* ]]; then
  echo "Filename must be a firmware-* file."
  exit 1
fi

# Check if FILENAME contains "-tft-" and prevent web/mui comingling.
if [[ ${FILENAME//-tft-/} != "$FILENAME" ]]; then
	TFT_BUILD=true
	if [[ $WEB_APP == true ]] && [[ $TFT_BUILD == true ]]; then
		echo "Cannot enable WebUI (--web) and MUI."
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

	# littlefs* offset for BigDB 8mb and OTA OFFSET.
	for variant in "${BIGDB_8MB[@]}"; do
		if [ -z "${FILENAME##*"$variant"*}" ]; then
			OFFSET=0x670000
			OTA_OFFSET=0x340000
		fi
	done

	# littlefs* offset for BigDB 16mb and OTA OFFSET.
	for variant in "${BIGDB_16MB[@]}"; do
		if [ -z "${FILENAME##*"$variant"*}" ]; then
			OFFSET=0xc90000
			OTA_OFFSET=0x650000
		fi
	done

	# Account for S3 board's different OTA partition
	# FIXME: Use PlatformIO info to determine MCU type, this is unmaintainable
	for variant in "${S3_VARIANTS[@]}"; do
		if [ -z "${FILENAME##*"$variant"*}" ]; then
			MCU="esp32s3"
		fi
	done

	if [ "$MCU" != "esp32s3" ]; then
		if [ -n "${FILENAME##*"esp32c3"*}" ]; then
			OTAFILE=bleota.bin
		else
			OTAFILE=bleota-c3.bin
		fi
	else
		OTAFILE=bleota-s3.bin
	fi

	# Check if WEB_APP (--web) is enabled and add "littlefswebui-" to BASENAME else "littlefs-".
	if [ "$WEB_APP" = true ]; then
		SPIFFSFILE=littlefswebui-${BASENAME}
	else
		SPIFFSFILE=littlefs-${BASENAME}
	fi

	if [[ ! -f $FILENAME ]]; then
		echo "Error: file ${FILENAME} wasn't found. Terminating."
		exit 1
	fi
	if [[ ! -f $OTAFILE ]]; then
		echo "Error: file ${OTAFILE} wasn't found. Terminating."
		exit 1
	fi
	if [[ ! -f $SPIFFSFILE ]]; then
		echo "Error: file ${SPIFFSFILE} wasn't found. Terminating."
		exit 1
	fi

	echo "Trying to flash ${FILENAME}, but first erasing and writing system information"
	$ESPTOOL_CMD erase_flash
	$ESPTOOL_CMD write_flash 0x00 "${FILENAME}"
	echo "Trying to flash ${OTAFILE} at offset ${OTA_OFFSET}"
	$ESPTOOL_CMD write_flash $OTA_OFFSET "${OTAFILE}"
	echo "Trying to flash ${SPIFFSFILE}, at offset ${OFFSET}"
	$ESPTOOL_CMD write_flash $OFFSET "${SPIFFSFILE}"

else
	show_help
	echo "Invalid file: ${FILENAME}"
fi

exit 0
