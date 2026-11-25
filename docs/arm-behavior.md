# How the belt arms behave: current, force, slack and reversals

Bench measurements of single Maslow 4 belt arms, made in November 2026 while chasing ~1 mm part-size
errors. They describe what the arm hardware and the firmware's position loop actually do, so they can
inform motion control, tension handling and calibration. The first section is the summary; the data
behind it, and the less certain observations, follow.

**Units.** Motor current is the raw current-sense ADC reading (0–4095), the same units as `MC` in the
status report. The UI converts it as mA = 1000 × 3.3 × ADC / 4095 / 1.5, so 1000 ADC ≈ 537 mA.
PWM is the raw motor drive (0–1023). Belt positions are encoder readings in mm; "out" is paying out.

**Tools.** The tests used three bench commands on a test branch (draft PR
[#1134](https://github.com/MaslowCNC/Maslow_4/pull/1134), not part of `Maslow-Main`):
`$TCT` holds one motor at a set current while pulling in, `$TRV` runs one belt through pay-out and
reel-in under the machine's own position loop, and `$TPO` (written but not run). Forces were read by
hand from a spring scale (a luggage-type hanging scale) inline in the belt.

## Summary

### 1. Current and belt force (reeling in)

When a motor reels a belt in until it stops against a load, the belt force follows the current:

| Current (ADC) | ≈ mA | Force (kg), mean of the arms | Spread between arms |
|---|---|---|---|
| 1000 | 537 | ~12 | |
| 1200 | 645 | ~15 | |
| 1600 | 860 | 16.4 | 14.3–18.3 |
| 2000 | 1075 | 20.3 | 19.1–21.1 |
| 2400 | 1290 | 25.9 | 24.0–27.0 |
| 2800 | 1505 | 27.7 | 25.2–30.0 (3 arms) |

- The curve bends over: about 1.6 kg per 100 ADC from 1000 to 1400, about 1.2 kg per 100 ADC from 1600
  to 2400, and little gain above 2400 (≈ 26–28 kg at most). Extended down, it reaches zero force at
  roughly 250–300 ADC. Up to ~1400 ADC: force ≈ 0.016 kg/ADC × (ADC − 260).
- The current reading itself is linear: a stalled motor draws about 16–17 ADC per PWM count from 400 to
  2800 ADC on every arm, so the bend is in the motor and gearbox, not the sensing.
- The four arms agree within about ±1–2 kg (~10 %) at a given current. That is small next to the
  history effects below.
- **Find Anchors** pulls each belt to `Maslow_Retract_Current_Threshold` (1300 in `maslow-m4.yaml`; 900 on
  one test machine), or that plus 500 when measuring flex. By the table, reeling in to 900 ADC gives about
  8–11 kg (80–110 N), and 1300 ADC about 13–17 kg.

### 2. The gearbox dominates: tension depends on history, not just current

- **Belts move in jumps.** Raising the current slowly, a belt holds still until the motor's pull is
  3–6 kg above the load (more at higher loads), then jumps about 1 mm and grabs again. Small current
  steps often change nothing at all.
- **Holding is nearly free.** A belt pulled to 18–20 kg kept that tension while the current was lowered
  all the way to 200 ADC, where the motor pulls almost nothing.
- **So one current can mean a ~10 kg range of tension.** 1000 ADC gave 11.8 kg reeling in, and about
  18 kg on the same belt when coming down from a higher current.
- **Back-driving.** By hand, a belt with its motor off needed about 21 kg to start moving. Once a
  gearbox is sliding, it doesn't stop until the load falls to 8–14 kg (each arm lands at its own level).
- **A sudden loss of pull starts a slide.** Dropping the motor's pull suddenly by about 5 kg or more
  (switching off, or a big step down) let the belt slide back to 8–14 kg. Steps of about 3 kg didn't.
  Switching off when the motor was already pulling almost nothing didn't slide below ~18 kg, so it is the
  loss of pull, not the motor's off state, that triggers it. With the motor off, gearboxes held up to
  roughly 18–20 kg.
- **Idle creep.** With the motor off, a belt at ~11 kg crept out about 0.1 mm per minute.

### 3. Reversals under the position loop (one belt, taut)

The machine moves belts with a P-only loop (`MotorUnit::recomputePID`, P = 300) that drives the motor
through `DCMotor::runAtPWM`, which maps any output onto at least 75 PWM (`motorStartsToMovePWM`).
Zero error maps to +75, driving out.

- **No position is lost at a reversal.** Paying out 1 or 5 mm and coming back, at 5 and 20 mm/s, every
  run ended within 0.05 mm of where it started. The encoder and the scale agreed.
- **No force spike.** Current never jumped at a reversal.
- **The 75-PWM floor is what makes this work.** With the floor removed (same P), the loop could not
  overcome gearbox breakaway near the setpoint: the belt stopped about 0.16–0.2 mm short of the start and
  lost tension a step at a time over repeated runs.
- **The floor's cost is chatter.** While holding, the drive alternates between about +75 and −80 PWM,
  more often at low tension (110–140 sign changes in 5 s at ~3 kg), drawing 750–900 ADC, and it holds no
  more accurately than the quiet version.
- **Speed turns friction into lag.** The gearbox takes about 50–70 ms to start moving. At 20 mm/s a 1 mm
  move (50 ms) is over before the belt responds, and the belt reaches the target 0.15–0.2 s later. A 5 mm
  move at 20 mm/s builds 1.5–2.2 mm of following error. Each belt ends on target, but while moving,
  belts can be 1–2 mm behind their setpoints, by different amounts.

### 4. Slack

The encoder reads the belt after the spool, near where the belt leaves the arm.

- **Slack outside the encoder is invisible.** With no tension on the belt, the motor still pushes about
  2–2.5 mm of belt out past the encoder (less if slack is already out), and the encoder counts it as
  normal pay-out. This was the same against a rigid hard point as against the spring scale, so it is the
  arm, not the scale.
- **Then slack builds inside the arm.** Once the belt can't be pushed further, the encoder stops while the
  position loop keeps driving out (P × lag, 200–930 PWM in the tests), unwinding belt off the spool inside
  the arm. A slowly creeping encoder can keep this going for seconds.
- **At the next reversal the motor winds it back first.** The encoder doesn't move during this dead time,
  which lasted 0.4 s to 3.4 s, growing with how much belt had unwound. Then the belt snaps back in at
  9–46 mm/s, usually near full power, and still lands on position.
- **Stiffness depends on tension.** One arm (TL, the highest) went from about 3.7 kg/mm near 11 kg to
  about 1.8 kg/mm near zero, probably belt sag. A lightly loaded belt is soft, so the same lag or friction
  moves the sled further.

### 5. What this means for accuracy

- On a single belt the end position always recovers, so lost motion at reversals on one taut belt is not
  the source of ~1 mm part errors. The remaining candidates are:
  - **following lag** of 1–2 mm per belt at cutting speeds, different for each belt, which moves the sled
    off its path mid-cut;
  - **slack trailing belts**, which cost seconds of dead time when they have to re-engage while the other
    belts move the sled;
  - **calibration errors**, which create slack or excess tension in the first place;
  - **tension history**, which changes belt stretch by up to the ~10 kg band above.
- **Calibration tension** depends on the current threshold and on stick-slip: reeling in to a threshold
  lands somewhere in a band, not at one force. Approaching a measurement tension slowly from below, and
  waiting until the belt is still, gave tension repeatable to about ±1 kg on the bench.
- **Trailing belts can't be tension-controlled through current.** Back-driving needs ~21 kg to start and
  8–14 kg to keep going even with no current, and one belt reeling in would have to back-drive up to three
  trailing belts at poor angles. Trailing belts have to be driven out under position control.
- **Slack detection from encoder lag** has a ~2–2.5 mm blind zone per belt. A threshold on how far the
  encoder is behind its setpoint (e.g. > 1 mm), combined with current (a belt spinning free draws about
  400–550 ADC, a loaded one 800+), is more robust than waiting for the encoder to stop moving.

## How the tests were run

- **Rig.** One belt at a time, hooked to a spring scale (or a rigid hard point) fixed to the frame, with
  the sled locked in place. The scale plus belt stretched 3.7–5.4 kg per mm depending on the arm, so the
  encoder doubles as a force sensor between scale readings.
- **Machines.** A Maslow 4.1 (`m4.local`, first runs with the sled free) and a second machine
  (`maslow.local`, all other runs).
- **`$TCT=<arm>,from=,to=,step=,hold=`** pulls in with a closed loop on measured current, settles until
  on target and still, holds and averages. Sweeps step up and back down; "one-shots" go to one level from
  the relaxed belt the previous run left.
- **`$TRV=<arm>,<out mm>,<mm/s>`** holds 5 s, pays out, holds, reels back, holds, under P = 300 with the
  75-PWM floor (or `floor=0`), logging setpoint, encoder, P output and current at 50 Hz.
- **Readings.** Scale readings are by eye, to about ±0.1–0.2 kg. Before most runs the belt was preloaded
  with a `$TCT` one-shot at 1000 ADC.

## Raw data

### A. One-shots: force at each current (`$TCT`, sled locked)

Each run starts from the tension the previous run left after the motor stopped.

| Arm | ADC | Before (kg) | Hold (kg) | After motor off (kg) | Belt reeled in (mm) | Note |
|---|---|---|---|---|---|---|
| TR | 1600 | 5.5 | 16.3 | 10.8 | 2.28 | |
| TR | 2000 | 10.8 | 21.1 | 10.2 | 1.94 | |
| TR | 2400 | 10.2 | 26.4 | 8.3 | 3.10 | |
| TR | 2800 | 8.3 | 27.8 | 7.7 | 3.63 | |
| BR | 1600 | 9.3 | 14.3 | 12.4 | 1.02 | |
| BR | 2000 | 12.4 | 15.2 | 12.7 | 0.55 | stuck short; retest below |
| BR | 2000 | 12.7 | 20.7 | 14.25 | 1.47 | retest |
| BR | 2400 | 14.25 | 24.0 | 13.3 | 2.76 | |
| BR | 2800 | 13.3 | 25.2 | 13.2 | 2.88 | first try halted on the current cap |
| BL | 1600 | 7.7 | 18.3 | 9.2 | 4.29 | |
| BL | 2000 | 9.2 | 20.2 | 9.0 | 2.05 | |
| BL | 2400 | 9.0 | 27.0 | 9.0 | 3.54 | |
| BL | 2800 | 9.0 | 30.0 | 9.0 | 3.87 | |
| TL | 1600 | 3.5 | 16.8 | 10.4 | 3.99 | |
| TL | 2000 | 10.4 | 19.1 | 10.2 | 2.17 | |
| TL | 2400 | 10.2 | 26.2 | 9.8 | 4.29 | current noisy during the hold: probably slipped |
| TL | 2800 | 9.8 | — | 9.0 | — | halted on the current cap twice; frame flexed |

Power needed to hold a stalled belt (from the logged PWM): 400 ADC ≈ 21–26 PWM, 1000 ≈ 58–68,
1600 ≈ 93–109, 2000 ≈ 120–134, 2400 ≈ 138–151, 2800 ≈ 163–187.

### B. Sweeps: 200 → 1800 → 200 ADC in steps of 200, 8 s holds (`$TCT`, sled locked)

The belt only moved at a few levels. Scale plateaus (kg) between those jumps:

| Arm | Start | Jumps on the way up (ADC: mm) | Plateaus (kg) | Coming down | After motor off |
|---|---|---|---|---|---|
| BL | 6.0 | 1000: 1.04, 1400: 1.22 | 6.0 → 11.8 → 18.2 | held, drifted to 17.9 | held |
| TL | 8.8 | 800: 0.11, 1200: 1.39, 1800: 0.82 | 8.8 → 9.15 → 14.8 → 18.1 | held, drifted to 17.8 | held |
| TR | 7.8 | 800–1000: 0.03, 1400: 1.03, 1800: 1.28 | 7.8 → 8.1 → 13.8 → 20.6 | held, drifted to ~19.85 | slid to 12.6 |
| BR | 11.4 | 1600: 0.6 | 11.4 → 14.5 | held | slid back ~1.1 mm |

Faster BL sweep (600 → 1800 in steps of 400): 600 → ~7 kg (unchanged), 1000 → 11.9, 1400 → 18.3,
1800 → 20.8; barely changed coming down; about 9 kg after the motor stopped. At 200 ADC the current
reading is noisy (sd ~50–70 ADC), so the lowest levels are unreliable.

### C. Free sled (`m4.local`, BR with the scale inline, other belts idle)

With the sled free, BR pulled against the other arms' gearboxes. It held at 800 and 1000 ADC, but between
1000 and 1200 the opposite belt (TL) back-drove: the sled moved, the belt ran 15 mm, and the test halted.
Two runs at 1200 did the same.

### D. Reversals (`$TRV`, sled locked, 5 s holds)

| Arm | Run | Worst lag out / back (mm) | Delay before the encoder followed, out / back | Ended (mm) | Scale before → lowest → after (kg) |
|---|---|---|---|---|---|
| TR | 1 mm @ 5 mm/s | 0.27 / 0.36 | 71 / 72 ms | +0.011 | 8.5 → 3.5 → 8.3 |
| TR | 1 mm @ 5, no floor | 0.47 / 0.91 | 99 / 192 ms | +0.16 | 8.3 → 7.3 → 6.3 over two runs |
| TR | 5 mm @ 5 (went slack) | 0.7 / 3.5 | 68 / 850 ms | +0.011 | 8.3 → 0 → 8.5 |
| TR | 1 mm @ 20 | 0.90 / 0.97 | after the 50 ms move | +0.011 | 8.5 → 3.5 → 7.9 (re-read late) |
| TR | 5 mm @ 20 (went slack) | 2.15 / 4.4 | 52 / 410 ms | +0.011 | 7.9 → 0 → 7.9 |
| TL | 1 mm @ 5 | 0.21 / 0.42 | 54 / 74 ms | +0.011 | 11.3 → 7.6 → 11.5 |
| TL | 5 mm @ 5 (not slack) | 0.27 / 0.42 | 70 / 75 ms | +0.032 | 11.2 → 0.3 → 11.0 |
| TL | 1 mm @ 20 | 0.83 / 0.91 | 48 ms / after the move | +0.011 | 10.8 → 7.0 → 10.7 |
| TL | 5 mm @ 20 (not slack) | 1.54 / 1.61 | 52 / 46 ms | 0.000 | 10.7 → 0.3 → 10.8 |
| TL | 10 mm @ 20 (went slack) | 3.1 / 6.9 | 58 ms / 1.19 s | +0.043 | 10.4 → 0 → 10.4 |
| TR | hard point, 5 mm @ 5 (slack) | 2.4 / 3.0 | 62 ms / 2.0 s | +0.054 | — |
| TR | hard point, repeat | 3.1 / 2.1 | 62 ms / 3.4 s | 0.000 | — |

Slack episodes in detail:

| Run | Encoder stopped at | Loop output while stuck | Wind-up before the belt moved back | Snap back (time, peak speed) |
|---|---|---|---|---|
| TR 5 mm @ 5 | 4.33 mm | ~205–264 PWM, 1.3 s | 0.85 s | ~0.2 s, ~20 mm/s |
| TR 5 mm @ 20 | 4.40 mm | ~180–260 PWM, 1.2 s | 0.41 s at full power | 0.19 s, ~46 mm/s |
| TL 10 mm @ 20 | 6.84–6.92 mm | ~926 PWM, ~1 s | 1.19 s at full power | 0.63 s, ~41 mm/s |
| TR hard point | ~3.0 mm (crept) | ~595 PWM, ~2.5 s | 2.0 s | 1.27 s, ~14 mm/s |
| TR hard point repeat | 2.07 mm | ~878 PWM, ~1.5 s | 3.4 s | 0.76 s, ~9 mm/s |

During the holds with the floor, the drive alternated between about +75 and −80 PWM. Without the floor
it sat at 0–50 ADC with no chatter.

## Less certain observations and open questions

- **Why force flattens above ~2400 ADC.** The current reading stays linear, so it is mechanical, but
  whether the gearbox, the motor or the frame limits it is not known. At 2800 ADC the frame visibly flexed
  on TL.
- **The breakaway margin grows with load.** The extra pull needed to start a stuck belt was about 2.6 kg
  at 6 kg, 3.2 kg at 12 kg and 6.4 kg at 18 kg on one arm, which suggests friction roughly proportional
  to load. Only a few points.
- **The slide threshold.** Steps of ~3 kg didn't slide and ~5 kg did; the exact value and how it depends
  on load are not pinned down.
- **Where a slide lands.** Each arm landed at its own level (BL ~9, TL 9–10.4, TR 7.7–10.8, BR 12.4–14.3 kg).
  On TR it fell over successive runs (10.8 → 7.7), possibly the gearbox warming up.
- **Belt sag.** The softening of TL near zero tension is attributed to sag on its long, high run, not
  measured directly.
- **Brake or coast.** Which electrical state `DCMotor::stop()` leaves the driver in is not confirmed (the
  comments in `DCMotor.cpp` disagree); it did not matter for the slides above.
- **Scale effects.** The spring scale has a few tenths of a kg of hysteresis and drift; one reading came
  back 0.6 kg low while the encoder showed the belt within 0.01 mm of where it started.
- **Only one belt at a time, with the sled locked.** How four belts share the sled, which belt sets the
  position when a trailing belt is slack, and how the following lags combine, are untested.
- **Low-tension holding with the floor.** The chatter rate and current depend on tension; only two
  tension levels were measured.
