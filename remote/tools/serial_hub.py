#!/usr/bin/env python3
"""Own the remote's USB serial port and share it on localhost:4568.

Opening the CYD's CH340 port resets the ESP32, so tools connect here instead
of opening the port themselves.  Every client sees everything the remote
prints; whatever a client sends goes to the remote.  All output is logged with
timestamps to remote/logs/hub-<date>.log.

Pause for flashing: create logs/hub.pause (the hub closes the port) and remove
it afterwards (the hub reopens it).  tools/flash.sh does this.

  nohup ~/.platformio/penv/bin/python tools/serial_hub.py > logs/hub.out 2>&1 &
"""

import glob
import os
import selectors
import socket
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.path.join(HERE, "..", "logs")
PAUSE = os.path.join(LOGDIR, "hub.pause")
PORT = 4568


def find_port():
    ports = glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/ttyUSB*")
    return ports[0] if ports else None


def main():
    os.makedirs(LOGDIR, exist_ok=True)
    log = open(os.path.join(LOGDIR, time.strftime("hub-%Y%m%d.log")), "a", buffering=1)
    sel = selectors.DefaultSelector()
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen()
    srv.setblocking(False)
    sel.register(srv, selectors.EVENT_READ, "srv")
    clients = []
    ser = None
    partial = b""

    while True:
        if os.path.exists(PAUSE):
            if ser:
                sel.unregister(ser)
                ser.close()
                ser = None
                log.write(time.strftime("%H:%M:%S ") + "-- paused --\n")
        elif ser is None:
            path = find_port()
            if path:
                try:
                    ser = serial.Serial(path, 115200, timeout=0)
                    sel.register(ser, selectors.EVENT_READ, "ser")
                    log.write(time.strftime("%H:%M:%S ") + f"-- opened {path} --\n")
                except Exception as e:
                    ser = None
                    time.sleep(1)
        for key, _ in sel.select(timeout=0.2):
            if key.data == "srv":
                c, _ = srv.accept()
                c.setblocking(False)
                sel.register(c, selectors.EVENT_READ, "client")
                clients.append(c)
            elif key.data == "ser":
                try:
                    data = ser.read(8192)
                except Exception:
                    sel.unregister(ser)
                    ser = None
                    continue
                for c in list(clients):
                    try:
                        c.sendall(data)
                    except Exception:
                        sel.unregister(c)
                        clients.remove(c)
                partial += data
                *lines, partial = partial.split(b"\n")
                for line in lines:
                    text = line.decode(errors="replace").rstrip("\r")
                    # Screenshot rows are bulky; keep only the markers.
                    if text.startswith("R ") or text.startswith("R\t"):
                        continue
                    log.write(time.strftime("%H:%M:%S ") + text + "\n")
            else:
                c = key.fileobj
                try:
                    data = c.recv(4096)
                except Exception:
                    data = b""
                if not data:
                    sel.unregister(c)
                    clients.remove(c)
                    c.close()
                    continue
                if ser:
                    ser.write(data)
                    for line in data.decode(errors="replace").splitlines():
                        log.write(time.strftime("%H:%M:%S ") + ">> " + line + "\n")


if __name__ == "__main__":
    main()
