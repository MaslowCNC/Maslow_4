#!/usr/bin/env python3
"""Turn the log of $TRV belt reversal tests into CSV files.

Reads any text that contains the firmware's "TRV ..." lines (a copy of the UI console, a
maslow_serial.py capture) and writes:

  - the samples (up to 50 per second, printed after each run): run, arm, floor, t, sp, pos, err,
    pid, out, adc. sp/pos are mm from the start position (+ is out); err = pos - sp, like BE;
    pid is the P output before DCMotor::runAtPWM's 75-PWM floor, out the PWM commanded.
  - with --phases, one row per phase from the "TRV phase" lines (holds and moves)

    scripts/trv_to_csv.py console.txt -o samples.csv --phases phases.csv
    scripts/trv_to_csv.py console.txt -o samples.csv --kg-per-mm 5.2 --pos0-kg 12

With --kg-per-mm (the spring scale's stiffness for that arm, from $TCT runs) a force column is
added: force_kg = pos0_kg - kg_per_mm * pos, so paying out lowers the force.

Slack and halt lines are reported on stderr. tct_to_csv.py and tpo_to_csv.py do the same for
$TCT and $TPO.
"""

import argparse
import csv
import re
import sys

LINE = re.compile(r"TRV (start|s|phase|slack|halt|finished|halted|done) (.*?)\]?\s*$")
PAIR = re.compile(r"(\w+)=([^\s,]+)")

SAMPLE_COLS = ["run", "arm", "floor", "t", "sp", "pos", "err", "pid", "out", "adc"]
PHASE_COLS = ["run", "arm", "floor", "step", "kind", "sp", "err_mean", "err_min", "err_max", "flips",
              "adc_mean", "adc_max", "dead_ms", "pos_end", "t_s"]


def parse(lines):
    samples, phases = [], []
    run, start = 0, {}
    for line in lines:
        m = LINE.search(line)
        if not m:
            continue
        kind, fields = m.group(1), dict(PAIR.findall(m.group(2)))
        if kind == "start":
            run, start = run + 1, fields
            continue
        common = {"run": run, "arm": start.get("arm", ""), "floor": start.get("floor", "")}
        if kind == "s":
            row = {k: fields.get(k, "") for k in SAMPLE_COLS}
            row.update(common)
            samples.append(row)
        elif kind == "phase":
            row = {k: fields.get(k, "") for k in PHASE_COLS}
            row.update(common)
            phases.append(row)
        elif kind in ("slack", "halt"):
            print(f"run {run}: {kind} " + " ".join(f"{k}={v}" for k, v in fields.items()), file=sys.stderr)
    return samples, phases


def write(path, cols, rows):
    out = open(path, "w", newline="") if path else sys.stdout
    with out:
        w = csv.DictWriter(out, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", help="log file (default: stdin)")
    ap.add_argument("-o", "--output", help="samples CSV (default: stdout)")
    ap.add_argument("--phases", help="also write one row per phase to this CSV")
    ap.add_argument("--kg-per-mm", type=float, help="spring scale stiffness, adds a force_kg column")
    ap.add_argument("--pos0-kg", type=float, default=0.0, help="scale reading at the start position (kg)")
    args = ap.parse_args()

    src = open(args.log, encoding="utf-8", errors="replace") if args.log else sys.stdin
    with src:
        samples, phases = parse(src)
    if not samples and not phases:
        sys.exit("no 'TRV' lines found")
    cols = list(SAMPLE_COLS)
    if args.kg_per_mm:
        cols.append("force_kg")
        for r in samples:
            try:
                r["force_kg"] = f"{args.pos0_kg - args.kg_per_mm * float(r['pos']):.2f}"
            except ValueError:
                r["force_kg"] = ""
    write(args.output, cols, samples)
    if args.phases:
        write(args.phases, PHASE_COLS, phases)


if __name__ == "__main__":
    main()
