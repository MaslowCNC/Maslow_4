# 36 V spindle (STM32G474) — what is untested, and a safe bring-up

Written 2026-11-17 with no board in hand: everything here compiles and the pin map matches the
netlist, but **no line of it has run on hardware**. Work through the steps in order; each one checks
an assumption the next one relies on. Stop at the first surprise.

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
| Update event | TIM1/TIM8 load new duties at the valley (`update on the valley` in the boot line) | `init failed: PWM update event not on the valley` | 2 |
| OPAMP → ADC | OPAMP1→ADC1, 2→ADC2, 3→ADC3, 4+5→ADC5, 6→ADC4, PGA ×16 | a zero offset far from 2010 counts (`current-sense zero out of range`), or a phase reading another phase's current | 2, 4 |
| Sense sign | Positive phase current (into the motor) makes the reading FALL (`SENSE_SIGN = -1`) | `#ALIGN` gives ia < 0; with the current loop on, a runaway to the trip | 4 |
| DRV8300 | MODE/DT open = 6-input mode, non-inverted INL, 200 ns dead time; idle = all inputs low = all FETs off | FETs on when they should be off: smoke. Scope before connecting a motor | 3 |
| Comparator trip | VINP above the DAC threshold (20 A conduction-direction) trips the break; break filter ~0.38 µs ignores switching ringing | nuisance `hardware over-current` at turn-on, or no trip at all | 3, 5 |
| ISR time | both motors' FOC + observer in well under the 50 µs period | `#STAT` isr > ~35 µs; jitter, then current spikes at speed | 2, 6 |
| Open loop (I/f) | 2.5 A holds the Z, 3 A spins up a loaded spindle, +1 A for Z moves | rotors slip at spin-up or Z moves stall | 5, 8 |
| Observer | R 0.39 Ω, L 75 µH, flux 7.84 mWb (ESC1 bench, one loose motor, 24 V); locks within 10 % at 2000 RPM | `observer did not lock at the closed-loop hand-over speed` | 6 |
| Closed loop | current PI from the ESC1 (0.2/1000); angle PID 10 / 0.12 / 100 at and above 4000 RPM, chosen in the simulation with a GUESSED inertia of 1e-5 kg m² — never run | speed hunting, `observer lost lock`, `rotor fell ... off its reference` | 6, 8 |
| Low-speed closed loop | below 4000 RPM the angle loop's bandwidth is scaled down, to half at the 1500 RPM hand-back (`#POSBW 0.5 4000`). At full gains the sim limit-cycled between ~1500 and ~3000 RPM (±5 A bursts at ~25 Hz) and a third of the sweep slipped a rotor at the hand-back; scaled, none did. The real observer may be noisier than the model's | iq bursts in `#STAT` between 1500 and 4000 RPM; a slip stop on spin-down | 6 |
| Hand-back | at 1500 RPM open loop takes over on the same reference, carrying the angle loop's integral (the steady load); the sim's Z swing there is 5° nominal, up to ~30° (0.7 mm) with the motor constants off | a Z twitch when the spindle stops | 6, 8 |
| Open loop | voltage drive + 5 Hz current trim (`#OLKI`), dead-time compensation 250 ns (`#DTC`) | hold current off its target in `#STAT`; rotors ringing after Z moves | 5 |
| Rotor inertia | assumed 1e-5 kg m²; above ~1e-4 the 250 rad/s² open-loop spin-up stalls | `rotor fell ... off its reference angle in open loop` at spin-up | 6 |
| 36 V headroom | 0.82 V per 1000 RPM back-EMF: ~22k RPM before the voltage limit; firmware cap 18k | Iq can no longer hold speed above some RPM | 7 |
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

## Bring-up

Use a current-limited 36 V bench supply for steps 1-7. Keep the hardware E-stop/power switch in
reach. **Run the first spins with both motors loose on the bench, not in the spindle**: in the
spindle any difference between the motors moves the Z.

### 1. XY side first

Flash the XY board with this branch's firmware. On a 36 V board its config must not put `uart1` on
gpio.1/gpio.2: those are now the G474's NRST and BOOT0, and UART traffic there would reset it. The
firmware raises a config alarm if it finds that, and copies `maslow-mini-36v.yaml` (no `uart1`) when
there is no config yet. Check `$MBOARD` reports the 36 V board (~0.60 V).

### 2. First power-up, no motors connected

1. Supply limit 0.3 A. Flash `g474-bench` over SWD (`pio run -e g474-bench -t upload`): nothing moves
   at boot in this build.
2. XY log should show `spindle reset reason: ...` then
   `spindle g474-0.1 ready: G474 FOC, PWM 20000 Hz, update on the valley`, then the link handshake
   and `T:IDLE,...` status lines. An `init failed: <why>` line names the peripheral that did not start.
3. `$Spindle/G474=SEND #STAT`: currents within ±0.1 A of 0 on all six phases, vbus within 0.5 V of a
   multimeter on VCC (else fix `VBUS_DIVIDER`), temperatures near room, isr below ~35 µs.

### 3. Gate drive, no motors

1. Scope GHx/GLx (or the INHx/INLx pins) of both bridges. At rest all six must be low.
2. `$Spindle/G474=SEND #ALIGN 0.3`: for 300 ms the bridges switch at 20 kHz. Check complementary
   gates, dead time (~0.3 µs total), phase A at the highest duty, and that nothing switches on
   either bridge at the PWM peak (that is where the ADCs sample).
3. Check what the CN10 vacuum device expects before plugging it in (see the table).

### 4. Sense sign and phase order, motors loose

1. Connect both motors, loose. Lower the trips first: `$Spindle/G474=SEND #TRIP 6 10`.
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

Raise the supply limit. 15000, then 18000 RPM, loose. Note where the voltage limit is reached.
Raise the trips back (`#TRIP 15 20`, or reflash).

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
