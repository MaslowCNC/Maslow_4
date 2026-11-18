# Workspace overlay: how it's calculated, and what isn't known yet

The **Workspace** toggle on the drawing (`webui/src/accuracy.js`, drawn by `accuracyOverlay` in `webui/src/art.js`)
shows the area where cuts should land within the tolerance (±2 mm by default), and whether the chosen job stays inside it. The full reasoning
and numbers are in `REPORT.md`. This page is the short version.

## How the map is calculated

1. **Frame.** The UI reads from the config:
   - the four anchors (`kinematics/MaslowKinematics/tlX…brY`, the calibrated ones once Find anchors has run);
   - their heights (`tlZ…brZ`) plus the spoil board thickness;
   - `beltEndExtension + armLength`;
   - `Maslow_vertical`.

   The centre is where the diagonals cross, as in the firmware, and is machine X 0, Y 0.
2. **Points.** Every 50 mm inside the anchors.
3. **How the sled follows the belts.**
   - All four belts are held at their target lengths. When the lengths disagree slightly, the sled settles where the
     belts' total stretch is smallest.
   - A longer belt is softer, so each belt counts with weight 1 / length.
   - This gives, at each point, how far the sled moves for a small error in each belt.
4. **How well calibration knows the anchors.**
   - The firmware's Find anchors solves for 5 anchor numbers plus each measured point's position. BL is at the
     origin and BR is on y = 0.
   - The model repeats that fit on paper, on the grid the firmware measures:
     - 40% of the frame;
     - 3/5/7/9 points a side, at most 260 mm apart;
     - plus the 150 mm starting ring.
   - With an assumed measurement noise, this gives how uncertain each anchor is. Anchors are best known near the
     grid and worst far from it.
5. **Errors that calibration partly cancels.**
   - Two errors are modelled:
     - the spoil board thickness entered wrong, which changes each belt's flat length by z × error / length;
     - a belt length scale error from pitch or stretch.
   - These were also present while calibrating, so the fit absorbs most of them. The model keeps only what's left.
6. **Error at a point.**
   - The error is measured from the centre (the usual home).
   - It's the leftover systematic error plus 2.45 × the largest standard deviation, which is about a 95% bound.
   - The standard deviation combines the anchor uncertainty and belt noise while cutting.
   - Points closer than 100 mm of belt to an anchor count as unusable.
7. **Safe area.** The largest rectangle centred on the frame centre, in 50 mm steps, where every point is within the
   tolerance.
8. **Job check.**
   - The job's extent is the rectangle its cutting moves cover, moved by the work offset.
   - Its outline is checked every 25 mm, with the error interpolated between map points.
   - Runs outside the tolerance are drawn orange and dashed, and the line under the drawing gives the worst error.

## Assumed values (no measurement behind them yet)

| Value | Used | Where a real value could come from |
|---|---|---|
| Calibration fit noise | 0.5 mm | The firmware logs it after Find anchors (`Find Anchors fit: rms=…`) but doesn't save it. Saving it in the config is a small firmware change. |
| Grid that was measured | firmware's auto rule | Computed on the machine, not saved. A user-set grid size or work area changes it, and the overlay won't know. |
| Belt noise while cutting | 0.1 mm | The status report's `BE` field. The UI could keep a running average. |
| Spoil board thickness error | 3 mm | Guess. |
| Belt scale error | 0.05% | Guess. Needs a measured belt pitch under working tension. |
| Minimum belt length | 100 mm | Guess. |
| Tolerance | ±2 mm | A choice: Settings → Frame and belts → Workspace tolerance (±0.5 to ±5 mm), remembered per browser. |

## Untested in the UI

- **Reading the config on a real machine** (`$/kinematics/MaslowKinematics/…` and `Maslow_vertical`): untested. In
  the simulator the frame is built in.
- **Re-reading the anchors after Find anchors finishes:** untested.
- **Frames that aren't rectangles:** the drawing stretches the real anchors onto the drawn corners, so a skewed frame
  draws slightly off.
- **The phone layout** doesn't show the overlay.

## What only a physical test can tell us

- **Whether the 95% bound is right at all.** Every number above is a model.
- **Friction.** A flat sled slides on the spoil board. Friction can stop it short of where the belts' balance
  point is, and the error then depends on the direction it came from (hysteresis). The model ignores this, and it
  could be the largest effect.
- **Belt stiffness.** It isn't in the map. Cutting forces stretch the belts, worse where they're long or nearly in
  line. Its size depends on a value nobody has measured.
- **Effects left out of the model:**
  - anchors moving or the frame flexing under tension;
  - anchor height errors;
  - the sled tilting;
  - belt length changing with temperature and humidity;
  - error that repeats every spool turn (44 mm), from an off-centre spool or the encoder;
  - router runout.
- **Vertical machines.** The weight of the sled isn't modelled, so the overlay says so instead of drawing.

## Suggested physical tests

1. **Pattern test.**
   - Cut a pattern of small crosses or holes every 300 mm across the whole frame, out towards the anchors.
   - Measure the distances between them with a steel rule or tape.
   - Compare the error at each cross with the map's value there.
2. **Grid test.** Repeat after calibrating with a grid that covers the work area. The model predicts the error at
   the work-area corners roughly halves (±1.9 → ±0.9 mm on a 3000 × 2100 frame).
3. **Repeat calibration.**
   - Run Find anchors 3 to 5 times without moving anything.
   - Note each run's anchors and the logged fit RMS.
   - The spread of the anchors tests the model's anchor uncertainty directly, and the RMS replaces the 0.5 mm guess.
4. **Direction test.** Approach the same point from opposite directions and mark it both times. The gap between the
   marks measures the friction (hysteresis) the model leaves out.
