#!/usr/bin/env python3
# ripple.py <host> <rpm> <log file> <step> [<step> ...]
#
# One bench spin (motors LOOSE) that stays at <rpm> while it works through the steps, for finding what makes the
# motors run rough (BRINGUP.md).  A step is:
#   base                 measure only
#   "#DTC 150"           send the command(s) (';' between several), let it settle, then measure
#   scope1 / scope2      "#SCOPE" capture of motor 1 / 2 (tools/scope.py reads it from the log)
# A measurement is one "#NOISE 20000" (1 s): per-phase DC current, the phase-sum error and the ripple of the angle
# error, the observer speed and the torque command.  Pick a speed that is NOT a multiple of 300 RPM, so the 500 ms
# #VLOG lines do not land on the same rotor angle every time.
# Sends E (coast) on any failure line, |zerr| > 25 deg, vbus > 41 V or a board temperature > 70 C.
import math
import re
import socket
import sys
import time

host, rpm, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
steps = sys.argv[4:]
BAD = re.compile(r"\bERR\b|ERR:|FAULT|\bfault|over-current|slipped|slip stop|refused|lost lock|fell|did not lock|WARN|aborted|"
                 r"reset reason|^boot:|Booting")
R = 2 * math.pi / 60
ramp = min(rpm, 2000) * R / 250 + max(0, min(rpm, 14000) - 2000) * R / 500 + max(0, rpm - 14000) * R / 150 + 0.6
G = None   # per-phase gains, read with #GAIN

s = socket.create_connection((host, 23), timeout=5)
s.settimeout(0.05)
t0 = time.time()
buf = b""
log = []
aborted = False
done = False
at_speed = None
scope_done = False


def note(l):
    log.append("%7.2f %s" % (time.time() - t0, l))


def send(c):
    s.sendall(("$Spindle/G474=SEND " + c + "\n").encode())
    note(">> " + c)


def abort(why):
    global aborted
    if not aborted:
        aborted = True
        send("E")
        note("*** ABORT (%s): E sent ***" % why)


def check(l):
    global done, at_speed, scope_done
    if "Slow Maslow.update" in l:
        return
    if l.startswith("SCOPE done"):
        scope_done = True
    if BAD.search(l) and not l.startswith(("tuning:", "NOISE", "SYNC:", ">>", "X ", "SCOPE", "GAIN")):
        abort("failure line")
    m = re.search(r"zerr ([+-]?[0-9.]+)", l)
    if m and abs(float(m.group(1))) > 25:
        abort("zerr %s" % m.group(1))
    m = re.search(r"vbus ([0-9.]+)", l)
    if m and float(m.group(1)) > 41:
        abort("vbus %s" % m.group(1))
    m = re.search(r" T ([0-9.]+)/([0-9.]+) C", l)
    if m and max(float(m.group(1)), float(m.group(2))) > 70:
        abort("temperature")
    m = re.match(r"VLOG (\d+) rpm (\w+)", l)
    if m and int(m.group(1)) == rpm and at_speed is None and (m.group(2) == "CL" or rpm < 2000):
        at_speed = time.time()
    if "state:" in l and "-> Needs Homing" in l:
        done = True


def pump(t):
    global buf
    end = time.time() + t
    while time.time() < end:
        try:
            b = s.recv(65536)
            if b:
                buf += b
        except socket.timeout:
            pass
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            l = line.decode("utf-8", "replace").rstrip("\r")
            if l.startswith("<") or l == "ok" or not l:
                continue
            l = l.replace("[MSG:INFO: SpindleBoard: ", "").replace("[MSG:WARN: SpindleBoard: ", "WARN ")
            if l.endswith("]"):
                l = l[:-1]
            note(l)
            check(l)


NO = re.compile(r"NOISE M(\d) .*?: a mean (\S+) sd (\S+) .*?\| b mean (\S+) sd (\S+) .*?\| c mean (\S+) sd (\S+)")
PS = re.compile(r"NOISE phase sum .*?: M1 mean (\S+) sd (\S+) .*?\| M2 mean (\S+) sd (\S+)")
RP = re.compile(r"NOISE ripple M(\d): angle err (\S+) sd (\S+) \[(\S+?)\.\.(\S+?)\] deg \| speed est sd (\S+) \[(\S+?)\.\.(\S+?)\] rpm \| "
                r"iq_ref (\S+) sd (\S+) \[(\S+?)\.\.(\S+?)\] \| iq sd (\S+) \| id (\S+) sd (\S+) A")
A_PER = 0.0104
rows = []


def measure(label):
    send("#STAT")          # restarts the "longest interrupt" window
    pump(0.5)
    mark = len(log)
    send("#NOISE 20000")
    pump(1.9)
    send("#STAT")
    pump(0.5)
    seg = [l[8:] for l in log[mark:]]
    row = {"label": label}
    for l in seg:
        m = re.match(r"STAT (closed|open) cmd .* isr (\d+) us", l)
        if m:
            row["isr"] = int(m.group(2))
    for l in seg:
        m = NO.match(l)
        if m:
            k = int(m.group(1)) - 1
            dc = [-float(m.group(2 + 2 * p)) * A_PER * G[k][p] for p in range(3)]
            # DC current vector (alpha, beta) magnitude
            al = (2 * dc[0] - dc[1] - dc[2]) / 3.0
            be = (dc[1] - dc[2]) / math.sqrt(3)
            row["dc%d" % k] = math.hypot(al, be)
            row["dcabc%d" % k] = dc
        m = PS.match(l)
        if m:
            row["ps0"], row["ps1"] = float(m.group(2)), float(m.group(4))
        m = RP.match(l)
        if m:
            k = int(m.group(1)) - 1
            g = [float(x) for x in m.groups()[1:]]
            row["eps_sd%d" % k], row["eps_pp%d" % k] = g[1], g[3] - g[2]
            row["w_sd%d" % k], row["w_pp%d" % k] = g[4], g[6] - g[5]
            row["iqr_sd%d" % k], row["iqr_pp%d" % k] = g[8], g[10] - g[9]
            row["iq_sd%d" % k], row["id%d" % k], row["id_sd%d" % k] = g[11], g[12], g[13]
    rows.append(row)


try:
    pump(0.3)
    send("#GAIN")
    pump(1.0)
    g = [[1.0] * 3, [1.0] * 3]
    for l in log:
        m = re.search(r"GAIN M(\d) a (\S+) b (\S+) c (\S+)", l)
        if m:
            g[int(m.group(1)) - 1] = [float(m.group(i)) for i in (2, 3, 4)]
    G = g
    send("#STAT")
    pump(0.8)
    send("#VLOG 500")
    pump(0.2)
    send("#SPIN %d 30000" % rpm)
    spin_end = time.time() + 30.0
    t_end = time.time() + ramp + 4.0
    while at_speed is None and time.time() < t_end and not aborted:
        pump(0.2)
    if at_speed is None and not aborted:
        abort("never reached speed")
    pump(1.0)
    for st in steps:
        if aborted or done:   # done here = the drive stopped by itself: never restart it from a script
            if done and not aborted:
                abort("the drive stopped")
            break
        if spin_end - time.time() < 12.0:
            send("#SPIN %d 30000" % rpm)   # stay at speed
            spin_end = time.time() + 30.0
            pump(0.2)
        if st in ("scope1", "scope2"):
            scope_done = False
            send("#SCOPE %s" % st[-1])
            t_end = time.time() + 10.0
            while not scope_done and time.time() < t_end and not aborted:
                pump(0.2)
            continue
        if st != "base":
            for c in st.split(";"):
                send(c.strip())
                pump(0.25)
            pump(0.8)
        measure(st)
    if not aborted:
        send("#STAT")
        pump(0.7)
        send("#SPIN 0 100")
    t_end = time.time() + ramp + 8.0
    while not done and time.time() < t_end:
        pump(0.3)
    send("#VLOG 0")
    pump(0.3)
    send("#STAT")
    pump(1.0)
finally:
    try:
        send("S0")
        pump(0.3)
    except Exception:
        pass
    s.shutdown(socket.SHUT_RDWR)
    s.close()
open(out, "w").write("\n".join(log) + "\n")

print("%s: %d RPM%s" % (out.split("/")[-1], rpm, "  *** ABORTED ***" if aborted else ""))
print("  %-26s | DC current A | angle err sd / p-p deg | speed est sd / p-p rpm | iq_ref sd / p-p A |  iq sd A  | id mean A  | phase-sum sd A" % "step")
print("  %-26s |   M1    M2   |    M1          M2      |     M1          M2     |    M1        M2    |  M1   M2  |  M1    M2  |  M1    M2" % "")
for r in rows:
    need = ["dc0", "dc1", "ps0"] + ["%s%d" % (a, k) for a in ("eps_sd", "eps_pp", "w_sd", "w_pp", "iqr_sd", "iqr_pp", "iq_sd", "id") for k in (0, 1)]
    if any(x not in r for x in need):
        print("  %-26s | (a #NOISE line did not come back)" % r["label"][:26])
        continue
    print("  %-26s | %5.2f %5.2f  | %4.1f/%5.1f  %4.1f/%5.1f  | %4.0f/%5.0f  %4.0f/%5.0f  | %4.2f/%4.1f  %4.2f/%4.1f | %4.2f %4.2f | %+5.2f %+5.2f | %4.2f  %4.2f | isr %s us" % (
        r["label"][:26], r["dc0"], r["dc1"], r["eps_sd0"], r["eps_pp0"], r["eps_sd1"], r["eps_pp1"], r["w_sd0"], r["w_pp0"], r["w_sd1"], r["w_pp1"],
        r["iqr_sd0"], r["iqr_pp0"], r["iqr_sd1"], r["iqr_pp1"], r["iq_sd0"], r["iq_sd1"], r["id0"], r["id1"], r.get("ps0", 0), r.get("ps1", 0), r.get("isr", "?")))
for l in log:
    t = l[8:]
    if re.search(r"ABORT|WARN|ERR|fault|locked|open loop below", t) and "Slow Maslow" not in t and not t.startswith(("NOISE", ">>", "X ")):
        print("  " + t[:200])
last = [l[8:] for l in log if "STAT up" in l]
if last:
    print("  " + last[-1][:140])
