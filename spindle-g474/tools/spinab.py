#!/usr/bin/env python3
# spinab.py <host> <rpm> <sync 0|1> <log file> [vlog seconds] [noise runs] [noise samples]
#
# One bench spin for the U9 sync clock comparison (BRINGUP.md "Board 2"): set the clock (#SYNC, drive off),
# "#SPIN <rpm> <ms>" (stops itself), "#VLOG 200" for a few seconds at speed, then "#NOISE" runs (their phase-sum
# line is the measurement error while the motors run), "#STAT", ramp down.  Motors LOOSE on the bench.
# Sends E (coast) on any failure line, |zerr| > 20 deg, vbus > 41 V, a board temperature > 70 C, or the voltage
# limit clipping more than 60 % of the periods in two VLOG lines in a row.  Prints a summary; the log file has
# everything.  Talks to the XY board's telnet port and passes commands through with $Spindle/G474=SEND.
import re
import socket
import statistics as st
import sys
import time

host, rpm, sync, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
vlog_s = float(sys.argv[5]) if len(sys.argv) > 5 else 3.0
noise_runs = int(sys.argv[6]) if len(sys.argv) > 6 else 3
noise_n = int(sys.argv[7]) if len(sys.argv) > 7 else 20000

BAD = re.compile(r"\bERR\b|ERR:|FAULT|\bfault|over-current|slipped|slip stop|refused|lost lock|fell|did not lock|WARN|aborted|"
                 r"reset reason|^boot:|Booting")
R = 2 * 3.14159265 / 60
# ramp: open loop to 2000 RPM at 250 rad/s^2, 2000..14000 at 500, above at 150 (config.h), + the lock
ramp = min(rpm, 2000) * R / 250 + max(0, min(rpm, 14000) - 2000) * R / 500 + max(0, rpm - 14000) * R / 150 + 0.6
dwell = vlog_s + noise_runs * (noise_n / 20000.0 + 0.9) + 1.5
spin_ms = min(30000, int((ramp + dwell) * 1000))

s = socket.create_connection((host, 23), timeout=5)
s.settimeout(0.05)
t0 = time.time()
buf = b""
log = []
aborted = False
done = False
at_speed = None
clip_run = 0


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
    global clip_run, done, at_speed
    if "Slow Maslow.update" in l:
        return
    if BAD.search(l) and not l.startswith(("tuning:", "NOISE", "SYNC:", ">>")):
        abort("failure line")
    m = re.search(r"zerr ([+-]?[0-9.]+)", l)
    if m and abs(float(m.group(1))) > 20:
        abort("zerr %s" % m.group(1))
    m = re.search(r"vbus ([0-9.]+)", l)
    if m and float(m.group(1)) > 41:
        abort("vbus %s" % m.group(1))
    m = re.search(r" T ([0-9.]+)/([0-9.]+) C", l)
    if m and max(float(m.group(1)), float(m.group(2))) > 70:
        abort("temperature")
    if l.startswith("VLOG"):
        sats = [int(x) for x in re.findall(r"sat (\d+)", l)]
        clip_run = clip_run + 1 if sats and max(sats) > 2400 else 0
        if clip_run >= 2:
            abort("voltage limit clipping")
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
            l = l.replace("[MSG:INFO: SpindleBoard: ", "").replace("[MSG:WARN: SpindleBoard: ", "WARN ").rstrip("]")
            note(l)
            check(l)


try:
    pump(0.3)
    send("#SYNC %d" % sync)
    pump(1.8)
    if not aborted:
        send("#STAT")
        pump(0.8)
        send("#VLOG 200")
        pump(0.3)
        send("#SPIN %d %d" % (rpm, spin_ms))
        t_end = time.time() + ramp + 4.0
        while at_speed is None and time.time() < t_end and not aborted:
            pump(0.2)
        if at_speed is None and not aborted:
            abort("never reached speed")
        if not aborted:
            pump(vlog_s)
            send("#VLOG 0")
            pump(0.3)
        for i in range(noise_runs):
            if aborted:
                break
            send("#NOISE %d" % noise_n)
            pump(noise_n / 20000.0 + 0.8)
        if not aborted:
            send("#STAT")
            pump(0.7)
            send("#VLOG 200")
            pump(0.2)
            send("#SPIN 0 100")   # ramp down now
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

# ---- summary
txt = [l[8:] for l in log]
vl = []
for l in txt:
    m = re.match(r"VLOG (\d+) rpm (\w+) vbus ([0-9.]+) lim ([0-9.]+) zerr ([+-][0-9.]+) \| (.*) \| cmp (.*) /(\d+)", l)
    if not m or int(m.group(1)) != rpm or (m.group(2) != "CL" and rpm >= 2000):
        continue
    mm = re.findall(r"M(\d) (-?\d+) dq ([+-][0-9.]+) ([+-][0-9.]+) v \S+ \S+ u ([0-9.]+)/([0-9.]+) sat (\d+) fw ([0-9.]+)", m.group(6))
    if len(mm) != 2:
        continue
    vl.append({"zerr": float(m.group(5)), "vbus": float(m.group(3)), "m": mm, "cmp": [int(x) for x in m.group(7).split()], "polls": int(m.group(8))})
print("%s: %d RPM, clock %s%s" % (out.split("/")[-1], rpm, "ON" if sync else "OFF", "  *** ABORTED ***" if aborted else ""))
if vl:
    z = [v["zerr"] for v in vl]
    print("  %d VLOG lines at speed: zerr %+.1f..%+.1f deg (sd %.1f), vbus %.1f V" % (len(vl), min(z), max(z), st.pstdev(z), st.mean(v["vbus"] for v in vl)))
    for k in (0, 1):
        iq = [float(v["m"][k][3]) for v in vl]
        idv = [float(v["m"][k][2]) for v in vl]
        u = [float(v["m"][k][4]) for v in vl]
        up = [float(v["m"][k][5]) for v in vl]
        sat = sum(int(v["m"][k][6]) for v in vl)
        fw = [float(v["m"][k][7]) for v in vl]
        ob = [abs(int(v["m"][k][1])) for v in vl]
        print("  M%d: obs %.0f rpm (sd %.0f) | id %+.2f iq %+.2f A (sd %.2f) | demand %.3f, peak %.3f, clipped %.2f %% | fw %.2f A" % (
            k + 1, st.mean(ob), st.pstdev(ob), st.mean(idv), st.mean(iq), st.pstdev(iq), st.mean(u), max(up), 100.0 * sat / (4000 * len(vl)), st.mean(fw)))
    cmp_sum = [sum(v["cmp"][i] for v in vl) for i in range(6)]
    polls = sum(v["polls"] for v in vl)
    print("  comparator highs (M1 a b c, M2 a b c) per %d main-loop reads: %s" % (polls, " ".join(str(x) for x in cmp_sum)))
ps = []
for l in txt:
    m = re.match(r"NOISE phase sum .*?: M1 mean ([+-][0-9.]+) sd ([0-9.]+) \[([+-][0-9.]+)\.\.([+-][0-9.]+)\] \| M2 mean ([+-][0-9.]+) sd ([0-9.]+) \[([+-][0-9.]+)\.\.([+-][0-9.]+)", l)
    if m:
        ps.append([float(x) for x in m.groups()])
if ps:
    print("  phase-sum error over %d x %d samples: M1 sd %s A, extremes %+.2f..%+.2f | M2 sd %s A, extremes %+.2f..%+.2f" % (
        len(ps), noise_n, "/".join("%.2f" % p[1] for p in ps), min(p[2] for p in ps), max(p[3] for p in ps),
        "/".join("%.2f" % p[5] for p in ps), min(p[6] for p in ps), max(p[7] for p in ps)))
for l in txt:
    m = re.match(r"STAT (closed|open) cmd .* isr (\d+) us", l)
    if m:
        print("  " + l[:120])
    if re.match(r"STAT up ", l) and "overruns" in l:
        last_up = l
    if re.search(r"ABORT|WARN|ERR|fault|locked|open loop below", l) and "Slow Maslow" not in l and not l.startswith("NOISE"):
        print("  " + l[:200])
try:
    print("  " + last_up[:140])
except NameError:
    pass
