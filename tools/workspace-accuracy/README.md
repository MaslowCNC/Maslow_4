# Workspace accuracy map

The **Workspace** toggle on the new UI's drawing shows the area where cuts should land within a tolerance (±2 mm by
default), and whether the chosen job stays inside it. This page says how the map is worked out on each machine and
lists every choice behind it, so we can check that it's the calculation we want.

| File | What it is |
|---|---|
| `webui/src/accuracy.js` | The map the UI runs. Plain JS, no UI code. Tests: `webui/src/accuracy.test.js`. |
| `webui/src/ui/parts.jsx` (`accuracyFor`) | Feeds it the frame from the config, caches it, and checks the job. |
| `webui/src/art.js` (`accuracyOverlay`) | Draws it. |
| `tools/workspace-accuracy/accuracy-model.mjs` | The prototype. `node tools/workspace-accuracy/accuracy-model.mjs` prints its tables. |
| `tools/workspace-accuracy/REPORT.md` | The long version: the reasoning, the numbers, and what the legacy colour map did. |
| `tools/workspace-accuracy/mockups.html` | The three display options considered (the UI uses option C). |

## Maslow 4.1 and Maslow Mini

**Both machines run the same map.** The firmware runs the same calibration on both, and the map follows it. The one
thing that depends on the board is the starting ring, which the map takes from the connected machine's name:

| | Maslow 4.1 | Maslow Mini | Does the map follow it? |
|---|---|---|---|
| Board (`[ESP800]`, `BoardId`) | `maslow41` | `unified`, `unified36v` | Used for the starting ring |
| Frame geometry | From the config | From the config | Yes, read on connect |
| Arm (`beltEndExtension + armLength`) | 30 + 123.4 mm by default | Same defaults | Yes, read from the config |
| Calibration solver | Same bundle adjustment | Same | Yes, repeated on paper |
| Auto grid | 40% of the frame, 3/5/7/9 a side, ≤ 260 mm apart | Same | Yes |
| Starting ring | 6 points, 150 mm | 6 points, **50 mm** (`Calibration.cpp`: any board that isn't a 4.1) | Yes (`startRing()`); 150 mm until the board is known |
| Vertical orientation | Possible (`Maslow_vertical`) | Possible | Yes: adds the sled's weight (see "Vertical frames") |

The starting ring is a firmware constant chosen by board, not a setting: users can't change it. Its effect on the map
is small. The ±1 and ±2 mm areas with the 150 mm and the 50 mm ring:

| Frame | ±1 mm, 150 / 50 mm ring | ±2 mm, 150 / 50 mm ring |
|---|---|---|
| 3000 × 2100 | 1000 × 700 / 1000 × 700 | 2000 × 2000 / 2000 × 2000 |
| 2000 × 1400 | 600 × 500 / 700 × 400 | 1100 × 1200 / 1100 × 1200 |
| 1200 × 900 | 400 × 200 / 300 × 200 | 500 × 500 / 500 × 400 |

**No physical test has been run on either machine.** The untested items and the physical tests below apply to both.

## How the map is calculated

1. **Frame.** On connect, and again when Find anchors finishes, the UI reads from the config:
   - the four anchors (`kinematics/MaslowKinematics/tlX…brY`);
   - the board from `[ESP800]`, for the starting ring;
   - their heights (`tlZ…brZ`), plus the spoil board thickness (`Maslow_spoilboardThickness`);
   - `beltEndExtension + armLength`;
   - `Maslow_vertical`. On a vertical frame the map also uses the sled weight from Settings (step 5).

   The centre is where the diagonals cross (`MaslowKinematics::calculateCenter`). That is machine X 0, Y 0.
2. **Points.** Every 50 mm inside the anchors.
3. **How the sled follows the belts.**
   - All four belts are held at their target lengths. When the lengths disagree slightly, the sled settles where
     the belts' total stretch is smallest.
   - A longer belt is softer, so each belt counts with weight 1 / length.
   - At each point this gives how far the sled moves for a small error in each belt: G = (JᵀWJ)⁻¹JᵀW.
4. **How well calibration knows the anchors.**
   - Find anchors solves for 5 anchor numbers (tlX, tlY, trX, trY, brX; BL is at the origin and BR on y = 0) plus
     each measured point's position.
   - The map repeats that fit on paper, on the grid the firmware would measure (the auto rule above). This gives each
     anchor's uncertainty for an assumed measurement noise.
   - Anchors are best known near the grid and worst far from it, so errors grow outside the measured area.
5. **Errors that calibration partly cancels.**
   - Two are modelled:
     - a spoil board thickness entered wrong, which changes each belt's flat length by z × error / length;
     - a belt length scale error, from belt pitch or stretch;
     - on a vertical frame, each belt's stretch under its tension (see "Vertical frames").
   - They were also there while calibrating, so the fit absorbs most of them. The map keeps only what's left.
6. **Error at a point.**
   - It's measured from the centre (the usual home), since a constant offset doesn't spoil a cut.
   - e95 = |leftover systematic error here − at the centre| + 2.45 × the largest standard deviation. That's about a
     95% bound.
   - The standard deviation combines the anchor uncertainty and belt noise while cutting.
   - Points with less than 100 mm of belt to an anchor count as unusable. So do points on a vertical frame where a
     motor would have to hold more than 150 N.
7. **Safe area.** The largest rectangle centred on the frame centre, in 50 mm steps, where every point is within
   the tolerance. Drawn as the dotted blue outline.
8. **Job check.**
   - The job's extent (the rectangle its cutting moves cover) is moved by the work offset into machine coordinates.
   - Its outline is checked every 25 mm, with the error interpolated between map points.
   - Runs outside the tolerance are drawn orange and dashed. The line under the drawing gives the worst error.

The map takes a few milliseconds. It's recomputed only when the anchors, the spoil board, the tolerance, the board
or (on a vertical frame) the sled weight change.

## Vertical frames

When `Maslow_vertical` is true the sled hangs on the belts. The flat model stays as it is, and `tensions()` in
`accuracy.js` adds the weight:

1. **Weight.** W = sled mass × 9.81 × sin(tilt). The mass is the **Sled weight** setting (Settings → Frame and
   belts, shown only on a vertical frame, 8 kg by default). The tilt is taken as 90°, upright: `Maslow_vertical`
   says only yes or no.
2. **Tensions.**
   - Before the weight, each belt carries a preload of 20 N, spread so that it balances on its own.
   - The belts are held at fixed lengths, so the weight sags the sled a little: K δ = W, where K = Σ kᵢ uᵢuᵢᵀ and
     each belt's stiffness is kᵢ = EA / length.
   - Each belt's tension changes by kᵢ × (how much the sag lengthens it). The top belts take up the weight and the
     bottom ones lose tension.
3. **Slack belts.** A belt whose tension would drop below 0 is slack. It's taken out and the rest are solved
   again. A slack belt doesn't hold the sled, so it plays no part in where the sled goes (its weight in G is 0), and
   the position comes from fewer belts.
4. **Motor limit.** A point where a belt would need more than 150 N isn't usable. That cuts off a band below the
   top edge, between the top anchors, where the top belts are nearly level. The legacy map's "tension" warning
   showed the same thing.
5. **Stretch.** Each belt stretches by tension × length / EA. That's added to the systematic belt errors (step 5
   above). Calibration was done hanging too, so the fit absorbs part of it, and the map keeps what's left.

On a 3000 × 2100 frame with an 8 kg sled, the ±2 mm area is 1000 × 700 mm, against 2000 × 2000 mm flat. In that
model, at 20 N of preload, the bottom belts go slack even at the centre: the weight takes about 34 N off each. So
most of the frame is held by the top belts alone.

The result depends heavily on the guesses. The ±2 mm area on the same frame when one value changes:

| Changed | ±2 mm area |
|---|---|
| None (8 kg, 20 N preload, EA 20 000 N, 150 N limit) | 1000 × 700 |
| Sled 5 kg | 1600 × 700 |
| Sled 12 kg | 300 × 100 |
| Belt EA 10 000 N | 800 × 400 |
| Belt EA 40 000 N | 1500 × 800 |
| Preload 40 N or 60 N | 1000 × 700, 900 × 700 |
| Motor limit 300 N | 1000 × 700 |

So the sled weight and the belt stiffness matter most. The preload and the motor limit matter little.

Not modelled on a vertical frame:
- calibration's own tensions: it moves on the top belts and then pulls the bottom ones tight to measure, so its
  bottom-belt tensions differ from cutting's;
- the extra sideways stiffness that tension adds (T / length);
- belt sag under the belt's own weight (small for a light belt);
- the sled swinging or lifting off the board.

## Choices to review

Each of these is a decision, not a fact. Changing one changes the map.

| Choice | Now | Alternatives |
|---|---|---|
| Error measured from | The frame centre | The user's home (work 0, 0), or the job's own centre |
| Bound | 95% (2.45 σ in the worst direction) | 1 σ, or 99% |
| Safe area shape | Largest rectangle centred on the frame centre | Any shape (a contour, mockup A), or centred on the user's home |
| Job check | The job's bounding rectangle | Every cutting move of the toolpath, now drawn on the frame |
| Default tolerance | ±2 mm | Any; Settings → Frame and belts offers ±0.5 to ±5 mm, per browser |
| Belt weighting | Stiffness 1 / length | Equal weights (stiff belts), or a measured stiffness |
| Systematic errors modelled | Spoil board thickness, belt scale | Anchor heights, frame flex, a once-per-spool-turn error |
| Grid the map assumes | The firmware's auto rule | The grid that was actually measured, once the firmware saves it |
| Unusable near anchors | Less than 100 mm of belt | A limit from the arm geometry |
| Vertical: tilt | Upright (90°) | A tilt setting, if machines lean back |
| Vertical: where the weight goes | Belts held at fixed lengths share it by stiffness, after a balanced preload | Top belts only (the legacy two-rope formula, an upper bound) |
| Vertical: sled weight | One per browser, in Settings | A firmware setting, so every browser agrees |

## Assumed values (no measurement behind them yet)

| Value | Used | Where a real value could come from |
|---|---|---|
| Calibration fit noise | 0.5 mm | The firmware logs it after Find anchors (`Find Anchors fit: rms=…`) but doesn't save it. Saving it in the config is a small firmware change. |
| Grid that was measured | The firmware's auto rule | Computed on the machine, not saved. A user-set grid size, or a work area that shrinks the grid, changes it and the map won't know. |
| Belt noise while cutting | 0.1 mm | The status report's `BE` field. The UI could keep a running average. |
| Spoil board thickness error | 3 mm | A guess. |
| Belt scale error | 0.05% | A guess. Needs a measured belt pitch under working tension. |
| Minimum belt length | 100 mm | A guess. |
| Sled mass (vertical) | 8 kg, changeable in Settings | Weigh the sled with the router. A Mini's sled is probably lighter than a 4.1's. |
| Belt EA (vertical) | 20 000 N | A guess. Hang a known weight on a measured length of belt. |
| Preload (vertical) | 20 N | A guess. Depends on Apply Tension's current threshold. |
| Motor holding limit (vertical) | 150 N | A guess. Depends on the motor current limit and the spool radius. |

None of these needs a new status report field. The fit RMS and the measured grid belong in the config.

## Untested in the UI

- **Reading the config on a real machine** (`$/kinematics/MaslowKinematics/…` and `Maslow_vertical`), on either
  board. In the simulator the frame is built in. Simulator → Cause an event → Stand the frame vertically switches
  it to vertical.
- **Re-reading the anchors after Find anchors finishes.**
- **Frames that aren't rectangles.** The drawing stretches the real anchors onto the drawn corners, so a skewed
  frame draws slightly off.
- **The phone layout** doesn't show the overlay.

## What only a physical test can tell us

- **Whether the 95% bound is right at all.** Every number above comes from a model.
- **Friction.** A flat sled slides on the spoil board. Friction can stop it short of the belts' balance point, and
  the error then depends on the direction it came from (hysteresis). The model ignores this, and it could be the
  largest effect.
- **Belt stiffness.** Cutting forces stretch the belts, worse where they're long or nearly in line. Its size
  depends on a value nobody has measured.
- **Effects left out of the model:**
  - anchors moving or the frame flexing under tension;
  - anchor height errors;
  - the sled tilting;
  - belt length changing with temperature and humidity;
  - error that repeats every spool turn (44 mm), from an off-centre spool or the encoder;
  - router runout.
- **Vertical frames.** Whether the bottom belts really go slack, how much the belts stretch, and what a motor holds.

## Suggested physical tests

Run each on a Maslow 4.1 and on a Mini.

1. **Pattern test.**
   - Cut small crosses or holes every 300 mm across the whole frame, out towards the anchors.
   - Measure the distances between them with a steel rule or tape.
   - Compare the error at each cross with the map's value there.
2. **Grid test.** Repeat after calibrating with a grid that covers the work area. The model predicts the error at
   the work-area corners roughly halves (±1.9 → ±0.9 mm on a 3000 × 2100 frame).
3. **Repeat calibration.**
   - Run Find anchors 3 to 5 times without moving anything.
   - Note each run's anchors and the logged fit RMS.
   - The spread of the anchors tests the model's anchor uncertainty directly, and the RMS replaces the 0.5 mm guess.
4. **Direction test.** Approach the same point from opposite directions and mark it both times. The gap between
   the marks measures the friction (hysteresis) the model leaves out.
5. **Vertical frames.**
   - Weigh the sled with the router fitted.
   - Hang a known weight on a measured length of belt to get EA.
   - Run the pattern test hanging, and compare it with the same test flat.
   - Watch the bottom belts' motor currents (the Motors toggle) across the frame: near zero means slack.
