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
- Process all measurements
- Iteratively refine anchor positions
- Display progress in the log
- Show final results with fitness score
- **Visualize the search process** with a 2D plot showing:
  - **Red points**: Phase 1 diagonal search for optimal radius
  - **Blue points**: Phase 2 arc search for best aspect ratio
  - **Green star**: Final best configuration found

### 5. Interpret Results

The results show:
- **Optimized anchor positions**: The computed X,Y coordinates
- **Fitness score**: How well the solution matches the measurements
  - Above 0.5: Good result
  - Below 0.5: May need better initial guess or data quality issues
- **Search visualization**: A 2D plot showing which points were tested during the rectangular optimization search

## Code Sharing

This tool uses the **exact same calibration computation code** as the ESP3D-WEBUI. The shared library is located at `../../ESP3D-WEBUI/www/js/calibration-computation.js`, ensuring identical behavior between:
- This data parser
- The machine simulator (`index.html`)
- The actual ESP3D-WEBUI calibration

The data parser now includes the **retry logic with aspect ratio-based starting positions**, matching the real machine behavior:
- If fitness is below 0.5 threshold, the algorithm retries with predefined aspect ratios instead of random points
- The 5 aspect ratios tested are: 2:1, 1.5:1, 1:1, 1:1.5, and 1:2 (width:height)
- These points are calculated on the arc found in Phase 2 using the optimal radius
- Up to 10 retry attempts are made (5 with aspect ratios, then 5 with original guess) before giving up
- The best result across all attempts is displayed if maximum retries are reached
- This ensures offline testing behaves identically to the machine

## Visualization

When the tool runs rectangular optimization to find better starting positions (triggered when initial fitness is low or the frame is nearly square), it displays a **2D visualization** showing:

- **Phase 1 - Diagonal Search** (Red points): Shows points tested along the diagonal to find the optimal radius. The search uses ternary search to efficiently narrow down the best radius between 100mm and 5000mm.

- **Phase 2 - Arc Search** (Blue points): Shows points tested along an arc at the optimal radius to find the best width/height aspect ratio. The search uses ternary search to efficiently find the optimal angle.

- **Best Configuration** (Green star): Marks the optimal rectangular configuration found by the search.

- **Reference Lines**: A faint red diagonal line shows the Phase 1 search path, and a faint blue arc shows the Phase 2 search path.

The visualization updates in real-time as the search progresses, providing visual feedback on how the algorithm explores the solution space. This helps understand:
- How many points are tested (typically 24 for Phase 1, 26 for Phase 2)
- The distribution of test points across the search space
- The location of the optimal solution relative to tested points

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
- **Initial guess matters**: Try to get within ~100mm of actual positions
- **Check fitness score**: Scores above 0.5 indicate reliable results
- **Look for patterns**: Consistent low fitness may indicate measurement issues

## Technical Details

The computation uses the "magnetically attracted lines" algorithm:
1. Draw lines from each anchor with measured lengths
2. Adjust line angles to make endpoints converge
3. Move anchors to minimize endpoint distances
4. Iterate until convergence

The algorithm uses:
- Progressive refinement with 8 step sizes (0.1 → 0.00000001)
- Center-of-mass computation from 3 lines
- Furthest-anchor adjustment strategy
- Maximum 200,000 iterations with stagnation detection

## Related Tools

- **Machine Simulator** (`index.html`): Simulates full calibration process with synthetic data
- **ESP3D-WEBUI**: The actual web interface on your Maslow machine
- **Test Page** (`test.html`): Validates the shared computation library

## Support

For issues or questions:
- GitHub Issues: https://github.com/MaslowCNC/Maslow_4/issues
- Forum: https://forums.maslowcnc.com/
