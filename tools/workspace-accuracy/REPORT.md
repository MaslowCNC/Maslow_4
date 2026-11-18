# Workspace accuracy: replacing the legacy colour map

Exploration for the new web UI (`webui/`). Nothing here is wired into the UI or firmware.

Files in this folder:

- `accuracy-model.mjs`: the model. `node tools/workspace-accuracy/accuracy-model.mjs` prints the tables below.
- `gen-mockups.mjs` → `mockups.html`: three display options on the new UI's frame drawing (open the HTML file in a browser).

## 1. Does anything account for the calibration grid size?

**No. Nothing that judges accuracy uses the grid.**

- **Legacy map** (`toolpath-displayer.js`, `computPositonGradient`): no grid input. It uses distance to anchors (fixed
  1200 mm) and a two-rope hanging tension. It also uses `initialGuess`, which is the configured anchors, not the
  calibrated ones.
- **Firmware: grid sizing.** `Calibration::generate_calibration_grid` (`Calibration.cpp:2358`):
  - Auto grid = 40% of the frame in each axis, centred on (0,0).
  - Points per side = smallest of 3/5/7/9 that keeps spacing ≤ `calibrationMaxSpacingMm` = 260 mm.
  - A 6-point ring (150 mm, or 50 mm on the Mini) around the start position comes first.
  - It's shrunk to stay 100 mm inside a user-set work area.
  - On the 3000 × 2100 frame this gives a **7×7 grid of 1200 × 840 mm**. The auto work area
    (`MaslowKinematics::getWorkArea`, 80% × 60% of the frame) is **2400 × 1260 mm**. So the corners of the work
    area are 600 mm (X) and 210 mm (Y) outside the measured points.
- **Firmware: solver.** `recomputeAnchorsWithLevenbergMarquardt` is a bundle adjustment:
  - Unknowns: 5 anchor parameters (BL fixed at the origin, BR on y = 0) plus one (x, y) per point.
  - It reports only belt-residual fitness: overall RMS, max residual, per-anchor RMS. The gates are RMS ≤ 5 mm and
    max ≤ 15 mm.
  - It computes no anchor uncertainty and no position uncertainty, and it doesn't ask where the grid sits.
  - The RMS appears only in the log line `Find Anchors fit: rms=…`. It isn't saved.
- **JS copy** (`docs/calibration-simulation/machine-simulator.js`): uses 50% of the frame (`frameWidth * 0.5`), not
  the firmware's 40%. It adds random, constant and proportional measurement error, but it measures only the fit,
  not the error at points away from the grid.

**How the grid relates to trustworthy accuracy.** The anchor errors are what's left after the fit, and their size
depends on how far the anchors are from the measured points:

- Inside the grid, errors in the fitted anchors largely cancel, because the fit made the model agree with the belt
  lengths there.
- Outside the grid, the solution is extrapolated. Errors grow with distance from the grid, fastest towards the
  anchors.

From the model (95% error relative to the centre, defaults below, 3000 × 2100 frame):

| Calibration grid | Error at auto work-area corner | Largest centred ±1 mm rectangle |
|---|---|---|
| auto 7×7, 1200 × 840 (today) | ±1.9 mm | 1050 × 650 mm |
| 9×9, 1200 × 840 (more points, same extent) | ±1.6 mm | 1050 × 850 mm |
| 9×9, 1800 × 1260 (60%) | ±0.95 mm | 1350 × 2050 mm |
| 9×9, 2400 × 1260 (= auto work area) | ±0.88 mm | 2150 × 2050 mm |
| 3×3, 2400 × 1260 (few points, full extent) | ±1.3 mm | 1300 × 2050 mm |

**Extent matters more than point count.**
- A 3×3 grid covering the work area beats a 9×9 grid covering 40% of the frame.
- A grid covering the work area roughly doubles the width that meets ±1 mm.
- This is a separate, firmware-side finding: the grid should probably follow the (auto) work area, not 40% of the
  frame. It's worth testing on a real machine.

## 2. Usable area from a cut-accuracy tolerance

**Yes.** The model gives an error in mm at every point, so "usable at ±t" is the region where the error is ≤ t. Each
term is a physical quantity with units. Nothing is tuned to look right.

**Model.** E95(p) is the 95% bound on where a point lands, relative to the frame centre (the usual home):

    E95(p) = |systematic(p) − systematic(centre)| + 2.45 · σmax(p)   [+ F / kmin(p) under a side load]

1. **How the sled responds to belt errors.**
   - All four belts are position-controlled. When their lengths disagree, the sled settles where the stretch
     energy Σ kᵢ·stretchᵢ² is smallest, with kᵢ = EA / Lᵢ.
   - So δp = (JᵀWJ)⁻¹JᵀW · δL, where the rows of J are unit vectors from each anchor to the sled and
     W = diag(1/Lᵢ).
   - This uses all four belts, the real anchor positions, and a flat machine (no gravity). That fixes the legacy
     assumption of a sled hanging from two ropes.
2. **Anchor uncertainty left after calibration.**
   - Uses the same 5 + 2N-parameter bundle as the firmware. Σθ = σcal² · (Schur complement of JᵀJ over the sled
     positions)⁻¹, on the actual grid points.
   - σcal is the fit RMS the firmware already computes. With the auto grid on 3000 × 2100, at σcal = 0.5 mm, the
     anchor sd is 0.35 to 1.4 mm. The anchor Y values are the weakest.
3. **Systematic belt errors, which calibration partly absorbs.**
   - Each enters as a bias in the calibration measurements and at the point being cut. The fitted anchors shift by
     the least-squares response, and the remainder is the error.
   - Spoil board thickness entered wrong by δz: the XY length is off by z·δz / L (z = anchor Z + spoil board).
   - Belt pitch or stretch scale error ε: the error is ε·L.
   - Constant arm or extension length error: a constant offset per belt.
4. **Runtime belt noise σrun.**
   - Encoder quantisation is 43.97 mm/rev ÷ 4096 = **0.011 mm**, which is negligible.
   - The real noise is following error, which is already in the status report as `BE`.
5. **Optional: stiffness under a side load F.**
   - K = Σ EA/Lᵢ·uuᵀ + T/Lᵢ·(I − uuᵀ), and the deflection is F / λmin(K).
   - This is the physical version of the legacy "tension" term. It needs the belt's EA, which we don't know. The
     12 kN guess makes 10 N of side load dominate everything (`defaults + 10 N load` → no ±1 mm area). Leave it out
     until someone measures it. The stiffness map is still useful for its shape.

**Results** (defaults: σcal 0.5 mm, σrun 0.1 mm, spoil board 3 mm off, belt scale 0.05%):

| Frame | ±0.5 mm | ±1 mm | ±2 mm |
|---|---|---|---|
| 3000 × 2100, auto grid | 400 × 300 (3%) | 1050 × 650 (17%) | 2000 × 2050 (89%) |
| 4340 × 3230 (`maslow-m4.yaml`), auto 9×9 grid | 550 × 400 (3%) | 1350 × 1000 (16%) | 2350 × 3200 (87%) |

The sizes are the largest centred rectangle in mm; the percentage is the share of the anchor rectangle.

**What drives it** (3000 × 2100, ±1 mm centred rectangle, one input at a time):

| Input | ±1 mm centred rectangle (mm) |
|---|---|
| Noise only | 1400 × 900 |
| σcal 1.0 instead of 0.5 | 850 × 400 |
| Spoil board 3 mm off | 2150 × 2050 |
| Belt scale 0.05% | 2500 × 2050 |
| Arm length off 1 mm | 2500 × 2050 |

So:
- **Calibration quality (σcal) × grid coverage dominates.**
- Systematic errors are mostly absorbed by calibration, because the same error is in the calibration data. They
  matter near the anchors, where z/L is large.
- The minimum belt length near the anchors is a hard limit in the model (`minBelt`, 100 mm).

**Caveats.**
- The model is first-order (linearised).
- σcal = 0.5 mm is a guess. No real fit logs were in the repo, and I didn't query the machine.
- It ignores anchor Z errors, frame flex, sled tilt, and a gravity component on a tilted frame.
- The error is relative to the centre. For a small part, the local error between two nearby points is smaller.
  A "part-size-aware" variant would use δp(p) − δp(q) for points q within the part.
- Validate against a real cut: measure a test pattern at a few positions and compare with E95.

### Vertical (hanging) machines: not modelled yet

The model above assumes a **flat** machine, where gravity doesn't act in the frame's plane. The firmware also
supports a vertical setup, with the same four belts:
- `Calibration::detectOrientation()` decides HORIZONTAL or VERTICAL at the start of a calibration.
- The result is saved as the config item `Maslow_vertical` (`MachineConfig.cpp:182`).
- The orientation changes only how calibration moves and measures. In VERTICAL, calibration moves on the top two
  belts while the bottom ones comply, then pulls the bottom ones tight to measure (`Calibration.cpp:1651`, `:2316`).
- When cutting, all four belts are position-controlled in both orientations. So the 4-belt sled response in
  section 2 still applies. What's new is gravity.

In vertical mode, gravity loads the belts, which is the effect the legacy map approximated with two ropes. Add it in
real units, not the legacy `/-3` scaling and 0.15 cut-off:

1. **Statics with four belts.**
   - Solve Σ Tᵢ·uᵢ = m·g·sin(α) (down the board), where α is the frame's tilt from horizontal and m is the
     sled + router mass.
   - Four tensions and two equations means the split depends on the bottom belts' preload and on how stiff each
     belt is. Model it as preload + elastic share: each belt's tension = preload + (EA/L)·(stretch from the sled's
     sag under its weight).
   - In practice the top belts carry most of the weight. The legacy two-rope formula,
     T = m·g·sin α · cos B / sin(A+B), is the limit case where the bottom belts carry nothing. It's a useful upper
     bound and shows the same blow-up near the top edge between the anchors, where A+B → 0.
2. **Tension limits, a hard boundary like `minBelt`.**
   - Usable only where each top belt's tension stays below what its motor can hold, which comes from the current
     limit.
   - Each bottom belt must keep a positive tension, so it isn't pushed slack.
   - Near the bottom of the frame the bottom belts are almost horizontal and add little vertical support, so they
     can go slack there if the preload is low.
   - A slack bottom belt drops out of G. Where that happens, the position comes from fewer belts and the errors get
     larger.
3. **Stiffness.**
   - The belt tensions add the geometric term T/L to K, so the higher tensions in vertical mode make the sled
     slightly stiffer sideways where the top belts are heavily loaded.
4. **Stretch under the weight.**
   - Each belt stretches by T·L/EA, which changes across the frame: a systematic error that varies with
     position.
   - Calibration measures under the same load, so the fit absorbs part of it. As with the other biases, model the
     part it doesn't absorb.
   - Belt sag under its own weight is negligible for a light belt.

Extra inputs for vertical mode:
- **Orientation.** Already saved as `Maslow_vertical`; the UI reads it with the rest of the config.
- **Tilt angle α.** New setting, with 90° = vertical. Today there's only a yes/no.
- **Sled mass.** New setting.
- **Bottom-belt preload**, in N.
- **Motor holding limit**, in N.
- **Belt EA.** It also needs measuring for the stiffness term.

Until this is built, the UI should show the flat-machine map only when `Maslow_vertical` is false, and say the map
doesn't apply otherwise.

## Inputs and firmware data

| Input | Where it is today | Needed |
|---|---|---|
| Calibrated anchors (tlX…brX, Z) | config (`maslow.yaml`, `$CD`) | read config once; no report change |
| beltEndExtension, armLength, spoil board thickness | config / settings | same |
| Calibration fit RMS (σcal) | log line only (`Find Anchors fit: rms=`) | **new**: save it as a config value next to the anchors when calibration succeeds (e.g. `calibrationFitRms`) |
| Grid actually measured (size, width, height) | computed on the device, log line only | **new**: save `calibrationGridN/W/H` with the anchors (or the UI repeats the firmware rule, which breaks if the rule changes) |
| Orientation (HORIZONTAL / VERTICAL) | config `Maslow_vertical`, set by `detectOrientation` during calibration | none to read it; vertical modelling also needs tilt, sled mass, preload and motor holding limit (see "Vertical machines") |
| Runtime noise σrun | `BE` in the status report | none; the UI can keep a running RMS of `BE` |
| Tolerance | none | UI preference (default ±1 mm) |

**No new status-report field is needed.** Everything new changes only when calibration finishes, so it belongs in
saved config, not the 50 ms report. That follows `.github/copilot-instructions.md`.

The map is about 60 × 42 points with 2×2 / 5×5 matrices each. It takes milliseconds in the browser, so the UI should
compute it, not the firmware.

## 3. Display

The mockups (`mockups.html`) draw on `art.js`'s frame (anchors at (30,30)…(380,240)). Colours:
- Okabe-Ito blue/orange for good/bad.
- Cividis for the heatmap. Both are safe for colour-blind users.
- Lilac for the job, as in the current UI.

There's a tolerance switch (±0.5 / ±1 / ±2) and a grid switch (today's auto grid vs a grid covering the work area).

- **A. Contour + safe rectangle.**
  - One line at the tolerance, plus the largest centred rectangle inside it, labelled with its size ("suggested
    work area 1000 × 700 mm").
  - Calibration points are drawn as dots, so the link to the grid is visible.
  - Job edges outside the tolerance turn orange and dashed.
- **B. Heatmap in mm + cursor readout.**
  - Smooth map with a legend in mm.
  - Hover shows "±0.85 mm (systematic 0.41 + random 0.43)".
  - Best for understanding the result; too much for the Run screen.
- **C. Job check only.**
  - The safe rectangle as a faint outline, plus one chip: "Job fits the ±1 mm area" or "780 mm of the outline is
    outside ±1 mm (worst ±1.4 mm)".

**Recommendation.**
- Use **C on the Run / File screen**, since it answers "will my job be accurate?".
- Use **A in the frame drawing** whenever a job or work area is shown.
- Feed A's safe rectangle into the work-area step as the default suggestion, in place of today's fixed
  80% × 60%.
- Keep **B** behind a "details" toggle or on the calibration result screen. It's the best way to show users that a
  wider grid or a better fit grows the area.
- A tolerance chooser (default ±1 mm) belongs in settings, not on the main screen.
