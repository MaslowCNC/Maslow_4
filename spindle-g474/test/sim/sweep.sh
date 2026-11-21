#!/bin/bash
# Robustness sweep: builds ./sim and runs it over a grid of rotor inertias and motor-constant errors
# (the model motor against the firmware's own constants), one line per case, then a summary.
#
#   ./sweep.sh                      # J 2e-6 3e-6 5e-6  x  R 0.8..1.3  x  flux 0.93..1.07 (75 runs)
#                                   (board 1 measured J 2.9e-6 / 3.4e-6 with #JMEAS)
#   J="1e-5" R="0.9 1" ./sweep.sh   # a smaller grid
#   POS=10,0.12,100 ./sweep.sh      # tuning overrides pass through to ./sim (POS, OLKI)
#
# Columns: J, flux factor, R factor | checks failed | peak rotor angle error during spin-up and the
# hand-over (deg) | spin-down through the hand-back: peak Z error (deg of phase), peak rotor angle
# error (deg), peak phase current (A) | the first failed check.
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -I. -I../../src sim_main.cpp ../../src/foc.cpp -o sim

J=${J:-"2e-6 3e-6 5e-6"}
R=${R:-"0.8 0.9 1 1.15 1.3"}
FLUX=${FLUX:-"0.93 0.97 1 1.03 1.07"}
JOBS=${JOBS:-8}

one() {
    out=$(./sim "$@" 2>&1)
    fails=$(echo "$out" | grep -o '^[0-9]* check(s) failed' | cut -d' ' -f1)
    up=$(echo "$out" | grep '6000 RPM  ' | head -1 | sed -E 's/.*rotor +([0-9.]+) deg.*/\1/')
    dz=$(echo "$out" | grep 'spun down' | sed -E 's/.*peak: zerr +([0-9.]+) deg.*/\1/')
    dr=$(echo "$out" | grep 'spun down' | sed -E 's/.*rotor +([0-9.]+) deg.*/\1/')
    di=$(echo "$out" | grep 'spun down' | sed -E 's/.*I +([0-9.]+) A.*/\1/')
    first=$(echo "$out" | grep -m1 -E 'FAIL|FAULT' | sed -E 's/^ *//')
    printf "J %-6s flux %-4s R %-4s | fails %-2s | up %5s | down z %6s rotor %7s I %5s | %s\n" \
        "$1" "$2" "$3" "$fails" "$up" "$dz" "$dr" "$di" "$first"
}
export -f one

out=$(for j in $J; do for r in $R; do for f in $FLUX; do echo "$j $f $r"; done; done; done |
    xargs -P "$JOBS" -L 1 bash -c 'one "$@"' _ | sort -k2,2g -k4,4g -k6,6g)
echo "$out"
echo "$out" | awk -F'|' '{ n++; split($2, a, " "); if (a[2] > 0) nf++;
        split($4, d, " "); if (d[5] + 0 > 150) slip++; if (d[5] + 0 > worst) worst = d[5] + 0 }
    END { printf "\n%d cases: %d with a failed check, %d with a rotor > 150 deg off at spin-down (worst %.0f deg)\n",
                 n, nf, slip, worst }'
