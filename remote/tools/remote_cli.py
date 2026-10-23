#!/usr/bin/env python3
"""Talk to the remote's serial debug console.

  remote_cli.py shot out.png        screenshot of the display as a PNG
  remote_cli.py cmd "tap 120 200"   send one console line, print output for --wait s
  remote_cli.py log                 print everything the remote prints (Ctrl-C to stop)

Needs tools/serial_hub.py running (it owns the USB port).
"""

import argparse
import struct
import sys
import time
import zlib

import socket


class Hub:
    """Line I/O over the serial hub's TCP socket (see serial_hub.py)."""

    def __init__(self, port=4568):
        try:
            self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError:
            sys.exit("serial hub not running: nohup ~/.platformio/penv/bin/python tools/serial_hub.py > logs/hub.out 2>&1 &")
        self.sock.settimeout(0.2)
        self.buf = b""

    def write(self, data):
        self.sock.sendall(data)

    def read(self, n=4096):
        if self.buf:
            data, self.buf = self.buf, b""
            return data
        try:
            return self.sock.recv(n)
        except socket.timeout:
            return b""

    def readline(self):
        while b"\n" not in self.buf:
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                return b""
            if not data:
                return b""
            self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return line + b"\n"

    def reset_input_buffer(self):
        self.buf = b""


def write_png(path, w, h, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for c in row:
            r = (c >> 11) & 0x1F
            g = (c >> 5) & 0x3F
            b = c & 0x1F
            raw += bytes(((r * 255) // 31, (g * 255) // 63, (b * 255) // 31))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def shot(s, path):
    s.reset_input_buffer()
    s.write(b"shot\n")
    w = h = None
    rows = []
    deadline = time.time() + 60
    while time.time() < deadline:
        line = s.readline().decode(errors="replace").strip()
        if not line:
            continue
        if line.startswith("SHOT "):
            _, w, h = line.split()
            w, h = int(w), int(h)
            rows = []
        elif line.startswith("R") and w is not None:
            row = []
            for tok in line[1:].split():
                c, n = tok.split("*")
                row += [int(c, 16)] * int(n, 16)
            rows.append(row[:w] + [0] * (w - len(row)))
        elif line == "END" and w is not None:
            break
    if w is None or len(rows) != h:
        sys.exit(f"incomplete screenshot ({len(rows)} rows)")
    # LovyanGFX reads back byte-swapped RGB565.
    rows = [[((c >> 8) | (c << 8)) & 0xFFFF for c in r] for r in rows]
    write_png(path, w, h, rows)
    print(f"wrote {path} ({w}x{h})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=float, default=1.0)
    ap.add_argument("action", choices=["shot", "cmd", "log"])
    ap.add_argument("arg", nargs="?")
    a = ap.parse_args()
    s = Hub()
    if a.action == "shot":
        shot(s, a.arg or "shot.png")
    elif a.action == "cmd":
        s.reset_input_buffer()
        s.write((a.arg + "\n").encode())
        end = time.time() + a.wait
        while time.time() < end:
            data = s.read(4096)
            if data:
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
    else:
        try:
            while True:
                data = s.read(4096)
                if data:
                    sys.stdout.write(data.decode(errors="replace"))
                    sys.stdout.flush()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
