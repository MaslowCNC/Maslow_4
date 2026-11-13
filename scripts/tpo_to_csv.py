#!/usr/bin/env python3
"""Turn the log of $TPO belt pay-out tests into CSV files.

Reads any text that contains the firmware's "TPO ..." lines (a copy of the UI console, a
maslow_serial.py capture) and writes:

  - the samples (~10 per second): run, arm, mode, target_adc, t, phase, pos, v, adc, pwm
  - with --summary, one row per run from its "TPO summary" line, with empty columns for the
    scale readings at breakaway, while moving and at the end hold

    scripts/tpo_to_csv.py console.txt -o samples.csv --summary runs.csv

Halt lines are reported on stderr. tct_to_csv.py does the same for $TCT.
"""

import argparse
import csv
import re
import sys

LINE = re.compile(r"TPO (start|s|breakaway|ready|end|summary|done|halt) (.*?)\]?\s*$")
PAIR = re.compile(r"(\w+)=(\S+)")

SAMPLE_COLS = ["run", "arm", "mode", "target_adc", "t", "ph", "pos", "v", "adc", "pwm"]
SUMMARY_COLS = [
    "run", "arm", "mode", "target_adc", "speed", "dist", "breakaway", "breakaway_adc", "breakaway_pwm",
    "breakaway_t", "moving_adc", "moving_adc_sd", "moving_v", "moving_n", "jumps", "end_adc", "end_pwm",
    "pos", "t_s", "halt", "scale_kg_breakaway", "scale_kg_moving", "scale_kg_end",
]


def parse(lines):
    samples, summaries = [], []
    run, start, halt = 0, {}, ""
    for line in lines:
        m = LINE.search(line)
        if not m:
            continue
        kind, fields = m.group(1), dict(PAIR.findall(m.group(2)))
        if kind == "start":
            run, start, halt = run + 1, fields, ""
        elif kind == "s":
            row = {k: fields.get(k, "") for k in SAMPLE_COLS}
            row.update(run=run, arm=start.get("arm", ""), mode=start.get("mode", ""), target_adc=start.get("target_adc", ""))
            samples.append(row)
        elif kind == "halt":
            halt = fields.get("reason", "?")
            print(f"run {run}: halted on {fields.get('arm', '?')} in {fields.get('phase', '?')}: {halt}", file=sys.stderr)
        elif kind == "summary":
            row = {k: fields.get(k, "") for k in SUMMARY_COLS}
            row.update(run=run, halt=halt)
            summaries.append(row)
    return samples, summaries


def write(path, cols, rows):
    out = open(path, "w", newline="") if path else sys.stdout
    with out:
        w = csv.DictWriter(out, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", help="log file (default: stdin)")
    ap.add_argument("-o", "--output", help="samples CSV (default: stdout)")
    ap.add_argument("--summary", help="also write one row per run to this CSV")
    args = ap.parse_args()

    src = open(args.log, encoding="utf-8", errors="replace") if args.log else sys.stdin
    with src:
        samples, summaries = parse(src)
    if not samples and not summaries:
        sys.exit("no 'TPO' lines found")
    write(args.output, SAMPLE_COLS, samples)
    if args.summary:
        write(args.summary, SUMMARY_COLS, summaries)


if __name__ == "__main__":
    main()
