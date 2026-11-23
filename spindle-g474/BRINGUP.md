# 36 V spindle (STM32G474) — what is untested, and a safe bring-up

Written 2026-11-17 with no board in hand: everything here compiles and the pin map matches the
netlist, but **no line of it has run on hardware**. Work through the steps in order; each one checks
an assumption the next one relies on. Stop at the first surprise.

## Board 1 results (2026-11-20, USB power only, steps 1-2.3)

- **Step 1 passed.** The ESP32-S3 arrived blank; factory image + `uploadfs` over USB worked.
  `$MBOARD` = Unified Control Board 36V, 590 mV. The config was copied from `maslow-mini-36v.yaml`.
  The USB port carries **no FluidNC console** (every Maslow build keeps USB in JTAG/serial mode, see
  `esp32s3/Console.cpp`): it shows only ROM, `printf` and panic output, and opening it resets the
  chip. Commands need WiFi: a fresh board is AP `maslow` / `12345678` at 192.168.0.1, then set the
  STA network in the web UI. Rename it (`$Hostname`): the default `maslow` clashes with other machines.
- **Step 2.1 done over the link, not SWD:** `$Spindle/G474=FLASH /file.bin` erased, wrote and
  verified 90 KB in 23 s on the first try (step 9's path; no ST-LINK needed for a blank G474). Put
  the .bin on the XY filesystem with the web UI's file upload (`POST /files`).
  - Found and fixed (82b7507da): after a flash the XY link stayed in 8E1, so every G474 line was
    garbage. `uart_mode()` passed an unzeroed config and IDF 5.5 then silently ignored the change back
    to 8N1 (the USB console shows `fnc_uart_param_config(1010): not able to power down in light
    sleep`). Maslow-Main has the same bug.
- **Step 2.2 passed:** `ready: G474 FOC, PWM 20000 Hz, update on the valley`. The `g474-bench` boot
  trace (`MSG:boot: ...` per init step) shows the link at 115200 (clock right), the ADC interrupt at
  20 kHz from TIM1 TRGO2 (40 in 2 ms, 4-5 us each with the drive off), and offset calibration within
  range on all six channels.
- **Step 2.3:** vbus 0.5 V and NTCs 29 / 26 C after fixing the slow-channel bug (1821880c2: the
  HAL's `ADC_CHANNEL_n` is a bitfield, so ADC2 had been converting PA1 = motor 1's phase A shunt
  input for "VBUS" and channel 0 for both NTCs). Current offsets are fine (means within ~10 counts of
  the zero) but **the noise is high**: `#NOISE 2000` gives sd 14-35 counts (0.15-0.36 A) on five
  channels and ~130 counts (1.35 A, range -1.7..+2.6 A) on motor 1 phase B (PA7, ADC2). All six RC
  networks are identical (1k / 100 pF, only ~1.6 MHz filtering). See step 2.4 for the cause.
- **Step 2.4 (36 V, one motor connected, drive off):** vbus 36.5 V (2.323 V at the pin); compare
  with a meter. The noise **doubled**: sd 54-60 counts on M1 a / M2 a, 26-41 on M1 c / M2 b / M2 c,
  ~280 counts (2.9 A, bursts to -970 counts = +10 A for ~4 samples) on M1 b. **Cause: U9, the
  TPS54360B 3V3 buck** (RT 200k, measured 480.7 kHz; 12V_GD is a linear BCX56 and quiet):
  - `#DUMP` shows the six channels moving together (M1 a / M1 c / M2 a / M2 c correlate +0.87..+0.98;
    M1 b anti-phase, 5x larger), so it is one disturbance on the shared ground/reference, with
    bursts every ~30-36 samples and a spectral cluster at 510-600 Hz;
  - moving the PWM to 20.2 kHz (`-D PWM_FREQUENCY_OVERRIDE_HZ=20200`) moved the line to 4222 Hz (+2nd
    harmonic at 8.44 kHz), exactly 24 x 20204 Hz - 480.7 kHz: U9's edges drifting through the ADC
    sampling instant. It grew at 36 V because SW_NODE now swings 36 V, not 4.6 V.
  - Not the slow ADC2 conversions: `#SLOW 0/1/2` give the same sd (verified after the scanf fix below).
  - Fix options (hardware): synchronize U9 to the PWM (its RT/CLK pin accepts 160 kHz-2.3 MHz; a G474
    timer at 24 x 20 kHz, phased so no edge lands in the sampling window), more RC filtering at the
    MCU pins (1k / 100 pF passes 480 kHz untouched), check ISB1's route and U9's hot-loop ground
    return against the shunt grounds, a snubber on SW_NODE.
  - Consequences now: closed-loop quality will suffer, and BRINGUP step 4's `#TRIP 6 10` would
    nuisance-trip on M1 b (bursts reach ~10 A, the comparator's 10 A threshold is 48.5 mV at the pin).
    Keep the defaults (15 A x 3 periods, 20 A comparator) for the first `#ALIGN`; its readout is now a
    100 ms average, which reads through the noise.
- **Found and fixed (G474): every numeric `#` argument was ignored.** newlib-nano was linked with float
  printf but not float scanf, so `sscanf("%f")` parsed nothing: `#TRIP`, `#IHOLD`, `#POS`, `#OBS`, `#LOG`
  silently did nothing (`#ALIGN` only worked because 0.5 V is its default). Now built with
  `PIO_FRAMEWORK_ARDUINO_NANOLIB_FLOAT_SCANF`; check any tuning command's echo after a reflash.
- **Found and fixed (G474): no drive enable ever succeeded.** `hw::enableOutputs()` read MOE back right
  after setting it, but MOE reads set only ~1 us later (break filter fDTS/8 x 8); every `#ALIGN` was
  refused with "TIM1 MOE would not set". It now polls up to 5 us and fails only on a break or timeout.
  Found with `#BRK` (break flags, MOE, SR/BDTR/AF1, comparator activity) and `#MOE` (MOE read-back timing).
- **Step 4 (`#ALIGN 0.5`, default trips, 36 V at 0.3 A limit) passed:** averaged over 100 ms,
  M1 ia +1.43 ib -0.92 ic -0.82 A, M2 ia +1.70 ib -0.70 ic -0.74 A. `SENSE_SIGN = -1` is right and each
  sense input reads its own phase (ib ~ ic ~ -ia/2; sums -0.31 / +0.26 A, within the offset/noise
  error). Both bridges carried current. The currents are above the 0.5-1.2 A estimate (R nearer
  0.3 ohm than 0.39?). Sense noise while switching = idle (all U9). ISR 14 us with both bridges
  modulating.
- **Step 5 (open loop, 1000 RPM, both motors loose, supply limit ~1 A) passed after one fix:**
  - First tries lost most link commands mid-spin (an `S0` too), then the G474 reset with "watchdog (main
    loop stalled)". Not the link: the ADC interrupt overran its 50 us period (55 us; the board record
    said it died INSIDE the interrupt), so interrupts ran back to back and the main loop never ran.
    ~20 newlib sinf/cosf/atan2f calls per interrupt were most of it. Now fast sin/cos/atan2 (error
    < 4e-7 / 1e-5 rad; sim passes, sweep unchanged: 0 slips, the same lightest-rotor / R-20 % soft
    fails), flash prefetch on, and 8 overruns in a row now turn the motors off (fault "control
    interrupt overran") instead of starving the main loop into a watchdog reset.
  - 6 s at 1000 RPM: interrupt 34 us (35 us worst since boot), 0 overruns, link fully responsive,
    `#SPIN` ramp-down and power-off clean. M1 obs negative / M2 positive (opposite senses as expected).
    M2 id ~2-3.7 A, iq ~0; M1 noisy (its phase B noise lands in dq). Flux ~1.0 (0.93-1.28). Observer
    speeds wander +-50 % around 1000 RPM (0.8 V back-EMF, noisy M1): watch the 2000 RPM hand-over
    (needs both within 10 %) in step 6.
  - Before closed loop: the U9 noise (above) and more interrupt headroom (closed loop adds work; CCM
    SRAM for the interrupt code would help).
- **Step 6 (`#SPIN 3000 8000`) did not reach closed loop: "observer did not lock at the closed-loop
  hand-over speed"** after 1.4 s at 2000 RPM; stopped cleanly, interrupt 34-36 us, 0 overruns.
  - Trip recording (XY `/spindle_trip.csv`, 2 ms samples - really 2.1 ms under load): both observers
    average the command exactly (2000 / 2002 RPM: the rotors track the I/f drive) but ripple sd ~240 RPM
    (12 %) at 33.4 and 66.9 Hz = 1x and 2x electrical (1 pole pair) on both motors, so they are inside the
    10 % band only 51-56 % of the time, never the 100 ms in a row the hand-over needs (longest 12-24 ms).
  - 1x / 2x electrical ripple is what current-measurement offset / gain errors do to the observer, and
    the ALIGN currents do not add up: ia+ib+ic = -0.31 A (M1) / +0.26 A (M2) of ~1.5 A, ib/ic up to 28 %
    off -ia/2. A fraction of a milliohm of shared copper/ground in the 5 mOhm shunt sense paths would do
    it (the same paths that let U9's noise in): check the shunt sense taps (Kelvin?), and/or calibrate
    per-phase gains in firmware (ALIGN along each phase axis, Kirchhoff sum = 0).
  - ALIGN slopes (0.05 -> 0.5 V) give R ~0.34 ohm (M1) / 0.29 ohm (M2), not the ESC1 bench's 0.39 ohm:
    `#OBS 0.31 75e-6 7.84e-3` (SI units) before the next try.
  - A fault during a `#SPIN` bench test is no longer latched into the status line's fault code: the
    XY board would otherwise run its job recovery ($STOP, then a Z re-home 'G') - motion on a bench.
- **Per-phase current gains (`#GCAL`, 2026-11-20):** 0.8 V DC vectors at 0-300 deg in 60 deg steps (rotors
  snap, nothing spins), +/- pairs remove the offsets, Kirchhoff (ia+ib+ic = 0) gives the gains.
  Board 1: M1 a 1.111 b 0.906 c 0.983, M2 a 0.889 b 1.076 c 1.035 (channels up to 11 % off: copper in
  the 5 mOhm sense paths). The pure-gain model fits to 0.001 A; an independent ALIGN now sums to
  -0.03 / -0.06 A (was -0.31 / +0.26) and both motors draw the same current. Apparent R per axis
  0.28-0.35 ohm, mean 0.32 (`#OBS 0.32 75e-6 7.84e-3`). The gains are not stored yet: re-run `#GCAL`
  or `#GAIN1` / `#GAIN2 <a> <b> <c>` after every reboot or reflash.
  - Observer at the 3 A open-loop 2000 RPM hold: speed ripple M1 12.1 -> 7.6 %, M2 11.6 -> 4.7 % (M2 now
    100 % inside 10 %); what is left on M1 is 2x electrical plus its phase B (U9) noise.
  - The hand-over lock test now averages the observer speed over 30 ms (`OBS_LOCK_TAU_S`; the rest still
    uses the 5 ms filter). With it: **"closed loop: observers locked at 2000 RPM"** - the first hardware
    hand-over (interrupt 43 us in closed loop, 0 overruns).
  - **Closed loop then oscillated and tripped:** 109 ms after the hand-over (speed ramping 2005 -> 2520)
    the M1 comparator tripped (20 A). Both motors oscillated at ~52 Hz, growing: M2 observer speed
    1100-3550 RPM, iq +-8 A at its limit, Z error to 25 deg; M1 iq -6.7..+11 A, 14.6 A peak. 52 Hz is far
    above the angle loop's designed natural frequency (~9-17 Hz with 10/0.12/100 and J 1e-5), so suspect
    the derivative term on a still-rippling observer speed (5-8 % = 10-16 rad/s -> 1-2 A of iq at
    kd 0.12; the 5 ms speed filter lags ~57 deg at 50 Hz) and M1's phase B noise, before the inertia.
    Next: lower kd / the low-speed bandwidth (`#POS`, `#POSBW`), filter the derivative, or fix the U9
    noise first.
- **Step 6.1 passed (closed loop, 3000 RPM, loose motors):** the oscillation's iq/speed swing put the
  rotor inertia at ~2-5e-6 kg m^2, not the simulation's 1e-5 (the D term alone then made a speed loop
  near 400 rad/s, above the 5 ms speed filter's 200 rad/s corner). Angle gains scaled by that ratio:
  `#POS 2.4 0.029 24` (now the defaults, with R 0.32). Simulation: stable for J 2e-6..1e-5 with R/flux
  errors (0 slips, no hunting); only the 15/36 mNm load-step Z checks fail (too soft - retune for
  stiffness once J is measured and the U9 noise is fixed). On board 1, with the `#GCAL` gains set:
  hand-over at 2000 RPM, ramp to 3000, **6.5 s in closed loop**: observers 3000 RPM +-6 % (single
  samples), id ~0, small iq (M2 -0.5..+1.9 A; M1 noisier), q voltage 2.2-3.0 V (back-EMF 2.46 V
  expected), Z error within +-4 deg, interrupt 37 us, 0 overruns; then the timed ramp-down, "open loop
  below 1500 RPM", stop. Next: step 6.2 (6000 / 9000 / 12000 RPM, measure J from a speed step),
  6.3-6.5; store the `#GCAL` gains (they are lost at every reboot).
- **Step 6.2 passed (closed loop to 12000 RPM, loose motors, ~1 A supply limit):**
  - A first try slipped 60 ms after the hand-over (M1 fell 90 deg behind): the hand-over seeded the
    angle loop's integrator and the fading d current from that instant's measured current, which on M1
    carries phase B's noise bursts (first closed-loop sample id -3.2 / iq -4.2 A while the drive held
    +3 / 0). It now seeds them from the open loop's own current vector turned by the tracked load angle:
    three hand-overs in a row clean, sim unchanged, sweep worst rotor 28 -> 21 deg.
  - `#JMEAS <rpm>` (iq averaged every interrupt: 0.5 s steady, the 500 rad/s^2 ramp, 0.5 s steady; drag
    = the mean of the two steady values) measured **J 2.9e-6 (M1) / 3.4e-6 (M2) kg m^2** over 3000->6000->
    9000->12000 RPM, repeatable to ~10 % (the simulation check reads 3.09e-6 for a 3.00e-6 model rotor).
    M2's steady drag ~0.5 A (6 mNm), roughly flat with speed.
  - 3000 / 6000 / 9000 / 12000 RPM: observers within 1 % above 6000 RPM, flux 0.93-1.07, q voltage
    2.9 / 4.7 / 7.4 / 10 V (back-EMF 0.82 V per 1000 RPM; the ~20 V limit is far off), Z error within
    +-4.4 deg, interrupt 37 us, 0 overruns; ramp-down, hand-back at 1500 RPM and stop clean.
  - Gains re-scaled to the measured J: `#POS 3.0 0.036 30` (now the defaults) ran the same sequence on
    board 1. The simulation now defaults to J 3e-6 (sweep grid 2e-6 / 3e-6 / 5e-6): 0 slips in 75 cases;
    every failure is the 15 mNm load step moving the Z ~12 deg (limit 10) - the stiffness left to win
    back once the U9 noise is fixed.
  - `#SPIN` now goes to 12000 RPM (bench).
- **Board calibration stored (2026-11-20):** `#GSAVE` writes both motors' `#GCAL` gains and the `#OBS`
  constants to a 48-byte CRC-checked record at 0x0801F800 (the board file reserves the last 4 KB);
  it loads at every boot ("calibration loaded: ..."); `#GCLEAR` erases it; both refused with the drive
  on. The XY flasher reads it before its mass erase and writes it back after the image ("stored
  calibration found; it is kept across the erase" ... "calibration restored"). Tested on board 1:
  save, reset, clear, reset, save, full reflash - image CRC (`#FLASHCRC`) intact after every erase.
  Board 1's record holds M1 1.111 / 0.906 / 0.983, M2 0.889 / 1.076 / 1.035, R 0.32.
  - **Flash banks - check before production.** The G474RB arrived with DBANK = 1 (dual bank, OPTR
    0xFFEFF8AA). In that mode bank 2 starts at 0x08040000, not at 0x08010000 as the HAL's
    FLASH_BANK_SIZE (size / 2) implies: erasing "bank 2 page 31" left the record alone, erasing bank
    1 page 63 cleared it. So on a 128 KB part the firmware above 64 KB (it is ~103 KB) and the
    record sit in bank 1's upper half, which RM0440 most likely does not guarantee on this size in
    dual-bank mode. Single-bank mode (DBANK = 0: 128 KB contiguous, 4 KB pages - the layout the
    Arduino variant assumes) is the clean setting; changing it is an option-byte write plus a full
    reflash and a re-saved calibration. cal.cpp handles both modes. `#CALDBG` shows OPTR and the
    page the record's erase uses.
  - **Board 1 is now single bank** (`#DBANK 0`, then reflash: OPTR 0xFFAFF8AA, the record's page is
    bank 1 page 31). Re-saved and checked: `#GSAVE` ("single bank, 4 KB pages"), reset ("calibration
    loaded"), full reflash keeps it, image CRC intact, closed loop at 3000 RPM.
- **Step 7 passed: both motors to 24000 RPM in closed loop** (loose motors, 36.3 V, supply limit 6 A;
  2026-11-20). ~80 s at 24000 RPM in eight runs of 6-15 s, each a full 0 -> 24000 -> 0 cycle through
  the hand-over and the hand-back; at speed no trip after the changes below, Z error within -4.5..+7.9
  deg, board NTCs 33 C at most. Final build: two 24000 RPM runs and five 6000 RPM cycles, all clean.
  What it took:
  - **Voltage headroom.** The back-EMF meets the current loops' old limit (0.95 x Vbus / sqrt(3) = 20.0 V)
    below 24000 RPM. `modulate()` now fits the three duties into the whole 0..DUTY_MAX range: centred
    while they fit, shifted down together above that (the line voltages do not change; the bottom phase may
    sit at 0, low side on all period, nothing to dead-time-compensate). The limit becomes (DUTY_MAX - dead
    time) / sqrt(3) x Vbus = 20.4-20.5 V (`voltageLimit()`, `MAX_MODULATION` 1.0 of that). Voltage demand
    against the limit on motor 2 (the clean one) - motor 1 averages the same but its sense noise adds
    peaks:

    | RPM | 12000 | 15000 | 18000 | 20000 | 22000 | 24000 |
    |---|---|---|---|---|---|---|
    | motor 2 demand | 0.49 | 0.61 | 0.73 | 0.80 | 0.87 | 0.94 |
    | motor 1 peak | 0.65 | 0.78 | 0.90 | 0.99 | 1.06 (1-2 % of periods clipped) | 1.12 (6-7 % clipped) |

    The old limit would have stopped near 22500 RPM.
  - **Field weakening** (`#FW <A> [share]`, default 3 A from 0.95 of the limit): negative d current lowers
    the q voltage by we x L x |id| - only ~0.19 V per A at 24000 RPM. At 24000 motor 2 needs none; motor
    1's noise-inflated demand winds it up to 1.4-3 A. The simulation reaches 24000 with the model's heavier
    drag (16 mNm) at 0.97 of the limit with it, 0.997 without.
  - **PWM update moved to the peak.** The duties loaded at the valley, but every write of the closed loop's
    ~35 us interrupt came after it (`STAT volts ... after-valley` counts them: all of them), so voltages
    landed 2 periods after their sample where 1.5 were compensated, and the observer and the two-shunt
    rebuild used the wrong period's voltage and duties (7 deg of frame offset at 24000 RPM in the
    simulation). Now the update is on the peak right after the sample (`update on the peak` in the boot
    line): every write lands in time whatever the interrupt's length, the delay is exactly 1.5 periods,
    and the observer and the rebuild use the voltage and duties in force (two interrupts back). The
    simulation models all three (`UPDATE=peak|valley|late`); the sweep is no worse (70/75 with only the
    known stiffness checks failing, 0 slips, worst rotor 34 deg at spin-down against 41).
  - **Over-current trips raised (U9 noise; back down once U9 is fixed).** Motor 1's comparators broke three
    times in a few minutes at 15000-18000 RPM with 20 A / 0.38 us (and 0.75 us), once ~70 ms after the
    hand-over with ~2.5 V applied, where no real 20 A can build up: the 50 us post-mortem (`#BRKDUMP`)
    showed motor 1's three phase samples summing to +5..+8 A for 250 us before it - common mode, the U9
    signature. Idle, `#CMPTEST` found no latch even at 5 A (the idle spikes are all shorter than the
    filter), comparator highs at 20 A, none from 25 A. Now 30 A and 0.75 us (`HW_TRIP_A`, `BreakFilter`
    0xC): none since. The software trip then fired once at 24000 RPM (motor 1 phase A -15.4 A for 3
    periods, on ~2.5 A of real current: motor 1's largest sample per 2 ms runs median 9 A, p99 15 A there),
    so it is 18 A x 5 periods now. The AON7264E FETs take far more for microseconds.
  - **Hand-back seeded from the closed loop's state.** One run stopped 70 ms after the 1500 RPM hand-back
    with "a rotor fell 151 deg off its reference angle in open loop": motor 2 got ~1 A of the 3 A spin
    current and its observer then read 650 RPM. `handbackToOpen()` had seeded the open-loop trims from
    that instant's measured current and last commanded voltage, both carrying sense noise (Kp passes it
    into the voltage). It now uses the closed loop's current references and integrators plus feed-forward,
    turned into the reference frame by eps - as the hand-over already did. Z error just after the
    hand-back since: -17.8..+6.9 deg over seven cycles (slip limit 150).
  - Hardware quirk: a read-modify-write of TIMx_BDTR.BKF is ignored while BKE is set (LOCK off);
    `hw::setBreakFilter()` clears BKE for the write.
  - For G-code use (step 8): the 36 V config template still has `max_rpm: 14000` and the matching
    `speed_map`; the ramp to 24000 takes ~11 s (500 rad/s^2 to 14000, 150 above), so `spinup_ms` must cover
    it. Under cutting load the q voltage rises by R x Iq (0.32 V per A): above ~22000 RPM a real load will
    lean on field weakening, which costs ~5 A of d current per volt.
- New maintenance commands: `#NOISE [samples]`, `#SLOW <0|1|2>`, `#DUMP [samples] [channel]` (consecutive
  raw samples, 16 per line; `D <channel> <first sample> <values>`), `#BRK`, `#MOE`, `#LOAD <us>` (pad the
  interrupt, drive off), `#SPIN <rpm> <ms>` (bench spin that stops itself), `#RXLOG` (last 256 link
  bytes), `#PREV` (the previous boot's record: last main-loop step, longest interrupt, CPU fault).
  `#STAT` adds uptime, reset reason, overruns, longest interrupt since boot, main-loop rate, link
  counters. `#ALIGN <V> [ms]` takes a duration (max 5 s). `#GCAL [V]` per-phase gain calibration, `#GAIN` / `#GAIN1` / `#GAIN2`, `#JMEAS <rpm>` rotor inertia
  from a closed-loop speed step, `#GSAVE` / `#GCLEAR` stored calibration, `#FLASHCRC <len>` image
  CRC-32 (zlib's, compare with the .bin), `#CALDBG` flash option bytes / record words. A CPU fault now turns both bridges
  off and sends `ERR:spindle CPU fault ... pc=...` over the link (`addr2line -e firmware.elf <pc>`).
  For high speed: `#VLOG <ms>` (one line per interval: speeds, dq currents and voltages, voltage demand /
  peak against the limit, clipped periods, field weakening, comparator highs seen by the main loop),
  `STAT volts` (the same per `#STAT`), `#FW <A> [share]`, `#TRIP <sw A> [comparator A] [motor 2
  comparator A]` (comparators up to 40 A; echoes the settings), `#BKF <0-15>` break filter (drive off),
  `#CMPTEST <A> [ms]` (drive off: would the idle noise latch a break at this threshold?), `#BRKDUMP
  [first]` (the 16 interrupts before the last fault: raw phase currents, comparator outputs, duties).
  `#SPIN` now goes to `MAX_COMMAND_RPM` (24000).

## Board 2 results (2026-11-23, USB power only): U9 sync clock

Board 2 is a second v1.0 board (ESP32-S3 USB serial / MAC 14:63:93:28:62:6C, hostname `maslow`, 192.168.1.245
on the office WiFi) with the sync bodge from the board's REVISIONS.md: a 22 pF capacitor stood on R30 pad 1
(U9 RT/CLK) and a wire to U20 pin 57 (PB4).

- **Bring-up from blank.** Its ESP32-S3 was blank (ROM loop: `invalid header: 0xffffffff`, TG0WDT resets).
  Flashed over USB with esptool: `firmware.factory.bin` at 0x0 and `littlefs.bin` at 0x5F0000. Stage the
  filesystem first: with `PLATFORMIO_DATA_DIR=.pio/fsdata` (as `scripts/flash-maslow.sh` sets it) `buildfs`
  packs that folder, and an empty one gives an empty filesystem. Copy `FluidNC/data/*` into it. WiFi was set
  by the user through the board's `maslow` access point. Board id read 600 mV (unified36v); the config was
  created from `maslow-mini-36v.yaml`. The blank G474 took `$Spindle/G474=FLASH` first time (113 KB, ~25 s).
  Its flash is still dual bank (OPTR 0xFFEFF8AA, as shipped); board 1 was moved to single bank (above).
  No `#GCAL` record yet.
- **U9 runs on USB power.** VSUP is diode-ORed from VCC (D1) and USB 5 V (D2), so U9 switches from ~4.6 V
  with only USB connected (duty ~70 %, on-time ~1.35 us). Its alias shows on every sense channel at
  4.9-5.1 kHz (U9 at ~485 or ~495 kHz on 4.6 V; board 1 at 36 V: 480.7 kHz).
- **`#PINCAP`: the bodge is connected.** Rise time on the internal pull-up: PB4 571-576 ns, the unconnected
  PB3 / PB5 / PD2 171-235 ns, so PB4 carries ~3x their load. That fits the wire plus the capacitor into
  U9's RT/CLK pin, which is held at ~0.5 V while U9 runs. An open wire would add little; a short to ground
  would time out.
- **`#SYNC`: U9 locks.** 500 kHz on PB4 (TIM3_CH1), exactly 25 clocks per PWM period, started from TIM1's
  update at the peak. `#NOISE 4000`, drive off, sd in counts (0.0104 A/count):

  | | M1a | M1b | M1c | M2a | M2b | M2c |
  |---|---|---|---|---|---|---|
  | clock off | 34.4 | 125.2 | 22.7 | 36.0 | 21.6 | 14.9 |
  | clock on, rising edge 170 ticks after the peak (default) | 3.8 | 3.8 | 1.8 | 1.8 | 1.9 | 1.5 |

  Locking turns U9's ripple into a fixed offset per channel (up to ~240 counts on M1b at the worst phase),
  so the clock starts at boot before the zero is measured, `#SYNC` changes are refused while the drive runs,
  and every change re-measures the zero.
- **Phase sweep** (`#SYNC 1 <rise>` in 10-tick steps, `tools/syncsweep.py`): every channel's sd <= 5.8 for a
  rising edge 150-190 ticks after the peak, <= 7.1 at 140 and 240-260. U9's switch-on edge lands on the
  rank-1 samples at 290-20, its switch-off edge (1.35 us later on 4.6 V) at 60-110. ADC5's rank 2 (M2b) is
  ~120 ticks later and sees the same edges at 90-130 and 200-230. From 36 V the switch-off edge is only
  ~31 ticks after the switch-on edge, which predicts a quiet window of about 140-260: the default 170 is in
  both. A first build put the clock edge on the peak: the trigger starts TIM3 from 0 whatever CNT was
  preset to, so `syncStart` now measures the phase against TIM1 and moves the counter (error 0, spread 0).
- **Not tested yet: 36 V.** Next: 36 V, no motors, `#NOISE 4000` with `#SYNC 0` and `#SYNC 1`, `#CMPTEST`,
  and a phase sweep there (`python3 tools/syncsweep.py <ip> 100:300:10`). If it holds, the trips can go back to 20 A
  comparators and 15 A x 3 software (step 7 raised them for U9's noise) on boards with the clock.
- New commands: `#SYNC` (status), `#SYNC 1 [rise ticks]`, `#SYNC 0` (PB4 driven low); `#PINCAP [reps]`
  (drive off). PB4 is now driven low from `hw::init` (UCPD1 dead-battery pull-down disabled first).

## Verified without hardware

- `pio run` and `pio run -e g474-bench` build (87 KB of 128 KB flash, 61 KB of 128 KB RAM).
- `python3 spindle-g474/tools/check_pins.py`: 0 mismatches. It checks all 33 MCU pins used, the
  DRV8300 input pins, the XY ↔ G474 wiring (link, NRST, BOOT0), which shunt each sense input reads
  and that the shunt sits under that phase's low-side FET, the 5 mΩ shunts, the 1k/31.6k bias
  networks, the VBUS divider, the NTC pull-ups and the 10k/2.2k BoardVersion divider.
- Alternate functions come from the Arduino core's `PeripheralPins.c`; OPAMP VINP indices,
  comparator inputs and DAC pairings from ST's HAL headers.
- The XY firmware (`firmware/`, env `wifi_s3`) builds with the 36 V changes. Static RAM is unchanged
  against the unmodified branch (132,392 bytes both); flash +7.7 KB (94.6 % used).
- `test/sim` runs the real `foc.cpp` against a two-motor model: sign conventions, hand-overs, Z
  holding and load steps behave (README.md, "Simulation"); `test/sim/sweep.sh` repeats it over 75
  combinations of rotor inertia and motor-constant errors. A model is not the motor: the inertia,
  friction, sense noise and the real dead-time shape are guesses.

## Untested — the assumptions most likely to be wrong

| Area | Assumption | How it shows up if wrong | Checked in step |
|---|---|---|---|
| Clocks | HSI16 → 170 MHz (generic G474RB clock config) | no link output at all, or a wrong baud rate | 2 |
| Sampling | All five ADCs start on TIM1 TRGO2 (OC4REF, 20 ticks before the peak); ADC5 finishes last and raises the interrupt | `init failed: no current samples`; `#STAT` isr 0 | 2 |
| Update event | TIM1/TIM8 load new duties at the peak right after the sample (`update on the peak` in the boot line; the valley was missed by every closed-loop write, see step 7) | `init failed: PWM update event not on the peak` | 2 |
| OPAMP → ADC | OPAMP1→ADC1, 2→ADC2, 3→ADC3, 4+5→ADC5, 6→ADC4, PGA ×16 | a zero offset far from 2010 counts (`current-sense zero out of range`), or a phase reading another phase's current | 2, 4 |
| Sense sign | Positive phase current (into the motor) makes the reading FALL (`SENSE_SIGN = -1`) | `#ALIGN` gives ia < 0; with the current loop on, a runaway to the trip | 4 |
| DRV8300 | MODE/DT open = 6-input mode, non-inverted INL, 200 ns dead time; idle = all inputs low = all FETs off | FETs on when they should be off: smoke. Scope before connecting a motor | 3 |
| Comparator trip | VINP above the DAC threshold (30 A conduction-direction since step 7: motor 1's U9 noise broke 20 A) trips the break; break filter 0.75 µs | nuisance `hardware over-current` at turn-on or at speed (`#BRKDUMP`), or no trip at all | 3, 5, 7 |
| ISR time | both motors' FOC + observer in well under the 50 µs period | `#STAT` isr > ~35 µs; jitter, then current spikes at speed | 2, 6 |
| Open loop (I/f) | 2.5 A holds the Z, 3 A spins up a loaded spindle, +1 A for Z moves | rotors slip at spin-up or Z moves stall | 5, 8 |
| Observer | R 0.39 Ω, L 75 µH, flux 7.84 mWb (ESC1 bench, one loose motor, 24 V); locks within 10 % at 2000 RPM | `observer did not lock at the closed-loop hand-over speed` | 6 |
| Closed loop | current PI from the ESC1 (0.2/1000); angle PID 10 / 0.12 / 100 at and above 4000 RPM, chosen in the simulation with a GUESSED inertia of 1e-5 kg m² — never run | speed hunting, `observer lost lock`, `rotor fell ... off its reference` | 6, 8 |
| Low-speed closed loop | below 4000 RPM the angle loop's bandwidth is scaled down, to half at the 1500 RPM hand-back (`#POSBW 0.5 4000`). At full gains the sim limit-cycled between ~1500 and ~3000 RPM (±5 A bursts at ~25 Hz) and a third of the sweep slipped a rotor at the hand-back; scaled, none did. The real observer may be noisier than the model's | iq bursts in `#STAT` between 1500 and 4000 RPM; a slip stop on spin-down | 6 |
| Hand-back | at 1500 RPM open loop takes over on the same reference, carrying the angle loop's integral (the steady load); the sim's Z swing there is 5° nominal, up to ~30° (0.7 mm) with the motor constants off | a Z twitch when the spindle stops | 6, 8 |
| Open loop | voltage drive + 5 Hz current trim (`#OLKI`), dead-time compensation 250 ns (`#DTC`) | hold current off its target in `#STAT`; rotors ringing after Z moves | 5 |
| Rotor inertia | assumed 1e-5 kg m²; above ~1e-4 the 250 rad/s² open-loop spin-up stalls | `rotor fell ... off its reference angle in open loop` at spin-up | 6 |
| 36 V headroom | measured: 24000 RPM needs ~0.94 of the 20.4 V limit unloaded (step 7); firmware cap 24000; field weakening up to 3 A | Iq can no longer hold speed under load near 24000 RPM | 7, 8 |
| Bus/NTC scaling | 100k/6.8k divider, NCP18XH103 (B 3380) | `#STAT` vbus/temperature off a multimeter/thermometer | 2 |
| Fan output | VAC_CONTROL (PB6) is driven as 25 kHz 3.3 V push-pull PWM like the ESP32's fan pin, straight to CN10 pin 1; **CN10 pin 2 is 36 V**. If the vacuum's control input pulls up to 36 V, PB6 is damaged. Check the device before connecting it | — | 3 |
| XY flasher | AN3155 on USART2 at 115200 8E1: mass erase, 256-byte writes padded to 8 bytes, read-back compare; GPIO2 → BOOT0, GPIO1 → NRST | `$Spindle/G474=FLASH` fails with the step it stopped at; recover with SWD | 9 |
| ROM bootloader side effects | while in the bootloader the G474 also listens on USART3 (PC10/PC11 = INLA2/INLB2: low sides of motor 2 may turn on) and I2C (PC8 = INHC2, held down by 4.7k). Harmless at standstill; never flash with the motors turning | motor 2 brakes briefly on phase A/B | 9 |
| Trip recorder | 2 s at 2 ms, new columns (dq currents, observer rpm, Z error) in the existing `[TRIP]`/`TRIPCSV` envelope | `trip_analysis.py` (tools/maslow-mcp) does not know the columns: it needs updating to read them | 8 |
| XY heap | +4 bytes per SpindleBoard and one web command entry (`$Spindle/G474`, ESP814) permanently; ~0.4 KB more only while flashing | compare `$HEAP` against a pre-change build as in the heap notes | 9 |

Things the ESP32 firmware had that this one does not: the voltage LUT and its `CAL` sweep, the
load-sense / adaptive-voltage / guard diagnostics (closed-loop current control replaces them),
nFAULT monitoring (the DRV8300 has no fault pin), USB console commands, WiFi OTA. The `L:` status
bits now mean "Iq above 90 % of its limit for 200 ms" per motor.

## What you need

- **A USB-C cable that carries data**, for the XY ESP32-S3: USB1 goes straight to its built-in USB
  (U8 IO19/IO20) and has the 5.1 kΩ CC pull-downs, so a C-to-C cable works as well as A-to-C. By the
  schematic, USB alone also powers the 3.3 V rail - both MCUs - through D2 into the TPS54360B buck
  while the 36 V side stays dead (on the 24 V board it does), so steps 1-2.3 need no other supply.
  VSUP is then ~4.6 V, just above the buck's 4.5 V minimum: use a good port and a short cable.
- **An ST-LINK and a Tag-Connect cable** for the G474's SWD pad J1: a TC2030 no-legs footprint on the
  top side, 12 mm from the G474 (U20), wired in Tag-Connect's standard SWD order: 1 = 3.3 V,
  2 = SWDIO, 3 = NRST, 4 = SWCLK, 5 = GND (6 = SWO, not connected).
  - STLINK-V3MINIE (or STLINK-V3SET) with a **TC2030-CTX-NL-STDC14** cable (formerly sold as
    TC2030-CTX-NL-ST14). The legged TC2030-CTX-STDC14 does not fit: J1 has no holes for its clips.
  - Optional: a TC2030-CLIP holds the plug from under the board (the underside by J1 is clear).
    Without it, press the plug onto the pads for the few seconds a flash takes.
  - Or an ST-LINK/V2 (clones work) or a Nucleo board's on-board ST-LINK, with a TC2030-IDC-NL cable
    and jumper wires to the five signals above.
  - The probe does not power the board: keep the USB-C cable (or the 36 V supply) connected.
  - `pio run -t upload` drives the ST-LINK through OpenOCD; the board file also sets up `pio debug`.
  - Once the XY board runs this branch, `$Spindle/G474=FLASH` can update the G474 without the cable
    (step 9). That path is untested, and if it fails or the G474 does not start, SWD is the only
    other way in: the G474 has no USB.
- A current-limited 36 V bench supply (from step 2.4), a multimeter and an oscilloscope (step 3).

Buttons, all on the underside: SW1 = ESP32 BOOT (IO0), by the USB-C connector; SW2 = G474 reset and
SW3 = G474 BOOT0, by J1 (hold SW3 while tapping SW2 to start the G474's ROM bootloader).

## Bring-up

Steps 1 to 2.3 run on USB power alone, with the motor side unpowered. From step 2.4 to step 7 use a
current-limited 36 V bench supply. Keep the hardware E-stop/power switch in reach. **Run the first
spins with both motors loose on the bench, not in the spindle**: in the spindle any difference
between the motors moves the Z.

### 1. XY side first

On USB power, flash the XY board with this branch's firmware. A new board's ESP32-S3 is blank and
`pio run -e wifi_s3 -t upload` writes only the app (seen on the 24 V board), so the first time write
the whole image - bootloader, partition table, boot_app0 and app - then the filesystem, from
`firmware/`:

```
pio run -e wifi_s3
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port <port> write-flash 0x0 .pio/build/wifi_s3/firmware.factory.bin
pio run -e wifi_s3 -t uploadfs
```

`<port>` is the board's USB serial port (`/dev/cu.usbmodem…` on a Mac). After that, `-t upload` and
WiFi updates work as usual. On the 24 V board the first boot or two panicked on the empty settings
store, then it booted.

On a 36 V board its config must not put `uart1` on gpio.1/gpio.2: those are now the G474's NRST and
BOOT0, and UART traffic there would reset it. The firmware raises a config alarm if it finds that,
and copies `maslow-mini-36v.yaml` (no `uart1`) when there is no config yet. Check `$MBOARD` reports
the 36 V board (~0.60 V).

### 2. First power-up on USB, then 36 V, no motors connected

1. USB-C only, no 36 V: the bridges and gate drivers are unpowered, so nothing on the motor side can
   switch. Put the Tag-Connect plug on J1 and flash `g474-bench` over SWD
   (`pio run -e g474-bench -t upload` in `spindle-g474/`): nothing moves at boot in this build.
2. XY log should show `spindle reset reason: ...` then
   `spindle g474-0.1 ready: G474 FOC, PWM 20000 Hz, update on the valley`, then the link handshake
   and `T:IDLE,...` status lines. An `init failed: <why>` line names the peripheral that did not start.
3. `$Spindle/G474=SEND #STAT`: currents within ±0.1 A of 0 on all six phases, temperatures near
   room, isr below ~35 µs, vbus ~0 V. Anything that would drive the motors is refused for now
   (`... refused: bus 0.0 V outside 20-44 V`).
4. Connect the 36 V bench supply, limit 0.3 A. `#STAT`: vbus within 0.5 V of a multimeter on VCC
   (else fix `VBUS_DIVIDER`). Note the supply's idle current.

### 3. Gate drive, no motors

1. Scope GHx/GLx (or the INHx/INLx pins) of both bridges. At rest all six must be low.
2. `$Spindle/G474=SEND #ALIGN 0.3`: for 300 ms the bridges switch at 20 kHz. Check complementary
   gates, dead time (~0.3 µs total), phase A at the highest duty, and that nothing switches on
   either bridge at the PWM peak (that is where the ADCs sample).
3. Check what the CN10 vacuum device expects before plugging it in (see the table).

### 4. Sense sign and phase order, motors loose

1. Connect both motors, loose. Lower the trips first: `$Spindle/G474=SEND #TRIP 6 10` (board 1: not
   until the U9 noise is fixed, see above - it would trip on noise).
2. `#ALIGN 0.5`: per motor expect ia **positive**, roughly +0.5 V / R minus the dead-time loss
   (0.5-1.2 A), and ib ≈ ic ≈ −ia/2. The rotors snap to the phase A axis.
   - ia negative on both: `SENSE_SIGN` is wrong — fix it before anything else.
   - ib/ic not ≈ −ia/2: a sense input reads another phase; recheck the OPAMP/ADC mapping.

### 5. Open loop (I/f), motors loose

1. `$Spindle/G474=SEND #IHOLD 1.5`, then `$Spindle/G474=SEND S1000`. Both motors run open loop at
   1000 RPM. `#STAT`: id ≈ 3 A, iq ≈ 0, `obs` ≈ −1000 RPM on M1 and +1000 on M2 (or the reverse:
   that decides `SPINDLE_DIRECTION` against the real spindle), flux ≈ 1.0.
2. `$Spindle/G474=SEND S0`: ramps down and powers off.

### 6. Closed loop, motors loose

1. `S3000`: at 2000 RPM the XY log shows `closed loop: observers locked at 2000 RPM`, then the
   speed ramps on. `#STAT`: id → 0, small iq, observer speed on the command.
   - `observer did not lock`: check flux ≈ 1.0 in `#STAT` during the open-loop hold; adjust with
     `#OBS` (the ESP32 VSWEEP gave 7.28 mWb, the ESC1 7.84 on another motor).
2. Step 6000, 9000, 12000. Watch iq, the voltage (`v`) against vbus/√3 × 0.95, isr time.
   Measure the rotor inertia while you are there: at a steady speed, `S` a step of a few thousand
   RPM and read iq during the 500 rad/s² ramp: J ≈ 0.0118 × iq / 500 (kg m², minus friction).
   Rerun `test/sim` with it (`./sim <J>`, then `J=<J> ./sweep.sh`) before changing `#POS`.
3. Watch 1500-4000 RPM closely (`#LOG 100` while ramping 4000 → 0): iq bursts there are the weak
   spot the simulation found (the angle loop feeding on the observer's low-speed error). If they
   show, lower the low-speed bandwidth further (`#POSBW 0.35 5000`; it also softens the 2000 RPM
   hand-over, so watch the spin-up angle error), then check R/flux (`#OBS`) and `#DTC`. A higher
   `CL_ENTER_RPM`/`CL_EXIT_RPM` band did not help in the simulation.
4. `S1000`: `open loop below 1500 RPM`. `S0`: stops.
5. Load a rotor by hand (gloves, ESC1-style) at 6000: iq rises, speed holds, no trip.

### 7. Up to speed

Raise the supply limit (6 A was plenty: ~1-1.5 A at 24000 RPM loose). `#VLOG 200`, then `#SPIN` 15000,
18000, 20000, 22000, 24000 for a few seconds each, loose: watch the demand `u` against 1.0, clipped
periods, `fw`, the Z error. Passed on board 1 (results above). The trips are raised for the U9 noise
(30 A comparators, 18 A x 5 software): put them back (`HW_TRIP_A`, `SW_TRIP_*`) once U9 is fixed.

### 8. In the spindle

1. Flash the normal `g474` build; the Z homes at power-up. Check homing, tool load and removal,
   Z jogs at standstill (they use the open-loop hold current).
2. Spin at 6000, jog Z up and down while spinning: `#STAT` zerr stays within a few degrees and the
   Z moves the commanded distance (45° of phase per mm).
3. Air cut, then a light cut. `$Spindle/Trip=TEST` records and sends a test trip recording.

### 9. Flashing from the XY board

Upload `firmware.bin` to the XY board, then `$Spindle/G474=FLASH /firmware.bin` with the machine at
rest. The log reports erase, written and verified percentages and the restart (~20 s for 90 KB).
If it fails, SWD always recovers the board. Check `$HEAP` against the pre-change XY build.
