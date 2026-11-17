#!/usr/bin/env python3
"""Send FluidNC/Maslow commands to a USB-connected Maslow and print the replies.

    scripts/maslow_serial.py [--port /dev/ttyACM0] [--wait 2.5] '$SS' '$I' '$GSTATE'

Opens the port with DTR/RTS held low, so the ESP32-S3's USB-Serial/JTAG link does not
reboot the board. Each command gets a fixed read window (--wait): the firmware streams
heartbeats and status lines non-stop, so "read until quiet" would never finish.
With no commands it just listens for one window (handy right after a reset).

Needs pyserial (bundled with PlatformIO: run it with PlatformIO's Python if needed).
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--wait", type=float, default=2.5, help="seconds to read after each command")
    ap.add_argument("commands", nargs="*")
    args = ap.parse_args()

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = args.port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    try:
        time.sleep(0.3)
        if not args.commands:
            print(read_window(s, args.wait))
            return
        s.reset_input_buffer()
        for cmd in args.commands:
            print(f"===== {cmd}")
            s.write((cmd + "\n").encode())
            print(read_window(s, args.wait))
    finally:
        s.close()


def read_window(s, seconds):
    end, buf = time.time() + seconds, b""
    while time.time() < end:
        buf += s.read(4096)
    return buf.decode(errors="replace")


if __name__ == "__main__":
    sys.exit(main())
