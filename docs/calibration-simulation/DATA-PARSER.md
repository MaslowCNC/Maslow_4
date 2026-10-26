# Calibration Data Parser

This simulator processes **real calibration measurement data** from your Maslow CNC machine to compute optimal anchor point positions. Unlike the machine simulator (`index.html`) which generates synthetic data, this tool works with actual measurements collected from your machine.

## Purpose

This tool is designed for:
- **Analyzing real calibration data** from your Maslow machine
- **Finding optimal anchor positions** without running full calibration on the machine
- **Testing different initial guesses** to see how they affect results
- **Troubleshooting calibration issues** by examining real measurement data

## Quick Start

1. Open `data-parser.html` in your web browser
2. Copy calibration measurement data from your machine
3. Paste it into the data input field
4. Click "Parse and Validate"
5. Adjust initial anchor position guesses if needed
6. Click "Compute Anchor Positions"
7. Review the optimized positions and fitness score

## Data Format

The tool accepts measurement data in multiple formats:

### Format 1: Plain comma-separated objects
```javascript
{tl:2051.76, tr:2053.05, bl:1942.31, br:1952.85},{tl:2154.52, tr:1955.15, bl:2132.14, br:1584.18}
```

### Format 2: Array notation
```javascript
[{tl:2051.76, tr:2053.05, bl:1942.31, br:1952.85},{tl:2154.52, tr:1955.15, bl:2132.14, br:1584.18}]
```

### Format 3: CLBM format (from machine output)
```javascript
CLBM:[{bl:2960.58, br:3150.08, tr:3067.72, tl:3049.85},{bl:3066.96, br:3042.59, tr:2957.53, tl:3158.38},]
```

Each measurement object contains:
- `tl`: Top-left belt length (mm)
- `tr`: Top-right belt length (mm)
- `bl`: Bottom-left belt length (mm)
- `br`: Bottom-right belt length (mm)

The parser automatically handles:
- CLBM: prefix (extracts the array)
- Trailing commas in arrays
- Unquoted property names (JavaScript object notation)
- Multiple measurements


## How to Get Measurement Data

Measurement data comes from the calibration process on your Maslow machine. The data is typically logged during calibration and can be found in:
- Machine console output during calibration
- Log files from calibration runs
- Debug output from the ESP3D-WEBUI

## Using the Tool

### 1. Paste Measurement Data

Copy your calibration measurements and paste them into the large text area. The format should be comma-separated measurement objects as shown above.

### 2. Parse and Validate

Click "Parse and Validate" to check that your data is correctly formatted. The tool will:
- Verify the data structure
- Count the number of measurements
- Display any parsing errors

### 3. Set Initial Anchor Positions

Provide starting estimates for where the anchor points are located:
- **Top Left X, Y**: Position of top-left anchor
- **Top Right X, Y**: Position of top-right anchor
- **Bottom Right X**: Position of bottom-right anchor
- Bottom-left is fixed at (0, 0)

These don't need to be perfect - the algorithm will refine them. However, better initial guesses lead to faster convergence.

### 4. Compute

Click "Compute Anchor Positions" to run the optimization algorithm. The tool will:
- Feed the measurements directly into the firmware-exact Levenberg-Marquardt solver
- Jointly refine the anchor and sled positions
- Display progress in the log
- Show the final anchor positions, RMS fitness, and whether the machine's quality gates pass

### 5. Interpret Results

The results show:
- **Optimized anchor positions**: The computed X,Y coordinates
- **Fitness (RMS)**: The RMS belt-length error in mm (lower is better). The machine accepts a
  calibration only when RMS ≤ 5 mm and the worst single-belt residual ≤ 15 mm; the log notes
  when a fitness gate fails (the machine would not save those anchors)

### Measurement Map & Error Breakdown

When a computation finishes (especially when a fitness gate fails), a **Measurement Map** is shown:

- **Anchors** are drawn as dark squares at their solved positions.
- **Each measurement** is a dot at its solved sled position, coloured by its RMS belt error
  (green = low, red = high). Dots that break a gate get a red outline.
- **Belt lines** run from each sled toward the four anchors; a line's thickness and colour show
  how much that individual belt residual contributes, so you can see whether one belt
  (e.g. bottom-left) is consistently the largest error.
- **Per-belt RMS cards** summarise which belt drives the overall error.
- **A per-measurement table** lists the signed residual for every belt, the measurement RMS,
  and the worst belt, with failing rows highlighted.

The map is interactive: **scroll to zoom**, **drag to pan**, and **click a dot** (or use the
table checkboxes) to exclude a suspect measurement — the anchors are re-solved from the
remaining points instantly, so you can see how much a bad point was skewing the fit. Use
**Exclude failing points** to drop everything that breaks a gate at once, **Include all points**
to restore them, and **Reset view** to recentre the map.

## Code Sharing

This tool uses the **exact same Levenberg-Marquardt math that runs on the machine**.
The function `recomputeAnchorsLM()` in the shared library
`calibration-computation.js` is a line-for-line port of the
firmware routine `Calibration::recomputeAnchorsWithLevenbergMarquardt()`
(`firmware/FluidNC/src/Maslow/Calibration.cpp`). Given the same CLBM measurements and
the same initial anchor guess, the parser produces the **same anchor positions** the
machine computes.

> **Maintainer note:** `calibration-computation.js` in this folder is now the **only**
> copy of this math in JavaScript, and it exists for development and simulation only.
> It used to be mirrored at `ESP3D-WEBUI/www/js/calibration-computation.js` and shipped
> inside `index.html.gz`, but the web UI stopped calling it once calibration moved into
> the firmware, so that copy was deleted to shrink the bundle. The authority for how
> calibration actually behaves is `Calibration::recomputeAnchorsWithLevenbergMarquardt()`
> in `firmware/FluidNC/src/Maslow/Calibration.cpp` — **if you change the firmware routine,
> update this file to match**, or the parser will quietly disagree with the machine.

The port reproduces the firmware exactly, including:
- **Sparse bundle adjustment**: anchor parameters `[tlX, tlY, trX, trY, brX]` and a sled
  `(x, y)` per waypoint are optimized jointly, solved via a Schur complement (a 5×5
  reduced system plus 2×2 per-waypoint blocks).
- **Analytic Jacobians** (not finite differences).
- **Deterministic retry loop**: up to 10 retries using the firmware's fixed anchor
  perturbation table (±25 mm then ±50 mm), keeping the best result across attempts.
- **Fitness gates**: RMS ≤ 5 mm and max single-belt residual ≤ 15 mm, plus convergence —
  the same gates the machine uses to decide whether to save the anchors.

> **Important:** Paste the machine's logged `CLBM:[...]` data directly. Those values are
> already projected into the XY plane by the firmware, so the parser feeds them straight
> into the solver **without any additional Z projection** — matching exactly what the
> machine's solver sees.

## Differences from Machine Simulator

| Feature | Data Parser | Machine Simulator |
|---------|-------------|-------------------|
| **Data Source** | Real measurements from machine | Simulated measurements |
| **Purpose** | Analyze actual calibration data | Test algorithm with synthetic data |
| **Use Case** | Troubleshooting, data analysis | Algorithm development, testing |
| **Input** | Paste measurement data | Configure simulation parameters |

## Example Workflow

1. **Collect Data**: Run calibration on your machine and save the measurement output
2. **Parse**: Paste the data into this tool and validate it
3. **Compute**: Process the measurements to find optimal anchor positions
4. **Compare**: Compare results with what your machine computed
5. **Troubleshoot**: If results differ, investigate data quality or initial guesses

## Tips

- **More measurements = better results**: The algorithm works better with more data points
- **Initial guess matters**: Try to get within ~100mm of actual positions (the input fields
  default to the anchors in `firmware/FluidNC/data/maslow.yaml`)
- **Check the RMS**: The reported fitness is the RMS belt-length error in mm (lower is better);
  the machine requires RMS ≤ 5 mm and max residual ≤ 15 mm to accept a calibration
- **Look for patterns**: Consistently high RMS may indicate measurement issues

## Technical Details

The computation is a **Levenberg-Marquardt sparse bundle adjustment**, ported verbatim from
the firmware (`Calibration::recomputeAnchorsWithLevenbergMarquardt`):
1. Estimate each waypoint's sled `(x, y)` from all four belt lengths via 2D Gauss-Newton
2. Jointly optimize anchor params `[tlX, tlY, trX, trY, brX]` and every sled position,
   minimizing the sum of squared belt-length residuals
3. Solve each LM step with a Schur complement (5×5 anchor system + 2×2 sled blocks) using
   analytic Jacobians and adaptive damping `λ`
4. Retry up to 10 times from the firmware's fixed perturbation table, keeping the best result

The algorithm uses the firmware constants:
- Initial `λ = 0.001`, ×10 on rejection, ×0.1 on acceptance
- Up to 100 iterations per attempt, convergence when the anchor step norm < 1e-4 mm
- Fitness gates: RMS ≤ 5 mm, max residual ≤ 15 mm

## Related Tools

- **Machine Simulator** (`index.html`): Simulates full calibration process with synthetic data
- **ESP3D-WEBUI**: The actual web interface on your Maslow machine
- **Test Page** (`test.html`): Validates the shared computation library

## Support

For issues or questions:
- GitHub Issues: https://github.com/MaslowCNC/Maslow_4/issues
- Forum: https://forums.maslowcnc.com/
