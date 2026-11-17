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
| Standstill Z hold / moves, spin-up | voltage vector from the LUT, +1 V while the Z moves | current vector (I/f): 2.5 A hold, 3 A spin-up, +1 A while the Z moves |
| Running | open loop | closed loop above 2000 RPM on the MXLEMMING flux observer (ESC1 bench), speed loop per motor, Z loop on the rotors' angle sum |
| Current sense | ESP32 ADC on the MP6541A SOx | 6 shunts → OPAMP1-6 (PGA ×16) → ADC1-5 injected, sampled at the PWM peak, two-shunt rebuild of the highest-duty phase |
| Over-current | software RMS (6 A), driver OCP via nFAULT | comparators COMP1/2/4 + COMP6/7/5 (DAC3/DAC4 thresholds, 20 A) into the TIM1/TIM8 break inputs; software 15 A × 3 periods per sample; 9 A RMS for 200 ms |
| Other protection | nFAULT over-temperature | bus voltage 20-44 V, two NTCs (85 °C warn, 100 °C stop), observer lock and speed, closed-loop Z error, independent watchdog |
| Fault codes (`F:`) | 1 = driver fault, 2 = over-current | 1 = bus voltage / temperature / init failure, 2 = any over-current or lost rotor |
| Console | USB CDC | the link only (no USB on the G474) |
| Update | WiFi OTA | XY board through the ROM bootloader, or SWD |

The Z/tool state machine (power-up homing, tool load and removal on the top-of-travel beam), the
hold release (`D`), the dust fan (`C`, `B`), the latched fault semantics and the Z refusal after a
fault are ported line for line from `spindle/src/main.cpp` and `serial_commands.cpp`.

### Angles and the Z

The Z is the relative angle of the two rotors (45° of phase per mm). As on the ESP32, motor *i*'s
commanded open-loop angle is `dir_i · α + φ + c_i` (α = integrated spin angle, φ = phase offset = Z,
`c_i` a constant). The motors turn in opposite senses, so φ adds to both and their angle sum
is `2φ + const`. In closed loop the firmware keeps the **sum of the two observers' angles** at
`2φ + const` with a proportional Z loop on top of the φ ramp's feed-forward; at the hand-back to
open loop the open-loop angles restart where the currents are, so the Z does not jump.

### Files

| File | |
|---|---|
| `src/board.h` | pin map (from the netlist) and fixed hardware facts: shunt chain, dividers, PWM |
| `src/config.h` | tunables, each marked BENCH (measured on the ESC1 or ESP32 bench) or GUESS |
| `src/hw.cpp` | peripheral set-up: TIM1/TIM8, OPAMPs, ADCs, COMP/DAC break, slow analog, fan, watchdog |
| `src/foc.cpp` | the 20 kHz interrupt: sensing, observer, current/speed/Z loops, SVPWM, hand-overs |
| `src/main.cpp` | link protocol, state machine, supervision and protection, status |
| `src/trip.cpp` | trip recorder (2 s at 2 ms) sent over the link like the ESP32's |
| `tools/check_pins.py` | netlist check |

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
| `#CUR kp ki`, `#VEL kp ki`, `#ZKP k` | current, speed and Z loop gains |
| `#IHOLD A`, `#ISPIN A`, `#IBOOST A`, `#ILIM A` | open-loop hold / spin-up / Z-move boost currents, closed-loop Iq limit |
| `#OBS R L flux` | observer motor constants (Ω, H, Wb) |
| `#TRIP sw_A [hw_A]` | software per-sample trip and comparator threshold |
| `#VER` | firmware version |
