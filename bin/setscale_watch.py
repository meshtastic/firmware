#!/usr/bin/env python3
"""Send set_scale and capture the device's serial response in one session.

Usage:
    python3 setscale_watch.py /dev/ttyUSB0 0
"""
import re
import sys
import threading

import serial
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

    # Serial listener FIRST (grabs the port, sees everything that follows)
    ser = serial.Serial(port, 115200, timeout=1)
    matches = []

    def listen():
        start = __import__("time").time()
        while __import__("time").time() - start < 25:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line and ("HX711" in line or "tare" in line.lower() or "offset" in line.lower()
                         or "calibrat" in line.lower()):
                clean = re.sub(r"\x1b\[[0-9;]*m", "", line)
                matches.append(clean)

    t = threading.Thread(target=listen)
    t.start()

    # Small wait so listener is ready, then send admin message via meshtastic lib
    import time
    time.sleep(2)

    iface = SerialInterface(devPath=port, noProto=False)
    msg = admin_pb2.AdminMessage()
    msg.set_scale = value
    iface.localNode._sendAdmin(msg)
    print(f">>> Sent set_scale={value}")
    iface.close()

    t.join()
    ser.close()

    if matches:
        for m in matches:
            print(m)
    else:
        print("(no HX711-related lines captured)")


if __name__ == "__main__":
    main()
