# Plasma Cutter Relay Configuration for Maslow 4

This guide explains how to configure your Maslow 4 (ESP32-S3) to control a plasma cutter using a Solid State Relay (SSR) connected to the AUX1 port.

## Hardware Setup

- **Board**: Maslow 4 (ESP32-S3)
- **Port**: AUX1 (GPIO 48)
- **Actuator**: Solid State Relay (SSR) controlling the plasma torch trigger
- **Logic Level**: 3.3V from ESP32-S3

## Configuration

### Basic Relay Spindle Configuration

Add the following to your `maslow.yaml` configuration file to enable relay control on GPIO 48:

```yaml
Relay:
  output_pin: gpio.48:low
```

### Configuration Options Explained

#### Pin Polarity Settings

The `:low` or `:high` suffix on the pin definition controls the active state:

- **`gpio.48:low`** (Recommended for SSRs):
  - Pin is LOW (0V) when relay is OFF (M5 command)
  - Pin goes HIGH (3.3V) when relay is ON (M3 command)
  - This is the **safest default** - relay stays OFF during boot/reset when pins default to LOW

- **`gpio.48:high`** (Active Low - use if your SSR requires inverted logic):
  - Pin is HIGH (3.3V) when relay is OFF (M5 command)
  - Pin goes LOW (0V) when relay is ON (M3 command)
  - **Warning**: Relay may trigger briefly during boot before configuration loads

#### Boot Safety Considerations

**Important**: During ESP32 boot, GPIO pins are in an undefined or LOW state until the firmware initializes. To prevent unexpected relay triggering:

1. **Use `gpio.48:low` configuration** - This ensures the relay stays off during boot since pins default to LOW
2. **Test your SSR** - Verify it requires a HIGH signal to activate
3. **Add external pull-down** - Consider adding a 10kΩ pull-down resistor on GPIO 48 for additional safety
4. **Wire your SSR** - Ensure the SSR control input expects a positive 3.3V signal to activate

### Advanced Configuration Options

For more control over relay behavior, you can add these optional parameters:

```yaml
Relay:
  output_pin: gpio.48:low
  enable_pin: NO_PIN              # Optional separate enable pin
  disable_with_s0: false          # If true, forces disable when S0 is commanded
  s0_with_disable: true           # If true, forces S0 when disabled
  tool_num: 0                     # Optional tool number (for tool changes)
  off_on_alarm: true              # Turn off relay on alarm condition
```

### Complete Configuration Example

Here's a complete example showing the relay configuration in context with other Maslow 4 settings:

```yaml
board: Maslow
name: Maslow S3 Board with Plasma Cutter

# ... other configuration settings ...

# Replace NoSpindle with Relay configuration
Relay:
  output_pin: gpio.48:low
  off_on_alarm: true

# ... rest of configuration ...
```

## G-code Commands

Once configured, use standard G-code commands to control the plasma cutter:

- **M3** or **M3 S1** - Turn plasma cutter ON (start cut)
- **M5** - Turn plasma cutter OFF (stop cut)
- **M4** - Turn plasma cutter ON (same as M3 for relay spindle)

The relay spindle treats any speed value greater than 0 as ON, and 0 as OFF.

### Example G-code Usage

```gcode
G0 Z5                ; Move to safe height
G0 X10 Y10           ; Rapid move to start position
M3                   ; Turn plasma cutter ON
G1 Z0.5 F100         ; Lower to cutting height
G1 X50 Y50 F500      ; Cut to end position
M5                   ; Turn plasma cutter OFF
G0 Z5                ; Raise to safe height
```

## Testing Your Configuration

### Step 1: Upload Configuration

1. Connect to your Maslow 4 web interface
2. Navigate to the Config tab
3. Edit your `maslow.yaml` file to include the Relay configuration
4. Save and reboot the machine

### Step 2: Test Relay Control

1. **Important**: Disconnect the plasma cutter from power before testing
2. Use a multimeter to measure voltage on AUX1 (GPIO 48):
   - Send `M5` command → Should read ~0V (LOW)
   - Send `M3` command → Should read ~3.3V (HIGH)
   - Send `M5` command → Should return to ~0V (LOW)
3. Once verified, connect your SSR and plasma cutter

### Step 3: Test Boot Behavior

1. With SSR connected (but plasma cutter unplugged), power cycle the Maslow 4
2. Observe if the SSR LED indicates activation during boot
3. If SSR activates during boot, verify your pin polarity configuration matches your SSR requirements

## Troubleshooting

### Relay Triggers During Boot

**Problem**: Relay activates briefly when Maslow 4 boots up.

**Solutions**:
- Change pin configuration from `:high` to `:low` (or vice versa)
- Add a 10kΩ pull-down resistor between GPIO 48 and GND
- Check if your SSR requires inverted logic (active-low vs active-high)

### Relay Doesn't Respond to M3/M5

**Problem**: Relay doesn't turn on/off with G-code commands.

**Solutions**:
- Verify GPIO 48 is correct for AUX1 on your board revision
- Check SSR control voltage requirements (some SSRs need 5V, not 3.3V)
- Use a multimeter to verify 3.3V is present on GPIO 48 when M3 is active
- Check for loose connections on the AUX1 connector

### Plasma Doesn't Fire

**Problem**: Relay clicks but plasma cutter doesn't fire.

**Solutions**:
- Verify plasma cutter is powered on and ready
- Check SSR current rating is sufficient for the plasma cutter trigger circuit
- Ensure SSR output is correctly wired to the plasma cutter trigger
- Test plasma cutter manually to verify it's working

## Safety Reminders

⚠️ **Important Safety Notes**:

1. **Test thoroughly** - Always test relay behavior during boot before connecting the plasma cutter
2. **Emergency stop** - Ensure your emergency stop button is functional and stops all operations
3. **Fire safety** - Never leave plasma cutter unattended during operation
4. **Proper grounding** - Ensure all equipment is properly grounded
5. **Ventilation** - Plasma cutting produces fumes; work in a well-ventilated area
6. **Eye protection** - Use appropriate welding helmet/goggles when plasma cutting

## Additional Resources

- FluidNC Wiki: http://wiki.fluidnc.com/
- Maslow Forums: https://forums.maslowcnc.com/
- FluidNC GitHub: https://github.com/bdring/FluidNC

## Technical Details

### Pin Specifications

- **GPIO 48** (ESP32-S3): Digital output, 3.3V logic level
- **Max Current**: 40mA (do not drive loads directly; always use SSR)
- **Boot State**: Defaults to LOW (input/floating) until firmware initializes

### Relay Spindle Implementation

The Relay spindle is implemented in:
- Header: `firmware/FluidNC/src/Spindles/RelaySpindle.h`
- Source: `firmware/FluidNC/src/Spindles/RelaySpindle.cpp`
- Base Class: `OnOffSpindle` provides the pin control logic

The relay spindle treats any non-zero speed command as ON and zero as OFF.
