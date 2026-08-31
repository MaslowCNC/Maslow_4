# Troubleshooting Guide Maslow 4
Excellent forum Troubleshooting resources:
<https://forums.maslowcnc.com/t/the-belt-maslow-a-k-a-maslow-4-manual/19147/9?>
also this
<https://forums.maslowcnc.com/t/the-big-bad-m4-troubleshooting-problems-solutions-thread/21000/26>



## LED Indicators

The Maslow 4 control board has two indicator LEDs: a **Red LED** and a **WiFi LED**. Their blink patterns tell you what the machine is doing.

### Red LED

The Red LED signals error conditions that require attention. There are two distinct blink patterns:

#### Slow Blink (300 ms on / 300 ms off) — Error / Emergency Stop

The Red LED blinks slowly when `Maslow.error` is set to `true`. The machine will not respond to movement commands while in this state, and it persists until the alarm is cleared with `$X`.

Possible causes:

| Cause | Details |
| --- | --- |
| **Encoder not found** | One or more belt-tension encoders could not be detected on the I²C bus at startup. Check encoder wiring and connectors. |
| **Motor not found** | One or more motor drivers did not respond during the startup self-test. Check motor wiring and the motor driver board. |
| **Emergency stop command** | The `$ESTOP` command was sent (e.g. via the web interface E-Stop button). |
| **Position error > 15 mm** | While running a G-code job, an axis drifted more than 15 mm from its target position for more than 5 consecutive checks. This usually indicates a belt that has gone slack, a broken or disconnected belt, or a mechanical obstruction. |

**Recovery:** Fix the underlying cause, then clear the alarm with `$X` (the **Clear** button in the web interface). Investigate the error message that was printed in the console/log when the LED started blinking—it will name the specific axis and cause.

`$X` turns the LED off and re-enables motion, so only use it once you have dealt with the cause. After clearing a position error, re-check belt tension and confirm the machine position before cutting.

#### Rapid Double-Blink (100 ms on / 100 ms off / 100 ms on / 800 ms pause) — Watchdog Fired

Both the **Red LED and the WiFi LED** blink together in a rapid double-blink pattern. This means the firmware's internal motion-control watchdog fired: the main `update()` loop was not called for more than 100 ms, which indicates the processor was blocked on another task (e.g. a large file write or a network operation) long enough that motor safety could not be guaranteed.

All motors are stopped immediately when this occurs.

**Recovery:** Clear the alarm with `$X` (the **Clear** button in the web interface). If this happens repeatedly, check for heavy WiFi traffic, large file uploads, or other operations that may be blocking the motion-control task.

### WiFi LED

The WiFi LED blinks to indicate the machine's IP address on the local network after it connects (short blinks encode the address). It also blinks together with the Red LED during the watchdog-fired pattern described above.

---

## Common Problems

### Spools have too much friction 
The ring of the spool that the belt winds around inside the arm sometimes comes from the factory with a little bit too much plastic on the edges or a tight fit on the main body of the arm. There is a slight taper so that the parts can be released from the mold.  Make sure the spool isn't geting stuck on that taper, lightly sand the inside of the spool and the corresponding ring on the arm and then use a silicone lubricant.  Be careful with other lubricants as they can interact with the plastic. 
### Frame Flexing
Maslow depends on calculating it's position solely from the length of the belts as they pass by the encoder gear with the magnet on the way out of the spool. If the frame is flexing visibly, even by a millimeter, then the position calculations will be off by that millimeter or more.  As much as is possible make sure your frame is solid and well supported. Concrete floors are great.  Metal pipes and struts naturally flex and bend.  Wood can be good with thoughtful engineering but as the machine runs look for twisting and lifting of the frame corners. 
### Frame Dimensions appropriate for your workspace
Use a frame calculator when you are setting up your Maslow.  The calculations get less accurate as the triangles that maslow makes get thinner, the tension near the top of a vertical maslow appoaches infinity as the belts get closer to horizontal at the top, and the belts and motors bump against the z supports when the belts are at too small or too large of an angle (around 130 degrees) 
Dlang has made a very useful and cool frame calculator here:
<http://lang.hm/maslow/maslow4_frame.html>


geertdoornbos has made a cool one where you can simulate the movement of the robot:
<https://maslowcnc.nl/frame>

Bar's frame simulator here: 
[Layout Simulator](layout-simulator/) ([documentation](layout-simulator/README.md))

Bar's Calibration point tester:
<https://barboursmith.github.io/Calibration-Simulation/>


### Anchors not free to move
Anchors at the ends of the belts need to be able to swing freely around their bolts or pins. The bolts and pins can't be too small or there will be rattling or too big. 10 mm or 3/8 inch works well but should be smooth without threads on the part where the anchor sits. If you tighten down a bolt or have too tight of a space in your corner anchor the belt won't be able to swing freely back and forth as the machine moves.  Also important to keep them from bouncing up and down too much on the bolt. washers, spacers or leaving just enough bolt to just fit is important. 
### Belt not retracting


### Find Anchors (aka Calibration) 

### Connection Dropping

### Sled lifting and tipping during movement

### Z axis not gong all the way down

### Reset Z position after cutting or setup interrupted




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





