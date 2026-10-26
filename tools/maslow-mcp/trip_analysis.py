"""Parse and summarise a spindle trip recording (spindle/src/trip_recorder.cpp).

The recording is ~2 s of motor state sampled every ~2 ms, ending ~100 ms after the fault that
stopped the motors.  It reaches the computer either as /spindle_trip.csv on the XY board (the
spindle sends it over the link) or as "[TRIP]" / "TRIPCSV," lines in a serial-hub log (USB).

summarise() turns it into a short report: what tripped, which motor showed trouble first, the
run-up of each motor's current up to its first event (Iq and lag read while nFAULT has the driver
off are ignored), and the evidence for the usual causes - a real overload (current and Iq climbing over hundreds of
ms), a bad reading or a commutation step (current jumping in one sample), a stalled FOC loop, the
driver's own over-current protection (nFAULT retries), or a Z move at the wrong moment.

Recordings from the sync-sense build also carry the shadow flux observer's view of each motor
(m<n>_ok, _ld, _ldmx, _orpm, _slip) and how many PWM-synchronised samples were valid each pass
(_sn, _sf, _slo, _soff); the report then says whether the observer saw the trouble coming.
"""

import math
import re

# Firmware constants (spindle/src/config.h) the report compares against.
OVERCURRENT_THRESHOLD_A = 6.0  # protection_current trip point
OVERCURRENT_CONSECUTIVE = 100  # samples above it before the software trip (~200 ms)
SYNC_LOAD_WARN_DEG = 70.0      # observer load angle logged as approaching pull-out

MOTOR_FIELDS = ["en", "rpm", "vlim", "ia", "ib", "ic", "prot", "nf_edges", "nf_low", "ip", "iq", "lag"]
FIELDS = ["t_ms", "foc_loops", "foc_max_us", "z_phase_deg", "z_target_deg"] + [
    "m%d_%s" % (m, f) for m in (1, 2) for f in MOTOR_FIELDS
]


def _header_fields(hdr):
    """The column names in a CSV header, or None (use FIELDS by position) if it does not name them
    all - a header damaged on the link, or a placeholder."""
    names = hdr.split(",")
    return names if set(FIELDS) <= set(names) else None


def parse(text):
    """Return (recordings, warnings).  Each recording is {'reason', 'uptime_s', 'rows'}; rows are
    dicts keyed by the recording's own CSV header (FIELDS if it has none, as older ones may not).
    Malformed lines (a byte dropped on the link) are skipped and counted."""
    recs, cur, bad = [], None, 0
    pending = None  # first half of a header the USB console split in two
    for line in text.splitlines():
        line = line.strip()
        if pending is not None:
            # The USB copy sends the header's second half (from m2_en) as a line of its own.
            m = re.search(r"\bm2_en,\S*$", line)
            if m:
                cur["fields"] = _header_fields(pending + m.group(0))
                pending = None
                continue
            pending = None
        # Hub logs prefix every line with a timestamp; keep only from the marker on.
        for marker in ("[TRIP]", "TRIPCSV,"):
            i = line.find(marker)
            if i > 0:
                line = line[i:]
        m = re.match(r"\[TRIP\] ===== trip recording: (.*) \(at ([\d.]+)s uptime\) =====", line)
        if m:
            cur = {"reason": m.group(1), "uptime_s": float(m.group(2)), "rows": [], "complete": False,
                   "fields": None}
            recs.append(cur)
            continue
        if cur is None:
            continue
        if line.startswith("[TRIP] ===== end of trip recording"):
            cur["complete"] = True
            continue
        if line.startswith("TRIPCSV,t_ms"):
            hdr = line[len("TRIPCSV,"):]
            if hdr.endswith(","):
                pending = hdr
            else:
                cur["fields"] = _header_fields(hdr)
            continue
        if not line.startswith("TRIPCSV,"):
            continue
        fields = cur["fields"] or FIELDS
        vals = line.split(",")[1:]
        if len(vals) != len(fields):
            bad += 1
            continue
        try:
            row = {k: float(v) for k, v in zip(fields, vals)}
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


def _phase_max(r, m):
    return max(abs(r["m%d_ia" % m]), abs(r["m%d_ib" % m]), abs(r["m%d_ic" % m]))


def _in_fault(r, m):
    return r["m%d_nf_edges" % m] > 0 or r["m%d_nf_low" % m] > 0


def _events(pre, m):
    """The first time each sign of trouble shows on motor m, in time order: a phase sample over the
    trip current, nFAULT, the filtered protection current over the trip current."""
    k = lambda f: "m%d_%s" % (m, f)
    ev = []
    spike = next((r for r in pre if _phase_max(r, m) > OVERCURRENT_THRESHOLD_A), None)
    if spike:
        ev.append((spike["t_ms"], "phase current %.2f A (Iq %s A, lag %s deg)" % (
            _phase_max(spike, m), _fmt(spike[k("iq")]), _fmt(spike[k("lag")], 0))))
    nf = next((r for r in pre if _in_fault(r, m)), None)
    if nf:
        ev.append((nf["t_ms"], "nFAULT (driver over-current protection, outputs off)"))
    prot = next((r for r in pre if r[k("prot")] > OVERCURRENT_THRESHOLD_A), None)
    if prot:
        ev.append((prot["t_ms"], "filtered current over %.0f A" % OVERCURRENT_THRESHOLD_A))
    if _has_observer(pre, m):
        warn = next((r for r in pre if r[k("ok")] and r[k("ldmx")] > SYNC_LOAD_WARN_DEG), None)
        if warn:
            ev.append((warn["t_ms"], "observer load angle %.0f deg (pull-out near 90)" % warn[k("ldmx")]))
        slip = next((r for r in pre if r[k("slip")] > 0), None)
        if slip:
            ev.append((slip["t_ms"], "observer pole slip"))
    return sorted(ev)


def _has_observer(rows, m):
    return bool(rows) and ("m%d_ok" % m) in rows[0]


def _observer_findings(m, rows, pre, clean, t_on, out):
    """The shadow observer's view of motor m up to its first event: was it valid, did its load
    angle climb, did it count a slip, and were the synchronised samples it runs on good."""
    k = lambda f: "m%d_%s" % (m, f)
    findings, climbed = [], False
    early = _window(clean, t_on - 2000, t_on - 500)
    late = _window(clean, t_on - 100, t_on)

    def stats(win):
        ok = [r for r in win if r[k("ok")]]
        n = sum(r[k("sn")] for r in win)
        return {
            "ok": len(ok) / len(win) if win else float("nan"),
            "ld": _mean([r[k("ld")] for r in ok]),
            "mx": max((r[k("ldmx")] for r in ok if not _nan(r[k("ldmx")])), default=float("nan")),
            "rpm": _mean([r[k("orpm")] for r in ok]),
            "cmd": _mean([abs(r[k("rpm")]) for r in ok]),
            "full": sum(r[k("sf")] for r in win) / n if n else float("nan"),
        }

    e, l = stats(early), stats(late)
    slips = [r["t_ms"] for r in pre if r[k("slip")] > 0]
    out.append("  observer: valid %s -> %s of samples, load angle %s -> %s deg (peak %s), speed %s vs %s "
               "commanded, all three phases read in %s -> %s of samples (mean %+d..%+d ms -> %+d..%+d ms)." % (
                   _pct(e["ok"]), _pct(l["ok"]), _fmt(e["ld"], 0), _fmt(l["ld"], 0), _fmt(l["mx"], 0),
                   _fmt(e["rpm"], 0), _fmt(e["cmd"], 0), _pct(e["full"]), _pct(l["full"]),
                   t_on - 2000, t_on - 500, t_on - 100, t_on))
    if slips:
        findings.append("M%d: the observer counted %d pole slip(s) before the trigger, first at %+d ms." % (
            m, len(slips), slips[0]))
    if _nan(l["ok"]) or l["ok"] < 0.5:
        why = []
        if not _nan(l["full"]) and l["full"] < 0.5:
            why.append("only %s of samples had all three phases" % _pct(l["full"]))
        if not _nan(l["cmd"]) and l["cmd"] < 1000:
            why.append("below the observer's 1000 RPM minimum")
        findings.append("M%d: the observer was not valid for most of the 100 ms before its first event%s - it could "
                        "not have warned." % (m, " (" + "; ".join(why) + ")" if why else ""))
    elif (not _nan(l["mx"]) and l["mx"] > SYNC_LOAD_WARN_DEG) or (
            not _nan(e["ld"]) and not _nan(l["ld"]) and l["ld"] - e["ld"] > 20):
        climbed = True
        findings.append("M%d: the observer's load angle climbed from %s to %s deg (peak %s) before its first event - "
                        "it saw the slip coming (shadow mode: nothing acted on it)." % (
                            m, _fmt(e["ld"], 0), _fmt(l["ld"], 0), _fmt(l["mx"], 0)))
    else:
        findings.append("M%d: the observer's load angle stayed at %s -> %s deg (peak %s) up to its first event - no "
                        "warning from the observer either." % (m, _fmt(e["ld"], 0), _fmt(l["ld"], 0), _fmt(l["mx"], 0)))
    if not _nan(e["full"]) and e["full"] < 0.5:
        findings.append("M%d: only %s of synchronised samples read all three phases before the event - the "
                        "sampling window was too short or samples were dropped." % (m, _pct(e["full"])))
    if not _nan(e["rpm"]) and not _nan(e["cmd"]) and e["cmd"] > 0 and abs(e["rpm"] - e["cmd"]) > 0.05 * e["cmd"]:
        findings.append("M%d: the observer's speed (%.0f RPM) was more than 5%% off the commanded %.0f RPM before "
                        "the event - check its R, L and flux parameters." % (m, e["rpm"], e["cmd"]))
    return findings, climbed


def _pct(x):
    return "nan" if _nan(x) else "%.0f%%" % (100.0 * x)


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

    # --- order of events --------------------------------------------------------------------
    # The first motor to show trouble is where the cause is; the other usually follows because the
    # two share the load. Readings after a motor's first event say little about why it happened.
    events = {m: _events(pre, m) for m in (1, 2) if any(r["m%d_en" % m] for r in pre)}
    # Run-ups are judged up to the first current event; the observer's own warnings are what the
    # run-up is checked for, so they appear in the timeline but do not end it.
    onset = {}
    for m, ev in events.items():
        cur = [t for t, what in ev if not what.startswith("observer")]
        if cur:
            onset[m] = cur[0]
    timeline = sorted((t, m, what) for m, ev in events.items() for t, what in ev)
    findings = []
    if timeline:
        out.append("")
        out.append("Order of events:")
        out += ["  %+5d ms  M%d %s" % e for e in timeline]
    first_m = min(onset, key=onset.get) if onset else None
    if first_m:
        others = [(t, m) for m, t in onset.items() if m != first_m]
        if others:
            findings.append("M%d went first (%+d ms); M%d followed %d ms later - look for the cause on M%d." % (
                first_m, onset[first_m], others[0][1], others[0][0] - onset[first_m], first_m))
        else:
            findings.append("Only M%d showed trouble (from %+d ms)." % (first_m, onset[first_m]))

    # --- per motor ---------------------------------------------------------------------------
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
        # Once nFAULT fires the driver's outputs are off and the phase currents read ~0, so Iq and lag
        # collapse whatever the motor is doing. Only samples before the motor's first event count.
        t_on = onset.get(m, 1)
        nf_on = next((r["t_ms"] for r in pre if _in_fault(r, m)), None)
        clean = [r for r in pre if r["t_ms"] < t_on and not _in_fault(r, m)]
        out.append("M%d: %.0f RPM, vlim %.2f V at the trigger; prot %.2f A (peak %.2f A at %+d ms); "
                   "largest phase sample %.2f A at %+d ms." % (
                       m, t0[k("rpm")], t0[k("vlim")], t0[k("prot")], peak[k("prot")], peak["t_ms"],
                       ph_val, phase_peak["t_ms"]))
        # Run-up of the filtered current and the load-sense fit.
        marks = [-2000, -1000, -500, -300, -200, -100, -50, -20, -10, 0]
        marks = [t for t in marks if t >= rows[0]["t_ms"]]
        out.append("  t ms : " + " ".join("%7d" % t for t in marks))
        for f, nd in (("prot", 2), ("vlim", 2), ("iq", 2), ("lag", 0)):
            cells = []
            for t in marks:
                r = _at(rows, t)
                fault = f in ("iq", "lag") and nf_on is not None and r["t_ms"] >= nf_on
                cells.append("%7s" % ("flt" if fault else _fmt(r[k(f)], nd)))
            out.append("  %-5s: " % f + " ".join(cells))
        obs = _has_observer(rows, m)
        if obs:
            # Observer load angle ("-" where it was not valid) and the share of samples with all
            # three phases read.
            cells, full = [], []
            for t in marks:
                r = _at(rows, t)
                cells.append("%7s" % (_fmt(r[k("ld")], 0) if r[k("ok")] else "-"))
                full.append("%7s" % ("%.0f%%" % (100.0 * r[k("sf")] / r[k("sn")]) if r[k("sn")] else "-"))
            out.append("  obsld: " + " ".join(cells))
            out.append("  full : " + " ".join(full))
        if nf:
            out.append("  nFAULT: %d falling edge(s), first at %+d ms; held low in %d pre-trigger sample(s). "
                       "Iq/lag from then on are shown as flt (driver outputs off)." % (nf, nf_first, nf_low))

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
        # Run-up: the 100 ms before this motor's first event against the ~1.5 s before that.
        early = _window(clean, t_on - 2000, t_on - 500)
        late = _window(clean, t_on - 100, t_on)
        iq_early = _mean([r[k("iq")] for r in early])
        iq_late = _mean([r[k("iq")] for r in late])
        lag_early = _mean([r[k("lag")] for r in early])
        lag_late = _mean([r[k("lag")] for r in late])
        # The observer is the finer instrument: if it saw the load angle climb, the event was not sudden.
        obs_out = []
        obs_findings, obs_climbed = _observer_findings(m, rows, pre, clean, t_on, obs_out) if obs else ([], False)
        if not _nan(iq_early) and not _nan(iq_late):
            out.append("  Iq %.2f A -> %.2f A, lag %.0f -> %.0f deg (mean %+d..%+d ms -> %+d..%+d ms, before %s)." % (
                iq_early, iq_late, lag_early, lag_late, t_on - 2000, t_on - 500, t_on - 100, t_on,
                "its first event" if m in onset else "the trigger"))
            if iq_early > 1.0 and iq_late < 0.4 * iq_early:
                findings.append("M%d: Iq fell to %.2f A before its first event - the motor was at its pull-out "
                                "edge (too little voltage for the load) and slipped." % (m, iq_late))
            if not _nan(lag_early) and not _nan(lag_late) and abs(lag_late - lag_early) > 20:
                findings.append("M%d: current lag moved %.0f deg off its earlier value before its first event - "
                                "load-sense saw the stall coming." % (m, lag_late - lag_early))
            elif m == first_m and not over and not obs_climbed and iq_late < 1.3 * iq_early:
                findings.append("M%d: no load build-up before %+d ms (Iq %.2f -> %.2f A, lag steady) - a sudden "
                                "event: a jam, a broken or pulled tool, or a commutation step, not a gradual "
                                "overload." % (m, t_on, iq_early, iq_late))
        out += obs_out   # the observer's summary line, after the Iq line
        findings += obs_findings
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
