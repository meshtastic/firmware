#!/bin/sh

PYTHON=${PYTHON:-$(which python3 python|head -n 1)}

# Usage info
show_help() {
cat << EOF
Usage: $(basename $0) [-h] [-p ESPTOOL_PORT] [-P PYTHON] [-f FILENAME|FILENAME]
Flash image file to device, leave existing system intact."

    -h               Display this help and exit
    -p ESPTOOL_PORT  Set the environment variable for ESPTOOL_PORT.  If not set, ESPTOOL iterates all ports (Dangerrous).
    -P PYTHON        Specify alternate python interpreter to use to invoke esptool. (Default: "$PYTHON")
    -f FILENAME      The *update.bin file to flash.  Custom to your device type.
    
EOF
}


while getopts ":hp:P:f:" opt; do
    case "${opt}" in
        h)
            show_help
            exit 0
            ;;
        p)  export ESPTOOL_PORT=${OPTARG}
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

[ -z "$FILENAME" -a -n "$1" ] && {
    FILENAME=$1
    shift
}

if [ -f "${FILENAME}" ] && [ -z "${FILENAME##*"update"*}" ]; then
	printf "Trying to flash update ${FILENAME}"
	$PYTHON -m esptool --baud 115200 write_flash 0x10000 ${FILENAME}
else
	show_help
	echo "Invalid file: ${FILENAME}"
fi

exit 0
