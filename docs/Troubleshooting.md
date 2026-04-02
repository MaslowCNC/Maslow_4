# Troubleshooting Guide Maslow 4

## LED Indicators

The Maslow 4 control board has two indicator LEDs: a **Red LED** and a **WiFi LED**. Their blink patterns tell you what the machine is doing.

### Red LED

The Red LED signals error conditions that require attention. There are two distinct blink patterns:

#### Slow Blink (300 ms on / 300 ms off) — Error / Emergency Stop

The Red LED blinks slowly when `Maslow.error` is set to `true`. This state persists until the machine is power-cycled. The machine will not respond to movement commands while in this state.

Possible causes:

| Cause | Details |
| --- | --- |
| **Encoder not found** | One or more belt-tension encoders could not be detected on the I²C bus at startup. Check encoder wiring and connectors. |
| **Motor not found** | One or more motor drivers did not respond during the startup self-test. Check motor wiring and the motor driver board. |
| **Emergency stop command** | The `$ESTOP` command was sent (e.g. via the web interface E-Stop button). |
| **Position error > 15 mm** | While running a G-code job, an axis drifted more than 15 mm from its target position for more than 5 consecutive checks. This usually indicates a belt that has gone slack, a broken or disconnected belt, or a mechanical obstruction. |

**Recovery:** Power the machine off and back on. Investigate the error message that was printed in the console/log when the LED started blinking—it will name the specific axis and cause.

#### Rapid Double-Blink (100 ms on / 100 ms off / 100 ms on / 800 ms pause) — Watchdog Fired

Both the **Red LED and the WiFi LED** blink together in a rapid double-blink pattern. This means the firmware's internal motion-control watchdog fired: the main `update()` loop was not called for more than 100 ms, which indicates the processor was blocked on another task (e.g. a large file write or a network operation) long enough that motor safety could not be guaranteed.

All motors are stopped immediately when this occurs.

**Recovery:** Power the machine off and back on. If this happens repeatedly, check for heavy WiFi traffic, large file uploads, or other operations that may be blocking the motion-control task.

### WiFi LED

The WiFi LED blinks to indicate the machine's IP address on the local network after it connects (short blinks encode the address). It also blinks together with the Red LED during the watchdog-fired pattern described above.

---

## Common Problems

### Spools have too much friction
### Frame Flexing
### Anchors not free to move
### Belt not retracting
### Calibration
### Connection Dropping
### Sled lifting and tipping during movement



## Things that can make it work better

## Maintainance actions
### Sand and lubricate spools
### Loosen bolts around belt guard slightly
### Set Z stop
### Calibrate with no bit in and z all the way down
### Enter Z offsets for your anchor points
### Enter spoilboard thickness and work thickness
### Run a test pattern and enter scale adjustments
### Saving your YAML file
### Updating the firmware
### Making sure your firmware and index files match




