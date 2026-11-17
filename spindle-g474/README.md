# Spindle firmware for the 36 V Unified Control Board (STM32G474)

The 36 V Unified Control Board (branch `36V-G474-spindle`, `Unified Control Board/`, sheet
"Spindle Drive") replaces the spindle ESP32 and its MP6541A drivers with an **STM32G474RBT3**
(U20), two **DRV8300** gate drivers (U21, U22) and twelve AON7264E FETs with 5 mΩ low-side shunts.
This firmware runs both spindle motors on it, sensorless, and speaks the ESP32 spindle board's
link protocol unchanged, so the XY board needs no protocol change.

**None of it has run on hardware yet.** Read [BRINGUP.md](BRINGUP.md) before powering a board.

## Build and flash

```
cd spindle-g474
pio run                    # env g474: the normal firmware (homes the Z at power-up)
pio run -e g474-bench      # same, but nothing moves at boot (first power-up, bench work)
pio run -t upload          # ST-LINK on the TC2030 pad J1 (needs the sandbox off)
```

Arduino STM32 core 2.12 (`framework-arduinoststm32@4.21200.0`, the one the ESC1 bench used) on
platform `ststm32@20.0.0`; the board definition is `boards/maslow_g474rb.json` (generic G474RB,
HSI16 → 170 MHz, no crystal). Use PlatformIO Core 6.2 (`~/.platformio/penv/bin/pio`).

From the XY board, once the 36 V XY firmware is on it: upload `.pio/build/g474/firmware.bin` to its
local filesystem (WebUI file manager), then `$Spindle/G474=FLASH /firmware.bin`. The XY board puts
the G474 in its ROM bootloader (GPIO2 → BOOT0, GPIO1 → NRST), mass-erases, writes, reads back,
compares and restarts it. The machine must be at rest with the spindle off.

`tools/check_pins.py` checks this firmware's pin map and the component values it assumes against
the KiCad netlist (needs `kicad-cli`).

## What it does

| | ESP32 spindle (`spindle/`) | G474 spindle (this) |
|---|---|---|
| Link | USART 115200 8N1, `S Z E ? H C B D G R T W` in, `T:...` status every 50 ms, `ZHOMED`, `MSG:/WARN:/ERR:`, trip recording | identical; `W` (WiFi OTA) answers `OTA_ERR:` |
| Commutation | SimpleFOC open loop in a FreeRTOS task, calibrated voltage LUT | FOC in the ADC interrupt at 20 kHz (`foc.cpp`) |
| Standstill Z hold / moves, spin-up | voltage vector from the LUT, +1 V while the Z moves | current vector (I/f): 2.5 A hold, 3 A spin-up, +1 A while the Z moves, driven as a voltage (R·I + back-EMF feed-forward) with a slow trim onto the current, so the rotors keep the back-EMF damping the ESP32 drive had |
| Running | open loop | closed loop above 2000 RPM on the MXLEMMING flux observer (ESC1 bench): each rotor's angle is held on its reference by a PID (the Z is the rotors' relative angle) |
| Current sense | ESP32 ADC on the MP6541A SOx | 6 shunts → OPAMP1-6 (PGA ×16) → ADC1-5 injected, sampled at the PWM peak, two-shunt rebuild of the highest-duty phase |
| Over-current | software RMS (6 A), driver OCP via nFAULT | comparators COMP1/2/4 + COMP6/7/5 (DAC3/DAC4 thresholds, 20 A) into the TIM1/TIM8 break inputs; software 15 A × 3 periods per sample; 9 A RMS for 200 ms |
| Other protection | nFAULT over-temperature | bus voltage 20-44 V, two NTCs (85 °C warn, 100 °C stop), observer lock and speed, rotor slip (angle off its reference: 90° closed loop, 150° open loop above 1000 RPM), independent watchdog |
| Fault codes (`F:`) | 1 = driver fault, 2 = over-current | 1 = bus voltage / temperature / init failure, 2 = any over-current or lost rotor |
| Console | USB CDC | the link only (no USB on the G474) |
| Update | WiFi OTA | XY board through the ROM bootloader, or SWD |

The Z/tool state machine (power-up homing, tool load and removal on the top-of-travel beam), the
hold release (`D`), the dust fan (`C`, `B`), the latched fault semantics and the Z refusal after a
fault are ported line for line from `spindle/src/main.cpp` and `serial_commands.cpp`.

### Angles and the Z

The Z is the relative angle of the two rotors (45° of phase per mm). As on the ESP32, motor *i*'s
reference angle is `ref_i = dir_i · α + φ + c_i` (α = integrated spin angle, φ = phase offset = Z,
`c_i` a constant). The motors turn in opposite senses, so φ adds to both and their angle sum is
`2φ + const`. **Both modes drive the same reference**: open loop puts the current vector on it,
closed loop steers each observed rotor angle onto it with a PID (the starting error at the
hand-over is the open-loop load angle, which the loop then removes). So the Z is held in angle,
not just in speed, and nothing is re-zeroed at either hand-over.

The phase ramp is trapezoidal (400°/s, 4000°/s²): a speed step rings the rotors on their current
vectors. The XY board's own Z planning (10 mm/s² = 450°/s²) is well inside it.

## Simulation

`test/sim` compiles the real `src/foc.cpp` on the host against two PMSM models (R-L-back-EMF per
phase, rigid rotor with viscous friction, 300 ns dead time, 1.5 LSB sense noise, the PWM's
half-period delay) and runs: hold, a 90° standstill Z move, spin-up through the hand-over to
6000 RPM, a Z move while spinning, 15 and 36 mNm load steps on one motor, 18000 RPM, spin-down
through the hand-back. It checks speeds, senses and the physical Z (half the sum of the rotor
angles) against the phase offset throughout.

```
cd spindle-g474/test/sim
c++ -std=c++17 -O2 -I. -I../../src sim_main.cpp ../../src/foc.cpp -o sim
./sim [J_kgm2] [flux_factor] [R_factor]      # model motor vs. the firmware's constants
TRACE=23.3,24.2 ./sim                         # a state line every 5 ms in that window
POS=10,0.12,100 OLKI=12 ./sim                 # try gains
```

Results (2026-11-17): all checks pass with the firmware's own constants, for rotor inertias
3e-6 to 5e-5 kg m² (the real one is unknown; 1e-5 assumed), and for a flux or R off by 7-30 %
one at a time. Z error at steady speed < 0.2°, peak 5° of phase (0.1 mm) on a 36 mNm load step.
Weak spots it shows: closed loop between ~1500 and ~3000 RPM jitters (±10 % speed, ±4 A) and the
hand-back leaves a transient Z swing of up to ~50° (1 mm) that settles in ~0.2 s; with the real R
20 % below `PHASE_R_OHM` **and** a second error (very low inertia or flux 7 % low) it trips there.
An inertia of 1e-4 kg m² stalls the open-loop spin-up (caught as a slip). These are what to look
at first on the bench (BRINGUP.md).

### Files

| File | |
|---|---|
| `src/board.h` | pin map (from the netlist) and fixed hardware facts: shunt chain, dividers, PWM |
| `src/config.h` | tunables, each marked BENCH (measured on the ESC1 or ESP32 bench) or GUESS |
| `src/hw.cpp` | peripheral set-up: TIM1/TIM8, OPAMPs, ADCs, COMP/DAC break, slow analog, fan, watchdog |
| `src/foc.cpp` | the 20 kHz interrupt: sensing, observer, current and angle loops, SVPWM, hand-overs |
| `src/main.cpp` | link protocol, state machine, supervision and protection, status |
| `src/trip.cpp` | trip recorder (2 s at 2 ms) sent over the link like the ESP32's |
| `tools/check_pins.py` | netlist check |
| `test/sim/` | host simulation of `foc.cpp` (see above) |

## Maintenance commands

Lines starting with `#` are maintenance commands (the ESP32 firmware ignores them). From the XY
board: `$Spindle/G474=SEND #STAT`. Replies arrive in the XY log as `MSG:` lines. Settings are lost
at reset.

| Command | |
|---|---|
| `#STAT` | mode, speeds, phase, Z error, bus, temperatures, ISR time; per motor currents, dq, voltages, observer speed and flux |
| `#LOG <ms>` | `#STAT` every `<ms>` (0 = off) |
| `#ALIGN [V]` | bring-up: a fixed voltage (default 0.5 V) on phase A's axis of both motors for 300 ms, then `#STAT` (sign/phase check) |
| `#CL 0\|1` | allow the closed-loop hand-over (1 = default; 0 = stay open loop at any speed) |
| `#CUR kp ki` | closed-loop current PI |
| `#POS kp kd ki` | closed-loop angle PID (A/rad, A per rad/s, A/(rad·s)) |
| `#OLKI ki` | open-loop current trim integrator (V/(A·s)) |
| `#DTC ns` | dead-time compensation (0 = off) |
| `#IHOLD A`, `#ISPIN A`, `#IBOOST A`, `#ILIM A` | open-loop hold / spin-up / Z-move boost currents, closed-loop Iq limit |
| `#OBS R L flux` | observer motor constants (Ω, H, Wb) |
| `#TRIP sw_A [hw_A]` | software per-sample trip and comparator threshold |
| `#VER` | firmware version |
