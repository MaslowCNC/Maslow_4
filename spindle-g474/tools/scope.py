#!/usr/bin/env python3
# scope.py <log from ripple.py> [capture index, default 0] [csv out]
#
# Decodes a "#SCOPE" capture (1024 samples of one motor) and says how much of each signal repeats once, twice,
# three and six times per electrical turn (= per mechanical turn: one pole pair), and where the observer's flux
# circle sits.  Once per turn = something constant in the stator frame (a DC current or flux offset); twice =
# unequal phases; three times = the two-shunt rebuild's sectors; six times = dead time.
# It also splits the current (id + j iq) into parts that turn in the control frame.  The part turning backward twice
# per turn is a backward-turning (negative-sequence) stator current: unequal phases, what "#CUR2" cancels.
import cmath
import math
import re
import sys

log = sys.argv[1]
want = int(sys.argv[2]) if len(sys.argv) > 2 else 0
csv = sys.argv[3] if len(sys.argv) > 3 else None
caps = []
cur = None
for l in open(log, errors="replace"):
    t = l[8:].rstrip("\n")
    m = re.match(r"SCOPE: motor (\d), 1024 samples, every (\d+) \(([0-9.]+) ms\), flux ([0-9.]+) mWb", t)
    if m:
        cur = {"motor": int(m.group(1)), "decim": int(m.group(2)), "flux": float(m.group(4)), "ch": [dict() for _ in range(8)]}
        caps.append(cur)
        continue
    m = re.match(r"X (\d) (\d+) ([0-9a-f]+)", t)
    if m and cur is not None:
        ch, first, hx = int(m.group(1)), int(m.group(2)), m.group(3)
        for i in range(len(hx) // 4):
            v = int(hx[4 * i:4 * i + 4], 16)
            cur["ch"][ch][first + i] = v - 65536 if v >= 32768 else v
if want >= len(caps):
    sys.exit("no capture %d in %s (%d found)" % (want, log, len(caps)))
c = caps[want]
idx = [i for i in range(1024) if all(i in c["ch"][k] for k in range(8))]
if len(idx) < 300:
    sys.exit("capture %d is incomplete: %d samples in all channels" % (want, len(idx)))
dt = c["decim"] / 20000.0
fa = [c["ch"][0][i] / 10000.0 for i in idx]
fb = [c["ch"][1][i] / 10000.0 for i in idx]
sig = {
    "angle error deg": [c["ch"][2][i] / 100.0 for i in idx],
    "speed estimate rpm": [abs(c["ch"][3][i]) * 1.0 for i in idx],
    "iq_ref A": [c["ch"][4][i] / 1000.0 for i in idx],
    "iq A": [c["ch"][5][i] / 1000.0 for i in idx],
    "id A": [c["ch"][6][i] / 1000.0 for i in idx],
    "flux magnitude / nominal": [math.hypot(a, b) for a, b in zip(fa, fb)],
}
# The observer's own angle (atan2 of the flux) against the angle the control uses (reference - angle error): what the
# tracking filter keeps out of the control.  The reference is sampled one interrupt after the flux, so the mean
# holds one sample of rotation; only the ripple means something.
ref_deg = [c["ch"][7][i] / 100.0 for i in idx]
wob = []
for k in range(len(idx)):
    d = math.degrees(math.atan2(fb[k], fa[k])) - (ref_deg[k] - sig["angle error deg"][k])
    wob.append((d + 180.0) % 360.0 - 180.0)
m0 = sorted(wob)[len(wob) // 2]
sig["observer - control angle deg"] = [((w - m0 + 180.0) % 360.0 - 180.0) for w in wob]
# rotor phase: the reference angle, unwrapped
ref = [c["ch"][7][i] / 100.0 * math.pi / 180 for i in idx]
th = [ref[0]]
for k in range(1, len(ref)):
    d = ref[k] - ref[k - 1]
    d -= 2 * math.pi * round(d / (2 * math.pi))
    # the capture may have gaps (lost lines): step by the sample index
    th.append(th[-1] + d)
turns = abs(th[-1] - th[0]) / (2 * math.pi)
rpm = turns / ((idx[-1] - idx[0]) * dt) * 60.0
H = (1, 2, 3, 4, 6)


def solve(Am, bv):
    n = len(bv)
    M = [row[:] + [bv[i]] for i, row in enumerate(Am)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(M[r][i]))
        M[i], M[p] = M[p], M[i]
        for r in range(n):
            if r != i and M[i][i] != 0:
                f = M[r][i] / M[i][i]
                for cc in range(i, n + 1):
                    M[r][cc] -= f * M[i][cc]
    return [M[i][n] / M[i][i] if M[i][i] else 0.0 for i in range(n)]


def harmonics(x):
    cols = [[1.0] * len(x)]
    for h in H:
        cols.append([math.cos(h * t) for t in th])
        cols.append([math.sin(h * t) for t in th])
    n = len(cols)
    Am = [[sum(a * b for a, b in zip(cols[i], cols[j])) for j in range(n)] for i in range(n)]
    bv = [sum(a * b for a, b in zip(cols[i], x)) for i in range(n)]
    co = solve(Am, bv)
    amp = [math.hypot(co[1 + 2 * k], co[2 + 2 * k]) for k in range(len(H))]
    fit = [sum(co[j] * cols[j][i] for j in range(n)) for i in range(len(x))]
    res = math.sqrt(sum((a - b) ** 2 for a, b in zip(x, fit)) / len(x))
    mean = sum(x) / len(x)
    sd = math.sqrt(sum((a - mean) ** 2 for a in x) / len(x))
    return co[0], sd, amp, res


print("%s capture %d: motor %d, %d samples over %.1f ms = %.2f turns at %.0f RPM, flux constant %.2f mWb" % (
    log.split("/")[-1], want, c["motor"], len(idx), (idx[-1] - idx[0]) * dt * 1000, turns, rpm, c["flux"]))
print("  %-28s %9s %8s | amplitude repeating per turn: %8s %8s %8s %8s %8s | rest (rms)" % ("signal", "mean", "sd", "1x", "2x", "3x", "4x", "6x"))
for name, x in sig.items():
    mean, sd, amp, res = harmonics(x)
    print("  %-28s %9.3f %8.3f |                               %8.3f %8.3f %8.3f %8.3f %8.3f | %8.3f" % (name, mean, sd, amp[0], amp[1], amp[2], amp[3], amp[4], res))


def rotating(xd, xq, ang):
    # id + j iq = the sum over k of part[k] x exp(j k ang): least squares on both axes, then cos / sin to +k / -k
    hs = (1, 2, 3)
    cols = [[1.0] * len(ang)]
    for h in hs:
        cols.append([math.cos(h * t) for t in ang])
        cols.append([math.sin(h * t) for t in ang])
    n = len(cols)
    Am = [[sum(a * b for a, b in zip(cols[i], cols[j])) for j in range(n)] for i in range(n)]
    cd = solve(Am, [sum(a * b for a, b in zip(cols[i], xd)) for i in range(n)])
    cq = solve(Am, [sum(a * b for a, b in zip(cols[i], xq)) for i in range(n)])
    part = {0: complex(cd[0], cq[0])}
    for k, h in enumerate(hs):
        A, B = complex(cd[1 + 2 * k], cq[1 + 2 * k]), complex(cd[2 + 2 * k], cq[2 + 2 * k])
        part[h], part[-h] = (A - 1j * B) / 2, (A + 1j * B) / 2
    return part


# the control angle = reference - angle error (the angle error is sampled one interrupt earlier: it moves slowly)
thc = [t - math.radians(e) for t, e in zip(th, sig["angle error deg"])]
part = rotating(sig["id A"], sig["iq A"], thc)
pa = lambda z: "%.3f at %+4.0f" % (abs(z), math.degrees(cmath.phase(z)))
print("  current's parts turning in the control frame (A at deg): steady %s | once per turn: backward %s, forward %s" % (pa(part[0]), pa(part[-1]), pa(part[1])))
print("      twice: backward %s, forward %s | three times: backward %s, forward %s" % (pa(part[-2]), pa(part[2]), pa(part[-3]), pa(part[3])))
# The flux "#CUR2" would have to add to cancel the backward twice-per-turn part, by the loop model (foc.cpp cur2Gain)
# with the default R, L and current-loop gains: volts per amp at -2 we, over we.
R_, L_, KP_, KI_, TAU_ = 0.28, 75e-6, 0.2, 1000.0, 1.5 / 20000.0
we = sum(c["ch"][3][i] for i in idx) / len(idx) * math.pi / 30.0   # signed, this motor's own frame
if abs(we) > 100:
    lam = -part[-2] * (cmath.exp(-2j * we * TAU_) * (R_ - 1j * we * L_) - 1j * we * L_ + KP_ + 1j * KI_ / (2 * we)) / we
    print("      the twice-per-turn compensation is short by %.2f %% of the flux constant at %+.0f deg (loop model, default R, L, gains;"
          " \"#CUR2\" prints what the drive has learned)" % (abs(lam) / (c["flux"] * 1e-3) * 100.0, math.degrees(cmath.phase(lam))))
ca, cb = harmonics(fa)[0], harmonics(fb)[0]   # fitted with the harmonics: a part turn at the end does not shift it
rad = [math.hypot(a - ca, b - cb) for a, b in zip(fa, fb)]
big = max(max(abs(a), abs(b)) for a, b in zip(fa, fb))
if big > 1.0005:
    lim = "re-centring on (no clamp at the flux constant; largest component %.3f)" % big
else:
    lim = "%.1f %% of the samples sit on the clamp" % (100.0 * sum(1 for a, b in zip(fa, fb) if abs(a) >= 0.9995 or abs(b) >= 0.9995) / len(fa))
print("  flux circle: centre (%+.3f, %+.3f) of the flux constant, radius %.3f (min %.3f, max %.3f); %s" % (
    ca, cb, sum(rad) / len(rad), min(rad), max(rad), lim))
if csv:
    with open(csv, "w") as f:
        f.write("t_ms,turns,flux_a,flux_b,angle_err_deg,speed_rpm,iq_ref_A,iq_A,id_A\n")
        for k, i in enumerate(idx):
            f.write("%.3f,%.4f,%.4f,%.4f,%.2f,%.0f,%.3f,%.3f,%.3f\n" % (
                (i - idx[0]) * dt * 1000, abs(th[k] - th[0]) / (2 * math.pi), fa[k], fb[k], sig["angle error deg"][k], sig["speed estimate rpm"][k],
                sig["iq_ref A"][k], sig["iq A"][k], sig["id A"][k]))
