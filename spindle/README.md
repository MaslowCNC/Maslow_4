# Spindle Motor Control Board

Dual BLDC motor control firmware for ESP32-S3 with two DRV8316 motor drivers and SimpleFOC library.

## Overview

This project provides open-loop control for two BLDC motors simultaneously using:
- **Microcontroller**: ESP32-S3
- **Motor Drivers**: Two Texas Instruments DRV8316 (6-PWM mode with SPI configuration)
- **Control Library**: SimpleFOC with SimpleFOCDrivers
- **Current Sensing**: Analog current monitoring on all three phases of each motor
- **Voltage Control**: Pre-measured calibration LUT for open-loop voltage-to-RPM mapping
- **Build System**: PlatformIO

## Hardware Configuration

### Pin Mapping

**Motor Driver U1 (Motor 1) - 6-PWM Mode:**
- INHA: GPIO 18
- INLA: GPIO 9
- INHB: GPIO 8
- INLB: GPIO 10
- INHC: GPIO 3
- INLC: GPIO 14
- SPI CS: GPIO 43

**Motor Driver U13 (Motor 2) - 6-PWM Mode:**
- INHA2: GPIO 21
- INLA2: GPIO 4
- INHB2: GPIO 47
- INLB2: GPIO 35
- INHC2: GPIO 48
- INLC2: GPIO 36
- SPI CS2: GPIO 44

**Current Sense ADC - Motor 1 (U1):**
- CURA: GPIO 5
- CURB: GPIO 6
- CURC: GPIO 7

**Current Sense ADC - Motor 2 (U13):**
- CURA2: GPIO 15
- CURB2: GPIO 16
- CURC2: GPIO 17

**SPI Bus (shared):**
- MOSI: GPIO 11
- SCK: GPIO 12
- MISO: GPIO 13

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

2. **SimpleFOCDrivers** (`simplefoc/SimpleFOCDrivers @ 1.0.8`) - Extended driver support including DRV8316
   - GitHub: https://github.com/simplefoc/Arduino-FOC-drivers

No manual library installation is required when using PlatformIO.

## Configuration

The firmware is configured in `src/config.h`:
- **Motor**: 1 pole pair BLDC motor
- **Supply Voltage**: 24V
- **PWM Frequency**: 60 kHz
- **Dead Time**: 2%
- **Overvoltage Protection**: 32V threshold
- **Current Sense Gain**: 0.25 V/A
- **Overcurrent Trip**: 5.0 A phase-RMS (6.0 A during calibration)
- **Velocity Ramp Rate**: 20 rad/s per second

### Motor Parameters

Adjust these constants in `src/config.h` as needed for your motor:
```cpp
const int   POLE_PAIRS     = 1;     // Number of pole pairs
const float SUPPLY_VOLTAGE = 24.0f; // Supply voltage in volts
const float MAX_VOLTAGE    = 12.0f; // Maximum output voltage
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
2. Download SimpleFOC and SimpleFOCDrivers libraries
3. Compile the firmware

This may take several minutes. Subsequent builds will be much faster.

## Usage

After uploading, the firmware will:
1. Initialize both DRV8316 drivers over the shared SPI bus
2. Configure motor parameters and protection settings for each driver
3. Load pre-measured calibration LUT data for both motors
4. Wait for serial commands — motors do **not** start automatically

### Serial Commands

Open the Serial Monitor at **115200 baud** and use the following commands:

| Key | Action |
|-----|--------|
| `q` | Select Motor 1 (default) |
| `w` | Select Motor 2 (spins opposite direction) |
| `e` | Select both motors |
| `0`–`9` | Set velocity: `0` = 0 RPM, `1` = 1000 RPM, …, `9` = 10000 RPM |
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

### DRV8316 Status Monitoring

The firmware continuously monitors and reports:
- Fault conditions
- Overcurrent protection (per phase, high and low side)
- Overvoltage protection
- Overtemperature warnings
- SPI communication errors
- Buck regulator errors
- Power-on-reset status

### Voltage Calibration LUT

The firmware uses a 100-entry look-up table (LUT) mapping speed (100–10000 RPM in 100 RPM steps) to a drive voltage. Pre-measured default values are included in `main.cpp`. Running auto-calibration (`C`) will replace these values for your specific motors.

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
2. Check DRV8316 status output for fault conditions
3. Verify all power connections and supply voltage (24V)
4. Check SPI communication is working (no SPI errors in status)
5. Verify motor phase connections (A, B, C)

### Overcurrent Faults

If an overcurrent fault trips both motors:
- The fault threshold is `OVERCURRENT_THRESHOLD` (5.0 A phase-RMS)
- Send any velocity command to re-enable the motors after a fault
- If faults persist, check motor wiring or reduce the target RPM

## License

This project is open source. Please check the repository for license details.

## References

- [SimpleFOC Documentation](https://docs.simplefoc.com/)
- [SimpleFOCDrivers Library](https://github.com/simplefoc/Arduino-FOC-drivers)
- [DRV8316 Datasheet](https://www.ti.com/product/DRV8316)
- [ESP32-S3 Technical Reference](https://www.espressif.com/en/products/socs/esp32-s3)
