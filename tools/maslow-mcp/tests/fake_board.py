#!/usr/bin/env python3
"""A tiny stand-in for a FluidNC XY board, for testing maslow_mcp.py without the machine.

    python tests/fake_board.py [telnet_port] [http_port]

Telnet: answers '?' with a status report, a few $ commands with canned output, everything else
with 'ok'.  Typing 'RUN' / 'IDLE' on the telnet line switches the reported state.  Every 2 s it
broadcasts a [MSG:INFO: ...] line, like Maslow's log output.
HTTP: /files list, file download, /files and /updatefw uploads.
"""

import http.server
import json
import socket
import sys
import threading
import time

TELNET_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 2323
HTTP_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
state = {"s": "Idle"}
clients = []

CANNED = {
    "$CD": ["board: Maslow S3 Board", "name: Maslow", "maslow:", "  trX: 3000.0", "  trY: 2000.0", "ok"],
    "$S": ["$Maslow/test=1", "$HTTP/Port=80", "$Telnet/Enable=ON", "ok"],
    "[ESP800]": ["FW version: FluidNC v3.9.9 (Maslow-Mini-a9c07eea) # webcommunication: Sync: 81", "ok"],
}


def handle(c):
    state["s"] = "Idle"  # each test session starts with an idle machine
    clients.append(c)
    connected_at = time.time()
    buf = b""
    try:
        while True:
            data = c.recv(1024)
            if not data:
                return
            for b in data:
                if b == ord("?") and time.time() - connected_at < 2.0:
                    buf += bytes([b])  # like the firmware: no realtime characters for 2 s after connecting
                elif b == ord("?"):
                    c.sendall(("<%s|MPos:1.000,2.000,0.000|FS:0,0>\r\n" % state["s"]).encode())
                elif b in (ord("!"), ord("~"), 0x18):
                    c.sendall(("[MSG:INFO: realtime %r]\r\n" % chr(b)).encode())
                elif b == ord("\n"):
                    line = buf.decode().strip()
                    buf = b""
                    if line == "RUN":
                        state["s"] = "Run"
                    elif line == "IDLE":
                        state["s"] = "Idle"
                    if line == "[ESP810]":
                        threading.Thread(target=spindle_ota, daemon=True).start()
                    if line.upper() == "$SPINDLE/TRIP=TEST":
                        threading.Thread(target=spindle_trip, daemon=True).start()
                    if line == "TRIPSTALE":
                        threading.Thread(target=spindle_trip_deferred, daemon=True).start()
                    for out in CANNED.get(line.upper(), ["ok"]):
                        c.sendall((out + "\r\n").encode())
                else:
                    buf += bytes([b])
    finally:
        clients.remove(c)


def broadcast(text):
    for c in list(clients):
        try:
            c.sendall((text + "\r\n").encode())
        except OSError:
            pass


def spindle_ota():
    """Mimic the XY board relaying the spindle's OTA lifecycle: an address, a retry address
    (as after a failed transfer and reboot), then the link coming back."""
    ip = "[MSG:INFO: SpindleBoard: spindle board is on WiFi at 127.0.0.%d (also maslow-spindle.local). Reflash it with: pio run -e esp32-s3-devkitc-1-ota -t upload]"
    time.sleep(1)
    broadcast(ip % 2)
    time.sleep(2)
    broadcast(ip % 3)
    time.sleep(2)
    broadcast("[MSG:INFO: SpindleBoard: link to spindle board confirmed -> H:spindle]")


TRIP = {"csv": None}


def trip_csv():
    """A recording of M2 stalling: its current climbs over the last 600 ms while Iq falls."""
    rows = ["[TRIP] ===== trip recording: over-current on M2 (6.3A) (at 812.345s uptime) =====",
            "[TRIP] 1000 samples, t_ms is relative to the trigger.  TRIP reprints, REARM clears.",
            "TRIPCSV,t_ms,foc_loops,..."]
    for i in range(1000):
        t = -1900 + 2 * i
        load = max(0.0, (t + 600) / 600)
        p2 = 2.5 + 4 * load if t <= 0 else 0.2
        vals = [t, 40, 60, 10.0, 10.0,
                1, 12000, 18.0, 1.0, -0.5, -0.5, 2.3, 0, 0, 1.0, 2.0, 67,
                1 if t <= 0 else 0, 12000, 19.5 + load, p2, -p2 / 2, -p2 / 2, p2, 0, 0, 1.2, 2.0 - 1.8 * load, 67 + 40 * load]
        rows.append("TRIPCSV," + ",".join("%g" % v for v in vals))
    rows.append("[TRIP] ===== end of trip recording =====")
    return "\n".join(rows) + "\n"


def spindle_trip():
    """Mimic $Spindle/Trip=TEST as a trip during a cut: the spindle's fault message, the recording
    skipped while the machine stops, then re-sent and saved."""
    time.sleep(0.5)
    broadcast("[MSG:WARN: SpindleBoard: over-current on M2 (6.3A) - spindle stopped; re-home the Z, then send a new speed]")
    broadcast("[MSG:INFO: SpindleBoard: spindle trip recording arriving while the machine is moving; "
              "it will be fetched again once the machine is at rest]")
    time.sleep(1.5)
    broadcast("[MSG:INFO: SpindleBoard: asked the spindle board to re-send its trip recording; "
              "it is saved to /spindle_trip.csv (~20 s)]")
    broadcast("[MSG:INFO: SpindleBoard: receiving spindle trip recording: over-current on M2 (6.3A) (at 812.345s uptime)]")
    time.sleep(1)
    TRIP["csv"] = trip_csv()
    broadcast("[MSG:INFO: SpindleBoard: spindle trip recording saved to /spindle_trip.csv (1004 lines)]")


def spindle_trip_deferred():
    """A new trip while a job runs: the job pauses in Hold, so the recording is not saved and
    /spindle_trip.csv keeps the previous one."""
    time.sleep(0.3)
    broadcast("[MSG:WARN: SpindleBoard: driver over-current (nFAULT retries) - spindle stopped; re-home the Z, then send a new speed]")
    broadcast("[MSG:ERR: SpindleBoard reported fault 2]")
    broadcast("[MSG:INFO: SpindleBoard: spindle trip recording arriving while the machine is moving; "
              "it will be fetched again once the machine is at rest]")


def telnet():
    srv = socket.create_server(("127.0.0.1", TELNET_PORT))
    while True:
        c, _ = srv.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


def chatter():
    n = 0
    while True:
        time.sleep(2)
        n += 1
        for c in list(clients):
            try:
                c.sendall(("[MSG:INFO: tick %d]\r\n" % n).encode())
            except OSError:
                pass


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="text/plain"):
        body = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/files"):
            self._send(200, json.dumps({"files": [{"name": "config.yaml", "size": "42"}], "path": "/", "status": "Ok"}), "application/json")
        elif self.path == "/config.yaml":
            self._send(200, "board: Maslow S3 Board\nname: Maslow\n")
        elif self.path == "/spindle_trip.csv" and TRIP["csv"]:
            self._send(200, TRIP["csv"])
        else:
            self._send(404, "not found")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        ok = b"S\"" in body or b"S\r\n" in body  # crude check that a size field was sent
        if self.path == "/updatefw":
            self._send(200, json.dumps({"status": "3" if ok else "4"}), "application/json")
        else:
            self._send(200, json.dumps({"status": "Ok" if ok else "missing size"}), "application/json")


threading.Thread(target=telnet, daemon=True).start()
threading.Thread(target=chatter, daemon=True).start()
http.server.ThreadingHTTPServer(("127.0.0.1", HTTP_PORT), H).serve_forever()
