#!/usr/bin/env python3
"""Unit test: trip_analysis.parse/summarise on synthetic recordings, old and sync-sense formats.

    python3 tests/test_trip_analysis.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import trip_analysis as ta  # noqa: E402

failures = []


def check(name, cond, detail):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name)
        print("     " + detail.replace("\n", "\n     "))


OBS = ["ok", "ld", "ldmx", "orpm", "slip", "sn", "sf", "slo", "soff"]
CORR = ["lr", "lrmx"]   # resistance-corrected load angle, newer recordings


def recording(obs, spike_t=-300, load_climb=False, obs_valid=True, full=20, split_header=False, corrected=False):
    """M2 jumps to 11.9 A at spike_t and faults 3 ms later; M1 follows 50 ms after.  With obs, each
    motor also has observer columns: its load angle climbs to 85 deg over the 200 ms before the spike
    when load_climb, else stays at 15 deg.  With corrected, that climb is in the corrected angle
    (lr) and the raw one (ld) reads a steady -15 deg, as an over-driven open loop does."""
    def motor_cols(m):
        cols = ["m%d_%s" % (m, f) for f in ta.MOTOR_FIELDS]
        return cols + (["m%d_%s" % (m, f) for f in OBS + (CORR if corrected else [])] if obs else [])

    hdr1 = "TRIPCSV,t_ms,foc_loops,foc_max_us,z_phase_deg,z_target_deg," + ",".join(motor_cols(1)) + ","
    hdr2 = ",".join(motor_cols(2))
    lines = ["[TRIP] ===== trip recording: driver over-current (nFAULT retries) (at 100.000s uptime) =====",
             "[TRIP] 1000 samples, t_ms is relative to the trigger.  TRIP reprints, REARM clears."]
    lines += [hdr1, hdr2] if split_header else [hdr1 + hdr2]
    for i in range(1000):
        t = -1900 + 2 * i
        row = [t, 50, 120, 10.0, 10.0]
        for m in (1, 2):
            t_spike = spike_t if m == 2 else spike_t + 50
            faulted = t >= t_spike + 3
            ia = 11.9 if t_spike <= t < t_spike + 3 else (0.02 if faulted else 2.0)
            row += [1, 12000, 10.0, ia, -ia / 2, -ia / 2, 1.5, 1 if faulted else 0, 1 if faulted else 0,
                    0.5, 0.05 if faulted else 2.5, 0 if faulted else 70]
            if obs:
                ld = 15.0
                if load_climb and t_spike - 200 <= t < t_spike:
                    ld = 15.0 + 70.0 * (t - (t_spike - 200)) / 200.0
                ok = 1 if obs_valid and not faulted else 0
                nan = float("nan")
                raw = -15.0 if corrected else ld
                row += [ok, raw if ok else nan, raw + 3 if ok else nan, 12010 if ok else nan,
                        0, 20, 0 if faulted else full, 0 if faulted else 20, 20 if faulted else 0]
                if corrected:
                    row += [ld if ok else nan, ld + 3 if ok else nan]
        lines.append("TRIPCSV," + ",".join("%g" % v for v in row))
    lines.append("[TRIP] ===== end of trip recording =====")
    if split_header:
        # A serial-hub log: every line has a timestamp in front.
        lines = ["12:00:00.000 " + s for s in lines]
    return "\n".join(lines) + "\n"


def run(name, text):
    recs, warnings = ta.parse(text)
    rep = ta.summarise(recs[-1]) if recs else ""
    return recs, warnings, rep


# Old format (no observer columns, no header the parser needs): still parsed by position.
recs, warn, rep = run("old", recording(obs=False))
check("old format parses", len(recs) == 1 and len(recs[0]["rows"]) == 1000 and not warn, str(warn))
check("old format: M2 went first, no observer lines", "M2 went first" in rep and "observer" not in rep, rep)

# Sync-sense format, one-line header (the XY board's /spindle_trip.csv), observer saw it coming.
recs, warn, rep = run("climb", recording(obs=True, load_climb=True))
check("sync format parses by header", len(recs[0]["rows"]) == 1000 and "m2_ldmx" in recs[0]["rows"][0], str(warn))
check("observer warning is in the timeline", "M2 observer load angle" in rep, rep)
check("observer climb reported", "M2: the observer's load angle climbed" in rep and "saw the slip coming" in rep, rep)
check("current event still decides who went first", "M2 went first (-300 ms)" in rep, rep)
check("no 'sudden event' when the observer saw a climb", "M2: no load build-up" not in rep, rep)
check("obsld row shown, '-' once faulted", "obsld:" in rep and rep.split("obsld:")[1].split("\n")[0].strip().endswith("-"), rep)

# Sync-sense format from the USB console: header split in two, hub timestamps; observer steady.
recs, warn, rep = run("steady", recording(obs=True, split_header=True))
check("split USB header joined", recs and len(recs[0]["rows"]) == 1000 and "m2_soff" in recs[0]["rows"][0] and not warn,
      str(warn))
check("steady observer reported", "M2: the observer's load angle stayed at 15 -> 15 deg" in rep
      and "no warning from the observer either" in rep, rep)

# Observer never valid, and the sampler reads all three phases in only 5 of 20 samples.
recs, warn, rep = run("blind", recording(obs=True, obs_valid=False, full=5))
check("invalid observer reported", "M2: the observer was not valid" in rep and "could not have warned" in rep, rep)
check("poor sampling reported", "only 25% of synchronised samples" in rep, rep)

# Corrected load angle present: the climb shows only in it, and the report judges on it.
recs, warn, rep = run("corrected", recording(obs=True, load_climb=True, corrected=True))
check("corrected columns parse", recs and "m2_lrmx" in recs[0]["rows"][0] and not warn, str(warn))
check("corrected warning in the timeline", "M2 observer corrected load angle" in rep, rep)
check("corrected climb reported, raw shown alongside",
      "M2: the observer's corrected load angle climbed" in rep and "[against V alone -15 -> -15]" in rep, rep)
check("obslr row shown", "obslr:" in rep, rep)

print("\n%d failure(s)" % len(failures))
sys.exit(1 if failures else 0)
