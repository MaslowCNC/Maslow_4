# Spindle Motor Control Board

Dual BLDC motor control firmware for ESP32-S3 with two MP6541A three-phase power stages and the SimpleFOC library.

## Overview

This project provides open-loop control for two BLDC motors simultaneously using:
- **Microcontroller**: ESP32-S3
- **Motor Drivers**: Two Monolithic Power MP6541A (independent HS/LS inputs, driven as 6-PWM)
- **Control Library**: SimpleFOC
- **Current Sensing**: Analog current monitoring on all three phases of each motor (low-side)
- **Voltage Control**: Pre-measured calibration LUT for open-loop voltage-to-RPM mapping
- **Build System**: PlatformIO

## Hardware Configuration

### Pin Mapping

**Motor Driver U14 (Motor 1) - 6-PWM:**
- INHA: GPIO 18
- INLA: GPIO 9
- INHB: GPIO 8
- INLB: GPIO 10
- INHC: GPIO 3
- INLC: GPIO 14

**Motor Driver U15 (Motor 2) - 6-PWM:**
- INHA2: GPIO 21
- INLA2: GPIO 4
- INHB2: GPIO 47
- INLB2: GPIO 35
- INHC2: GPIO 48
- INLC2: GPIO 36

**Current Sense ADC - Motor 1 (U14):**
- CURA: GPIO 5
- CURB: GPIO 6
- CURC: GPIO 7

**Current Sense ADC - Motor 2 (U15):**
- CURA2: GPIO 15
- CURB2: GPIO 16
- CURC2: GPIO 17

**Driver fault / sleep:**
- nFAULT (U14): GPIO 11 (open drain, 5.1k pull-up)
- nFAULT (U15): GPIO 12 (open drain, 5.1k pull-up)
- nSLEEP (both drivers, shared): GPIO 13

**Other I/O:**
- Vacuum / fan PWM: GPIO 40
- Inter-board link to the XY board: RX GPIO 39, TX GPIO 38
- Z homing beam: IR LED GPIO 1, detector GPIO 2 (HIGH = beam interrupted)

## Required Software and Libraries

### Project Structure

```
Spindle-Motor-Control-Board/
├── platformio.ini              # PlatformIO configuration
├── src/
│   ├── main.cpp                # Setup, loop, fault monitoring, telemetry
│   ├── config.h                # Motor parameters and protection thresholds
│   ├── pins.h                  # Pin definitions for both motor drivers
│   ├── motor_controller.h/.cpp # MotorController class (driver init, FOC loop, current sensing)
│   ├── calibration.h/.cpp      # Voltage-LUT calibration state machine
│   └── serial_commands.h/.cpp  # Serial command handler
├── .gitignore                  # Git ignore file for PlatformIO
└── README.md                   # This file
```

### PlatformIO Setup

This project uses PlatformIO for building and uploading. PlatformIO will automatically download and manage all required libraries.

#### Installation Options

**Option 1: VS Code (Recommended)**
1. Install [Visual Studio Code](https://code.visualstudio.com/)
2. Install the PlatformIO IDE extension from the VS Code marketplace
3. Restart VS Code

**Option 2: Command Line**
1. Install Python 3.6 or later
2. Install PlatformIO Core: `pip install platformio`

### Libraries (Auto-installed by PlatformIO)

The `platformio.ini` file specifies these dependencies which will be automatically downloaded:

1. **SimpleFOC** (`askuric/Simple FOC @ 2.3.2`) - Core FOC control library
   - GitHub: https://github.com/simplefoc/Arduino-FOC

No manual library installation is required when using PlatformIO.

## Configuration

The firmware is configured in `src/config.h`:
- **Motor**: 1 pole pair BLDC motor
- **Supply Voltage**: 24V
- **PWM Frequency**: 20 kHz
- **Dead Time**: 2% (software/MCPWM - the MP6541A inserts none of its own)
- **Current Sense Gain**: 0.15 V/A (SOx / 11000 into the board's 3.3k/3.3k termination)
- **Overcurrent Trip**: 6.0 A phase-RMS (software); the MP6541A's own OCP is fixed at 16-20 A
- **Velocity Ramp Rate**: 20 rad/s per second

### Motor Parameters

Adjust these constants in `src/config.h` as needed for your motor:
```cpp
const int   POLE_PAIRS     = 1;     // Number of pole pairs
const float SUPPLY_VOLTAGE = 24.0f; // Supply voltage in volts
const float MAX_VOLTAGE    = 24.0f; // Maximum output voltage (the full bus)
```

## Building and Uploading

### Using VS Code with PlatformIO

1. Open the project folder in VS Code
2. PlatformIO will automatically detect the project and install dependencies
3. Connect your ESP32-S3 board via USB
4. Click the "Upload" button in the PlatformIO toolbar (right arrow icon)
   - Or use the command palette: `PlatformIO: Upload`

### Using PlatformIO CLI

```bash
# Build the project
pio run

# Upload to the board
pio run --target upload

# Open serial monitor
pio device monitor
```

### Board Configuration

The project is configured for `esp32-s3-devkitc-1` in `platformio.ini`. If you have a different ESP32-S3 variant, you can modify the `board` parameter. Available boards:
- `esp32-s3-devkitc-1` (default)
- `esp32s3box`
- `esp32-s3-wroom-1`
- See [PlatformIO ESP32 boards](https://docs.platformio.org/en/latest/boards/index.html#espressif-32) for more options

### First Build

On the first build, PlatformIO will:
1. Download the ESP32 platform tools
2. Download the SimpleFOC library
3. Compile the firmware

This may take several minutes. Subsequent builds will be much faster.

## Usage

After uploading, the firmware will:
1. Configure the PWM peripheral for both drivers (the MP6541A needs no register setup) and
   measure the zero-current level of the six current-sense inputs
2. Leave both drivers asleep (nSLEEP low) until a motor is commanded
3. Load pre-measured calibration LUT data for both motors
4. Wait for serial commands — motors do **not** start automatically

### Serial Commands

Open the Serial Monitor at **115200 baud** and use the following commands:

| Key | Action |
|-----|--------|
| `q` | Select Motor 1 (default) |
| `w` | Select Motor 2 (spins opposite direction) |
| `e` | Select both motors |
| `0`–`9` | Set velocity: `0` = 0 RPM, `1` = 2000 RPM, …, `9` = 18000 RPM |
| `+` | Step angle forward (active motor, angle-control mode) |
| `-` | Step angle backward (active motor, angle-control mode) |
| `r` | Toggle continuous rotation (active motor) |
| `s` | Reset angle to 0 and stop (active motor) |
| `p` | Advance relative phase between motors (+45°) |
| `l` | Retard relative phase between motors (−45°) |
| `i` | Print angle and status of active motor |
| `a` | Print status of all motors |
| `x` | Emergency stop: disable both motors, zero velocity |
| `C` | Auto-calibrate voltage LUT (both motors sequentially) |
| `Q` | Manual calibration — Motor 1 |
| `W` | Manual calibration — Motor 2 |
| `B` | Resume manual calibration from first unrecorded step |

During manual calibration, use `t`/`g` (±0.05 V) and `y`/`h` (±0.01 V) to adjust hunt voltage, then `SPACE` to record and advance to the next step.

### Telemetry Output

Telemetry is printed every 500 ms in the following format:

```
  M1 RPM  M1 Lim(V)  M1 Curr(A)    M2 RPM  M2 Lim(V)  M2 Curr(A)
   3000       2.83       0.450      3000       2.91       0.462
```

### Driver Status Monitoring

The MP6541A has no status registers — each driver reports faults on a single open-drain
nFAULT pin, and the firmware tells the two fault types apart by their shape:

- **Over-current**: the driver turns the outputs off and retries after ~2 ms, so nFAULT
  produces a burst of falling edges (counted by an interrupt handler). Persisting for 500 ms
  stops the spindle — see *Overcurrent Faults* below.
- **Over-temperature**: nFAULT is held low continuously until the die cools. Sustained for
  300 ms this latches a fault and alarms the XY board.

Over-voltage and UVLO are **not** reported on nFAULT by this part, so they cannot be observed
in firmware; the driver still protects itself in hardware.

### Voltage Calibration LUT

The firmware uses a 180-entry look-up table (LUT) mapping speed (100–18000 RPM in 100 RPM steps)
to a drive voltage. Pre-measured default values are included in `main.cpp`; they only cover
100–14000 RPM, so the entries above that repeat the last measured value until a calibration
sweep fills them in. Running auto-calibration (`C`) replaces the whole table for your motors.

Note that the LUT belongs to the PWM carrier and the current target it was measured at — change
either and the table should be rebuilt. A stored calibration in NVS is also sized by
`CAL_LUT_SIZE`, so changing the LUT length makes the board ignore any previously saved sweep and
fall back to the compiled-in defaults.

## Troubleshooting

### Compilation Errors

**Missing library errors:**
- PlatformIO should automatically download libraries on first build
- If libraries fail to download, try: `pio lib install` in the project directory
- Check your internet connection

**ESP32 platform not found:**
- PlatformIO will download the ESP32 platform automatically
- Manual installation: `pio platform install espressif32`

### Upload Issues

**Port not found:**
- Ensure the ESP32-S3 is connected via USB
- List available ports: `pio device list`

**Upload fails:**
- Hold the BOOT button while uploading (if your board requires it)
- Try reducing upload speed in `platformio.ini`: `upload_speed = 115200`

### Motor Not Running

1. Send a velocity command (`1`–`9`) via the Serial Monitor
2. Check the console for nFAULT (over-current / over-temperature) messages
3. Verify all power connections and supply voltage (24V)
4. Check nSLEEP (GPIO 13) goes high when a motor is commanded
5. Verify motor phase connections (A, B, C)

### Overcurrent Faults

The motors run open-loop, so an over-current means the rotor has already lost synchronisation
with the commanded field. Spinning it straight back up cannot recover that, so the firmware
does **not** retry — it stops and waits for the operator:

- Both motors stop at 0 RPM, from either the software trip (`OVERCURRENT_THRESHOLD`, 6.0 A
  phase-RMS) or the MP6541A's own hardware OCP reported on nFAULT.
- Fault code **2** is latched, which alarms the XY board and stops a running job. It stays
  latched until you send a new speed command — that is the deliberate restart.
- The Z reference is invalidated. Z position is the relative phase between the two motors, so a
  slip makes it meaningless: **Z targets are refused until a homing cycle runs** (`G`), and the
  board reports state "Needs Homing". Homing is deliberately still allowed while the fault is
  latched, since it is how you recover.
- If it keeps tripping, check motor wiring, the 24 V supply's current limit, or reduce the
  target RPM.

## License

This project is open source. Please check the repository for license details.

## References

- [SimpleFOC Documentation](https://docs.simplefoc.com/)
- [MP6541A Datasheet](https://www.monolithicpower.com/en/mp6541a.html)
- [ESP32-S3 Technical Reference](https://www.espressif.com/en/products/socs/esp32-s3)
