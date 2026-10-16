"""Parse and summarise a spindle trip recording (spindle/src/trip_recorder.cpp).

The recording is ~2 s of motor state sampled every ~2 ms, ending ~100 ms after the fault that
stopped the motors.  It reaches the computer either as /spindle_trip.csv on the XY board (the
spindle sends it over the link) or as "[TRIP]" / "TRIPCSV," lines in a serial-hub log (USB).

summarise() turns it into a short report: what tripped, the run-up of each motor's current,
and the evidence for the usual causes - a real overload (current and Iq climbing over hundreds of
ms), a bad reading or a commutation step (current jumping in one sample), a stalled FOC loop, the
driver's own over-current protection (nFAULT retries), or a Z move at the wrong moment.
"""

import math
import re

# Firmware constants (spindle/src/config.h) the report compares against.
OVERCURRENT_THRESHOLD_A = 6.0  # protection_current trip point
OVERCURRENT_CONSECUTIVE = 100  # samples above it before the software trip (~200 ms)

MOTOR_FIELDS = ["en", "rpm", "vlim", "ia", "ib", "ic", "prot", "nf_edges", "nf_low", "ip", "iq", "lag"]
FIELDS = ["t_ms", "foc_loops", "foc_max_us", "z_phase_deg", "z_target_deg"] + [
    "m%d_%s" % (m, f) for m in (1, 2) for f in MOTOR_FIELDS
]


def parse(text):
    """Return (recordings, warnings).  Each recording is {'reason', 'uptime_s', 'rows'}; rows are
    dicts keyed by FIELDS.  Malformed lines (a byte dropped on the link) are skipped and counted."""
    recs, cur, bad = [], None, 0
    for line in text.splitlines():
        line = line.strip()
        # Hub logs prefix every line with a timestamp; keep only from the marker on.
        for marker in ("[TRIP]", "TRIPCSV,"):
            i = line.find(marker)
            if i > 0:
                line = line[i:]
        m = re.match(r"\[TRIP\] ===== trip recording: (.*) \(at ([\d.]+)s uptime\) =====", line)
        if m:
            cur = {"reason": m.group(1), "uptime_s": float(m.group(2)), "rows": [], "complete": False}
            recs.append(cur)
            continue
        if cur is None:
            continue
        if line.startswith("[TRIP] ===== end of trip recording"):
            cur["complete"] = True
            continue
        if not line.startswith("TRIPCSV,") or line.startswith("TRIPCSV,t_ms"):
            continue
        vals = line.split(",")[1:]
        if len(vals) != len(FIELDS):
            bad += 1
            continue
        try:
            row = {k: float(v) for k, v in zip(FIELDS, vals)}
        except ValueError:
            bad += 1
            continue
        cur["rows"].append(row)
    warnings = []
    if bad:
        warnings.append("%d malformed sample line(s) skipped (dropped bytes on the link)" % bad)
    for r in recs:
        if not r["complete"]:
            warnings.append("recording '%s' has no end marker - it was cut short" % r["reason"])
    return recs, warnings


def _nan(x):
    return x is None or (isinstance(x, float) and math.isnan(x))


def _fmt(x, nd=2):
    return "nan" if _nan(x) else ("%.*f" % (nd, x))


def _mean(xs):
    xs = [x for x in xs if not _nan(x)]
    return sum(xs) / len(xs) if xs else float("nan")


def _window(rows, t0, t1):
    return [r for r in rows if t0 <= r["t_ms"] < t1]


def _at(rows, t):
    """The sample closest to t ms."""
    return min(rows, key=lambda r: abs(r["t_ms"] - t))


def summarise(rec):
    """Plain-text diagnosis of one recording."""
    rows = rec["rows"]
    if not rows:
        return "Trip recording '%s' has no samples." % rec["reason"]
    pre = [r for r in rows if r["t_ms"] <= 0]
    out = []
    span = (rows[0]["t_ms"], rows[-1]["t_ms"])
    out.append("Trip: %s (spindle uptime %.3f s)" % (rec["reason"], rec["uptime_s"]))
    out.append("%d samples from %+d to %+d ms around the trigger%s." % (
        len(rows), span[0], span[1], "" if rec.get("complete", True) else " (incomplete)"))

    # --- FOC loop health ---------------------------------------------------------------------
    last = _window(rows, -200, 1)
    stalls = [r for r in last if r["foc_loops"] == 0]
    worst = max(last, key=lambda r: r["foc_max_us"]) if last else None
    typical_loops = _mean([r["foc_loops"] for r in _window(rows, -2000, -300)])
    out.append("")
    out.append("FOC loop (last 200 ms): typical %.0f loops/sample earlier; %d sample(s) with 0 loops; "
               "longest iteration %d us at %+d ms." % (
                   typical_loops, len(stalls), worst["foc_max_us"] if worst else 0, worst["t_ms"] if worst else 0))

    # --- Z ---------------------------------------------------------------------------------------
    # Moving = still short of its target, or changed over the last 200 ms.
    z = _at(rows, 0)
    z_before = _at(rows, -200)
    moving = abs(z["z_target_deg"] - z["z_phase_deg"]) > 0.5 or abs(z["z_phase_deg"] - z_before["z_phase_deg"]) > 0.5
    out.append("Z phase at trigger %.1f deg (target %.1f, %.1f at -200 ms)%s." % (
        z["z_phase_deg"], z["z_target_deg"], z_before["z_phase_deg"], " - Z WAS MOVING" if moving else ""))

    # --- per motor ---------------------------------------------------------------------------
    findings = []
    for m in (1, 2):
        k = lambda f: "m%d_%s" % (m, f)
        en = [r for r in pre if r[k("en")]]
        out.append("")
        if not en:
            out.append("M%d: disabled for the whole window." % m)
            continue
        t0 = _at(rows, 0)
        peak = max(pre, key=lambda r: r[k("prot")])
        phase_peak = max(pre, key=lambda r: max(abs(r[k("ia")]), abs(r[k("ib")]), abs(r[k("ic")])))
        ph_val = max(abs(phase_peak[k("ia")]), abs(phase_peak[k("ib")]), abs(phase_peak[k("ic")]))
        over = [r for r in pre if r[k("prot")] > OVERCURRENT_THRESHOLD_A]
        nf = sum(r[k("nf_edges")] for r in rows)
        nf_first = next((r["t_ms"] for r in rows if r[k("nf_edges")] > 0), None)
        nf_low = sum(1 for r in pre if r[k("nf_low")])
        out.append("M%d: %.0f RPM, vlim %.2f V at the trigger; prot %.2f A (peak %.2f A at %+d ms); "
                   "largest phase sample %.2f A at %+d ms." % (
                       m, t0[k("rpm")], t0[k("vlim")], t0[k("prot")], peak[k("prot")], peak["t_ms"],
                       ph_val, phase_peak["t_ms"]))
        # Run-up of the filtered current and the load-sense fit.
        marks = [-2000, -1000, -500, -300, -200, -100, -50, -20, -10, 0]
        marks = [t for t in marks if t >= rows[0]["t_ms"]]
        out.append("  t ms : " + " ".join("%7d" % t for t in marks))
        for f, nd in (("prot", 2), ("vlim", 2), ("iq", 2), ("lag", 0)):
            out.append("  %-5s: " % f + " ".join("%7s" % _fmt(_at(rows, t)[k(f)], nd) for t in marks))
        if nf:
            out.append("  nFAULT: %d falling edge(s), first at %+d ms; held low in %d pre-trigger sample(s)." % (
                nf, nf_first, nf_low))

        # Classify this motor's run-up.
        if over:
            first_over = over[0]["t_ms"]
            before = _window(rows, first_over - 50, first_over)
            base = _mean([r[k("prot")] for r in _window(rows, -2000, -500)])
            jump_from = before[0][k("prot")] if before else base
            out.append("  above %.0f A for %d sample(s), from %+d ms; baseline before -500 ms %.2f A." % (
                OVERCURRENT_THRESHOLD_A, len(over), first_over, base))
            if jump_from < 0.5 * OVERCURRENT_THRESHOLD_A:
                findings.append("M%d: current JUMPED (%.2f A -> over %.0f A within 50 ms at %+d ms) - points at a "
                                "commutation step (FOC stall), a bad ADC reading or a sudden mechanical jam, "
                                "not a gradual overload." % (m, jump_from, OVERCURRENT_THRESHOLD_A, first_over))
            else:
                findings.append("M%d: current CLIMBED from %.2f A to over %.0f A (crossed at %+d ms) - a real "
                                "overload: the cutter load grew until the motor could not carry it." % (
                                    m, base, OVERCURRENT_THRESHOLD_A, first_over))
        iq_early = _mean([r[k("iq")] for r in _window(rows, -2000, -500)])
        iq_late = _mean([r[k("iq")] for r in _window(rows, -100, 0)])
        lag_early = _mean([r[k("lag")] for r in _window(rows, -2000, -500)])
        lag_late = _mean([r[k("lag")] for r in _window(rows, -100, 0)])
        if not _nan(iq_early) and not _nan(iq_late):
            out.append("  Iq %.2f A -> %.2f A, lag %.0f -> %.0f deg (mean before -500 ms -> last 100 ms)." % (
                iq_early, iq_late, lag_early, lag_late))
            if iq_late < 0.5 and iq_early > 1.0:
                findings.append("M%d: Iq fell to %.2f A before the trip - the motor was at its pull-out edge "
                                "(too little voltage for the load) and slipped." % (m, iq_late))
            if not _nan(lag_early) and not _nan(lag_late) and abs(lag_late - lag_early) > 20:
                findings.append("M%d: current lag moved %.0f deg off its earlier value - load-sense saw the "
                                "stall coming." % (m, lag_late - lag_early))
        if nf and nf_first is not None and nf_first <= 0:
            findings.append("M%d: the driver's own over-current protection fired (%d nFAULT retries, first at "
                            "%+d ms)." % (m, nf, nf_first))
        if nf_low > 20 and nf == 0:
            findings.append("M%d: nFAULT held low with no retries - driver thermal shutdown." % m)

    if stalls or (worst and worst["foc_max_us"] > 2000):
        findings.append("FOC loop stalled just before the trip (%d empty sample(s), longest iteration %d us) - "
                        "the open-loop angle jumped, which spikes current. Look for flash writes, long serial "
                        "writes or WiFi on the spindle board." % (len(stalls), worst["foc_max_us"] if worst else 0))
    if moving:
        findings.append("The Z was moving when it tripped (phase offset changing) - the Z move adds load on top of "
                        "cutting.")

    out.append("")
    out.append("Findings:" if findings else "Findings: nothing stands out in the recording.")
    out += ["  - " + f for f in findings]
    return "\n".join(out)
