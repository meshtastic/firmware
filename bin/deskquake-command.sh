#!/usr/bin/env bash

set -euo pipefail

DEFAULT_BAUD=115200
PORT=""
COMMAND=""

show_help() {
cat <<'EOF'
Usage: deskquake-command.sh [-p PORT] [-b BAUD] COMMAND

Send a DeskQuake console command directly to the node over serial.

Close any running serial monitor before using this helper. The serial port can
only be opened by one process at a time.

Commands:
  dqcount   Print the current DeskQuake status
  dqreset   Reset DeskQuake count, peak, and last-quake timer
    dqdfu     Reboot the node into DFU/UF2 mode
  dqhelp    Print the DeskQuake command list

Options:
  -p PORT   Serial device path. If omitted, auto-detect a likely port.
  -b BAUD   Serial baud rate. Default: 115200
  -h        Show this help

Examples:
  bin/deskquake-command.sh dqcount
  bin/deskquake-command.sh dqreset
  bin/deskquake-command.sh -p /dev/cu.usbmodem2101 dqhelp
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

configure_port() {
    local port="$1"
    local baud="$2"

    if [[ "$OSTYPE" == darwin* ]]; then
        stty -f "$port" "$baud" raw -echo clocal cread
    else
        stty -F "$port" "$baud" raw -echo clocal cread
    fi
}

while getopts ":p:b:h" opt; do
    case "$opt" in
        p)
            PORT="$OPTARG"
            ;;
        b)
            DEFAULT_BAUD="$OPTARG"
            ;;
        h)
            show_help
            exit 0
            ;;
        :)
            echo "Missing argument for -$OPTARG" >&2
            show_help >&2
            exit 1
            ;;
        *)
            echo "Unknown option: -$OPTARG" >&2
            show_help >&2
            exit 1
            ;;
    esac
done
shift "$((OPTIND - 1))"

if [[ $# -ne 1 ]]; then
    show_help >&2
    exit 1
fi

COMMAND="$1"

case "$COMMAND" in
    dqcount|dqreset|dqdfu|dqhelp)
        ;;
    *)
        echo "Unsupported DeskQuake command: $COMMAND" >&2
        show_help >&2
        exit 1
        ;;
esac

if [[ -z "$PORT" ]]; then
    if ! PORT="$(detect_port)"; then
        echo "Could not auto-detect a DeskQuake serial port. Use -p PORT." >&2
        exit 1
    fi
fi

configure_port "$PORT" "$DEFAULT_BAUD"

exec 3>"$PORT"
printf '%s\r\n' "$COMMAND" >&3
exec 3>&-

echo "Sent '$COMMAND' to $PORT at $DEFAULT_BAUD baud"