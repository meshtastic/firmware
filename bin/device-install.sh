#!/bin/sh

PYTHON=${PYTHON:-$(which python3 python | head -n 1)}

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
Usage: $(basename $0) [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME|FILENAME]
Flash image file to device, but first erasing and writing system information"

    -h               Display this help and exit
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The .bin file to flash.  Custom to your device type and region.

EOF
}

while getopts ":hp:P:f:" opt; do
	case "${opt}" in
	h)
		show_help
		exit 0
		;;
	p)
		export ESPTOOL_PORT=${OPTARG}
		;;
	P)
		PYTHON=${OPTARG}
		;;
	f)
		FILENAME=${OPTARG}
		;;
	*)
		echo "Invalid flag."
		show_help >&2
		exit 1
		;;
	esac
done
shift "$((OPTIND - 1))"

[ -z "$FILENAME" -a -n "$1" ] && {
	FILENAME=$1
	shift
}

if [ -f "${FILENAME}" ] && [ -n "${FILENAME##*"update"*}" ]; then
	echo "Trying to flash ${FILENAME}, but first erasing and writing system information"
	$ESPTOOL_CMD erase_flash
	$ESPTOOL_CMD write_flash 0x00 ${FILENAME}
	# Account for S3 board's different OTA partition
	if [ -n "${FILENAME##*"s3"*}" ] && [ -n "${FILENAME##*"-v3"*}" ] && [ -n "${FILENAME##*"t-deck"*}" ] && [ -n "${FILENAME##*"wireless-paper"*}" ] && [ -n "${FILENAME##*"wireless-tracker"*}" ] && [ -n "${FILENAME##*"station-g2"*}" ] && [ -n "${FILENAME##*"unphone"*}" ]; then
		if [ -n "${FILENAME##*"esp32c3"*}" ]; then
			$ESPTOOL_CMD write_flash 0x260000 bleota.bin
		else
			$ESPTOOL_CMD write_flash 0x260000 bleota-c3.bin
		fi
	else
		$ESPTOOL_CMD write_flash 0x260000 bleota-s3.bin
	fi
	$ESPTOOL_CMD write_flash 0x300000 littlefs-*.bin

else
	show_help
	echo "Invalid file: ${FILENAME}"
fi

exit 0
