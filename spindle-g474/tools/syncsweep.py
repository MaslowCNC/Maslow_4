#!/usr/bin/env python3
# syncsweep.py <host> <rise list a,b,c | start:stop:step> [noise samples] [raw log file]
#
# U9 sync clock phase sweep (BRINGUP.md "Board 2").  For each phase: "#SYNC 1 <rise>" (rising edge, timer ticks
# after the PWM peak, 0-339; the firmware re-measures the current-sense zero), then "#NOISE".  Prints, per phase,
# the sd of the six sense channels and how far each zero moved from its clock-off value (U9's ripple at the
# sample instant).  A clock-off reference is taken first and last.  Drive off, no motion.  Talks to the XY
# board's telnet port and passes the commands through with $Spindle/G474=SEND.
import re
import socket
import sys
import time

host = sys.argv[1]
spec = sys.argv[2]
if ":" in spec:
    a, b, c = (int(x) for x in spec.split(":"))
    phases = list(range(a, b, c))
else:
    phases = [int(x) for x in spec.split(",")]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
out = sys.argv[4] if len(sys.argv) > 4 else None

s = socket.create_connection((host, 23), timeout=5)
s.settimeout(0.1)
buf = []


def drain(t):
    end = time.time() + t
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d:
                return
            buf.append(d.decode("utf-8", "replace"))
        except socket.timeout:
            pass


def send(cmd, t):
    buf.append("\n>> %s\n" % cmd)
    s.sendall((cmd + "\n").encode())
    drain(t)


NO = re.compile(r"NOISE M(\d) .*?a mean ([-+\d.]+) sd ([\d.]+) .*?\| b mean ([-+\d.]+) sd ([\d.]+) .*?\| c mean ([-+\d.]+) sd ([\d.]+)")
ZE = re.compile(r"NOISE zero .*?: M1 (-?\d+) (-?\d+) (-?\d+) M2 (-?\d+) (-?\d+) (-?\d+)")
ER = re.compile(r"SYNC: (\w+).*?phase error (-?\d+) ticks")


def measure(cmd):
    mark = len("".join(buf))
    if cmd:
        send(cmd, 1.2)
    send("$Spindle/G474=SEND #NOISE %d" % n, 0.8 + n / 20000.0 + 0.9)
    seg = "".join(buf)[mark:]
    sd = {}
    for m in NO.finditer(seg):
        k = int(m.group(1)) - 1
        v = [float(x) for x in m.groups()[1:]]
        sd[3 * k], sd[3 * k + 1], sd[3 * k + 2] = v[1], v[3], v[5]
    z = ZE.search(seg)
    e = ER.search(seg)
    return {
        "sd": [sd[c] for c in range(6)] if len(sd) == 6 else None,
        "zero": [int(x) for x in z.groups()] if z else None,
        "state": e.group(1) if e else "",
        "err": int(e.group(2)) if e else None,
        "stopped": "clock stopped" in seg,
    }


names = ["M1a", "M1b", "M1c", "M2a", "M2b", "M2c"]
ref = None


def show(label, r):
    if r["sd"] is None:
        print("%-5s  (no NOISE lines came back)" % label)
        return
    line = "%-5s " % label + " ".join("%6.1f" % x for x in r["sd"]) + "  %6.1f " % (sum(x * x for x in r["sd"]) / 6) ** 0.5
    if r["zero"] and ref:
        sh = [r["zero"][c] - ref[c] for c in range(6)]
        line += " | " + " ".join("%+5d" % x for x in sh) + "  %4d" % max(abs(x) for x in sh)
    elif r["zero"]:
        line += " | zeros " + " ".join("%5d" % x for x in r["zero"])
    if r["stopped"]:
        line += "   ZERO OUT OF RANGE: the firmware stopped the clock (the sds are clock-off values)"
    elif r["err"]:
        line += "   phase error %d" % r["err"]
    print(line, flush=True)


drain(0.5)
print("rise  " + " ".join("%6s" % c for c in names) + "  rms sd  | zero shift from clock off (" + " ".join(names) + ")  max")
send("$Spindle/G474=SEND #SYNC 0", 1.2)
r = measure(None)
show("off", r)
ref = r["zero"]
for ph in phases:
    show(str(ph), measure("$Spindle/G474=SEND #SYNC 1 %d" % ph))
send("$Spindle/G474=SEND #SYNC 0", 1.2)
show("off", measure(None))
send("$Spindle/G474=SEND #SYNC 1", 1.2)   # leave the clock on at the firmware's default phase
s.shutdown(socket.SHUT_RDWR)
s.close()
if out:
    open(out, "w").write("".join(buf))
