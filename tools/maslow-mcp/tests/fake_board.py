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
    clients.append(c)
    buf = b""
    try:
        while True:
            data = c.recv(1024)
            if not data:
                return
            for b in data:
                if b == ord("?"):
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
                    for out in CANNED.get(line.upper(), ["ok"]):
                        c.sendall((out + "\r\n").encode())
                else:
                    buf += bytes([b])
    finally:
        clients.remove(c)


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
