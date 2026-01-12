# Absolute XY Coordinate Input Feature

## Overview
The tablet view now supports entering and moving to absolute XY coordinates, allowing precise positioning of the machine to specific locations on the work surface.

## What's New
- **Clickable Position Displays**: X, Y, and Z coordinate displays are now interactive
- **Go To XY Button**: New button to execute absolute positioning moves
- **Visual Feedback**: Modified coordinates are highlighted to show pending moves
- **Absolute Positioning**: Move to specific coordinates referenced to machine origin, not current home

## How to Use

### 1. Entering Absolute Coordinates

1. **Click on a coordinate display**: Tap or click on any of the X, Y, or Z coordinate values in the tablet view
   - The displays are underlined to indicate they're clickable
   - A tooltip will appear on hover: "Click to enter absolute X/Y/Z coordinate"

2. **Enter the desired coordinate**: Use the on-screen numpad to enter the absolute coordinate value
   - Enter the value in current units (mm or inches, depending on your unit setting)
   - The display will show your entered value
   - The coordinate will be highlighted in yellow to indicate it's a target position

3. **Set multiple coordinates**: You can set both X and Y coordinates before moving
   - Click X coordinate, enter value, click "Set"
   - Click Y coordinate, enter value, click "Set"
   - Both will be highlighted, ready for the move

### 2. Moving to Absolute Position

1. **Press "Go To XY" button**: Located next to the coordinate displays
   - This button executes the move to the entered X and Y coordinates
   - The machine will move using absolute positioning (G90 G0 command)

2. **Safety checks**:
   - The system will verify the machine is homed before allowing the move
   - If not homed, you'll see: "Cannot move to absolute position. Belt lengths are unknown."
   - The move command will appear in the serial messages log

3. **Visual feedback**:
   - The yellow highlighting will automatically clear when the machine reaches the target
   - Coordinates are considered "reached" when within 0.1mm of the target

### 3. Important Notes

**Absolute vs. Relative Positioning**:
- This feature uses **absolute coordinates** referenced to the machine's origin (0,0)
- This is different from the home position you may have set with "Define XY Home"
- Absolute coordinates are in the machine coordinate system, not work coordinate system

**Unit Awareness**:
- Enter coordinates in the currently selected units (mm or inch)
- Toggle units using the "mm" / "Inch" button if needed
- The system respects your unit setting when executing moves

**Homing Requirement**:
- The machine must be calibrated/homed before using absolute positioning
- This ensures the machine knows where it is in space
- Run calibration if you see "Belt lengths are unknown" errors

## Example Workflows

### Moving to a Specific Corner
1. Enter X coordinate: 0
2. Enter Y coordinate: 0
3. Press "Go To XY"
→ Machine moves to origin (bottom-left corner)

### Positioning for Multiple Cuts
1. Enter X: 100, Y: 50
2. Press "Go To XY" → Machine moves to first position
3. Perform operation
4. Enter X: 200, Y: 50
5. Press "Go To XY" → Machine moves to second position
6. Perform operation

### Checking Specific Points
1. Note your design coordinates from CAD software
2. Enter those exact coordinates
3. Use "Go To XY" to verify tool reaches the correct positions
4. Useful for checking alignment before running full job

## Technical Details

**G-code Commands Used**:
- `G90 G0 X### Y###` - Absolute positioning rapid move
- G90 ensures coordinates are interpreted as absolute
- G0 is rapid (non-cutting) movement

**Coordinate System**:
- Machine Position (absolute) - referenced to machine origin
- Work Position (relative) - referenced to your defined XY home
- This feature uses machine position for precise, repeatable moves

**Precision**:
- Coordinates displayed to 2 decimal places in mm
- Coordinates displayed to 4 decimal places in inches
- Target reached tolerance: 0.1mm

## Troubleshooting

**"Cannot move to absolute position. Belt lengths are unknown."**
- Solution: Run machine calibration first (Setup button → Find Anchor Locations)

**Coordinates don't highlight when entered**
- Check that you clicked "Set" on the numpad after entering the value
- Make sure you're clicking on the coordinate displays (X, Y, or Z values)

**Machine doesn't move after pressing "Go To XY"**
- Check serial messages for error details
- Verify coordinates are valid and within machine limits
- Ensure machine is in Idle state, not Alarm or error state

**Coordinates stay highlighted after move**
- The machine may not have reached the target position yet
- Tolerance is 0.1mm - ensure machine actually moved to target
- Check for mechanical issues preventing accurate positioning

## Safety Considerations

⚠️ **Important Safety Notes**:
- Always verify coordinates are within your machine's work area
- Double-check units (mm vs. inches) before executing moves
- Ensure workpiece and clamps won't interfere with the move path
- The machine will take the most direct path - no obstacle avoidance
- Consider Z-axis height before moving to avoid collisions

## Related Features

- **Define XY Home**: Sets work coordinate system origin
- **Move to XY Home**: Returns to work coordinate origin
- **Jog Controls**: Relative movements from current position
- **Toggle Units**: Switch between mm and inches

