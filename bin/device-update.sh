#!/bin/sh

# Usage info
show_help() {
cat << EOF
Usage: ${0##*/} [-h] [-p ESPTOOL_PORT] -f FILENAME
Flash image file to device, leave existing system intact."

    -h               Display this help and exit
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerrous).
    -f FILENAME         The .bin file to flash.  Custom to your device type and region.
EOF
}


while getopts ":h:p:f:" opt; do
    case "${opt}" in
        h)
            show_help
            exit 0
            ;;
        p)  export ESPTOOL_PORT=${OPTARG}
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

if [ -f "${FILENAME}" ]; then
	echo "Trying to flash update ${FILENAME}."
	esptool.py --baud 921600 write_flash 0x10000 ${FILENAME}
else
	echo "Invalid file: ${FILENAME}"
	show_help
fi

exit 0
