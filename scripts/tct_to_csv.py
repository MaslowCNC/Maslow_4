#!/usr/bin/env python3
"""Turn the log of a $TCT belt tension / motor current test into a CSV.

Reads any text that contains the firmware's "TCT ..." lines (a copy of the UI console, a
maslow_serial.py capture, a $SS dump) and writes one row per held current level.  The
scale_kg column is left empty: fill it in from the readings you wrote down at each
"TCT read" line, in step order.

    scripts/tct_to_csv.py console.txt > br.csv
    scripts/tct_to_csv.py console.txt -o br.csv
    pbpaste | scripts/tct_to_csv.py

Halt lines (the test stopped early, and why) are reported on stderr.
"""

import argparse
import csv
import re
import sys

LINE = re.compile(r"TCT (start|baseline|read|step|done|halt) (.*?)\]?\s*$")
PAIR = re.compile(r"(\w+)=(\S+)")

COLUMNS = [
    "run", "arm", "step", "dir", "target_adc", "adc", "adc_sd", "adc_min", "adc_max", "mA",
    "baseline_adc", "pwm", "pos", "move_mm", "settled", "t_s", "scale_kg",
]


def parse(lines):
    rows, run = [], 0
    for line in lines:
        m = LINE.search(line)
        if not m:
            continue
        kind, fields = m.group(1), dict(PAIR.findall(m.group(2)))
        if kind == "start":
            run += 1
        elif kind == "step":
            row = {k: fields.get(k, "") for k in COLUMNS}
            row["run"] = run
            rows.append(row)
        elif kind == "halt":
            print(f"run {run}: halted on {fields.get('arm', '?')} at step {fields.get('step', '?')}: "
                  f"{fields.get('reason', '?')}", file=sys.stderr)
        elif kind == "done":
            print(f"run {run}: {fields.get('arm', '?')} finished {fields.get('steps', '?')} steps", file=sys.stderr)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", help="log file (default: stdin)")
    ap.add_argument("-o", "--output", help="CSV file (default: stdout)")
    args = ap.parse_args()

    src = open(args.log, encoding="utf-8", errors="replace") if args.log else sys.stdin
    with src:
        rows = parse(src)
    if not rows:
        sys.exit("no 'TCT step' lines found")

    dst = open(args.output, "w", newline="") if args.output else sys.stdout
    with dst:
        w = csv.DictWriter(dst, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)


if __name__ == "__main__":
    main()
