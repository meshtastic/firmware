#!/usr/bin/env python3
"""Send a set_scale admin message to a Meshtastic device over serial.

Usage:
    python3 setscale.py /dev/ttyUSB0 0          -> tare (zero the scale, platform empty)
    python3 setscale.py /dev/ttyUSB0 5         -> calibrate: the load currently on the
                                                   platform is 5 kg
"""
import sys

from meshtastic.protobuf import admin_pb2
from meshtastic.serial_interface import SerialInterface


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)

    port = sys.argv[1]
    try:
        value = int(sys.argv[2])
    except ValueError:
        print(f"Error: '{sys.argv[2]}' is not an integer (weight in whole kg, e.g. 0 or 5)")
        sys.exit(1)
    if value < 0:
        print(f"Error: '{sys.argv[2]}' is negative; weight must be >= 0")
        sys.exit(1)

    iface = SerialInterface(devPath=port)

    msg = admin_pb2.AdminMessage()
    msg.set_scale = value

    iface.localNode._sendAdmin(msg)
    print(f"Sent set_scale={value} ({'tare' if value == 0 else 'calibrate to %d kg' % value})")

    iface.close()


if __name__ == "__main__":
    main()
