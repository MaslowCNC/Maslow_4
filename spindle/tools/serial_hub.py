#!/usr/bin/env python3
"""Shared serial console for the spindle board.

One process owns the USB serial port; any number of clients share it over TCP, and everything the
board prints is logged with timestamps.  Lets several people/tools watch and type at once, and
lets the port be released for flashing without closing anyone's console.

    Start the hub (leave it running):
        ~/.platformio/penv/bin/python tools/serial_hub.py
    Console (see output, type commands + Enter), in any number of terminals:
        nc localhost 4567
    Send one command from a script:
        printf 'WHY\\n' | nc -w 1 localhost 4567
    Flash without closing anything:
        tools/flash.sh            (pauses the hub, uploads, resumes)

The hub releases the port while logs/hub.pause exists, and reopens it (also after the board
resets or is unplugged) as soon as it can.  logs/hub.status says what it is doing.
"""

import glob
import os
import socket
import sys
import threading
import time

import serial  # pyserial (bundled with PlatformIO's Python)

HERE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.path.join(os.path.dirname(HERE), "logs")
PAUSE_FILE = os.path.join(LOGDIR, "hub.pause")
STATUS_FILE = os.path.join(LOGDIR, "hub.status")
HOST, PORT, BAUD = "127.0.0.1", 4567, 115200

os.makedirs(LOGDIR, exist_ok=True)
log_path = os.path.join(LOGDIR, time.strftime("hub-%y%m%d-%H%M%S.log"))
log_file = open(log_path, "a", buffering=1)

clients = []
clients_lock = threading.Lock()
ser = None
ser_lock = threading.Lock()
at_line_start = True


def stamp():
    t = time.time()
    return time.strftime("%H:%M:%S", time.localtime(t)) + ".%03d " % int((t % 1) * 1000)


def log_text(text):
    """Append board output to the log, timestamping the start of each line."""
    global at_line_start
    out = []
    for ch in text:
        if at_line_start and ch not in "\r\n":
            out.append(stamp())
            at_line_start = False
        if ch == "\n":
            at_line_start = True
        if ch != "\r":
            out.append(ch)
    log_file.write("".join(out))


def note(msg):
    """A hub message: to the log and to every client."""
    global at_line_start
    line = "%s[hub] %s\n" % (stamp(), msg)
    if not at_line_start:
        line = "\n" + line
        at_line_start = True
    log_file.write(line)
    broadcast(("[hub] %s\n" % msg).encode())


def set_status(s):
    with open(STATUS_FILE, "w") as f:
        f.write(s + "\n")


def broadcast(data):
    with clients_lock:
        for c in list(clients):
            try:
                c.sendall(data)
            except OSError:
                clients.remove(c)


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def serial_loop():
    global ser
    state = None
    while True:
        if os.path.exists(PAUSE_FILE):
            if ser:
                with ser_lock:
                    ser.close()
                    ser = None
            if state != "paused":
                state = "paused"
                set_status("paused")
                note("paused - port released (remove logs/hub.pause to resume)")
            time.sleep(0.2)
            continue
        if ser is None:
            port = find_port()
            try:
                if not port:
                    raise OSError("no /dev/cu.usbmodem* port")
                s = serial.Serial(port, BAUD, timeout=0.1)
                with ser_lock:
                    ser = s
                state = "connected"
                set_status("connected " + port)
                note("connected to " + port)
            except (OSError, serial.SerialException):
                if state != "waiting":
                    state = "waiting"
                    set_status("waiting for board")
                    note("waiting for the board's USB port...")
                time.sleep(0.5)
                continue
        try:
            data = ser.read(4096)
        except (OSError, serial.SerialException):
            with ser_lock:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
            note("port lost (board reset or unplugged)")
            continue
        if data:
            log_text(data.decode("utf-8", errors="replace"))
            broadcast(data)


def client_loop(conn, addr):
    with clients_lock:
        clients.append(conn)
    try:
        conn.sendall(("[hub] connected; logging to %s\n" % log_path).encode())
        buf = b""
        while True:
            data = conn.recv(1024)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.rstrip(b"\r")
                log_file.write("%s>> %s\n" % (stamp(), line.decode("utf-8", errors="replace")))
                with ser_lock:
                    if ser is None:
                        conn.sendall(b"[hub] not connected to the board - command dropped\n")
                    else:
                        ser.write(line + b"\n")
    except OSError:
        pass
    finally:
        with clients_lock:
            if conn in clients:
                clients.remove(conn)
        conn.close()


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen()
    print("serial hub: console at  nc localhost %d   log: %s" % (PORT, log_path), flush=True)
    threading.Thread(target=serial_loop, daemon=True).start()
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=client_loop, args=(conn, addr), daemon=True).start()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        set_status("stopped")
