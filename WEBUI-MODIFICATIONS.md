# WebUI Modifications

This document describes modifications made to the ESP3D-WEBUI for FluidNC.

## Modified Features

### Smart Auto-Scroll for Terminal Output

**Feature Request**: Serial output keeps scrolling, preventing users from reviewing historical output while new data arrives.

**Solution**: 
- Added a "Scroll to Bottom" button that appears when the user has scrolled away from the bottom of the terminal output
- The terminal now intelligently pauses auto-scrolling when the user scrolls up
- A floating button with a down chevron icon appears in the bottom-right corner when auto-scroll is paused
- Clicking the button instantly scrolls to the bottom and resumes auto-scrolling
- Auto-scroll automatically resumes if the user manually scrolls to within 5 pixels of the bottom

**Modified Files**:
- `Terminal.js`: Added scroll to bottom button and handler function
- `_index.scss` (CNC target): Added styling for the floating button
- `_panel.scss`: Made panel position relative for button positioning

## How to Rebuild the WebUI

The WebUI is built from the ESP3D-WEBUI 3.0 source code.

### Prerequisites
- Node.js v20+ 
- npm

### Build Steps

1. Clone ESP3D-WEBUI repository:
```bash
git clone https://github.com/luc-github/ESP3D-WEBUI.git
cd ESP3D-WEBUI
```

2. Apply the modifications from this repository (or make changes manually):
   - Modify `src/components/Panels/Terminal.js`
   - Modify `src/targets/CNC/style/_index.scss`
   - Modify `src/style/components/_panel.scss`

3. Install dependencies:
```bash
npm install
```

4. Build for CNC-GRBL target:
```bash
npm run cnc-grbl
```

5. Copy the built file:
```bash
cp dist/CNC/GRBL/index.html.gz /path/to/FluidNC/FluidNC/data/index.html.gz
```

### Build Variants

FluidNC uses the CNC-GRBL variant. Other available variants:
- `npm run cnc-grblhal` - For grblHAL firmware
- See ESP3D-WEBUI README for complete list

## Testing

After rebuilding and uploading the firmware with filesystem, the terminal should:
1. Auto-scroll by default when new data arrives
2. Pause auto-scrolling when user scrolls up
3. Show a floating "Scroll to Bottom" button when paused
4. Resume auto-scrolling when button is clicked or user scrolls to bottom

## Version Information

- ESP3D-WEBUI: 3.0.0
- FluidNC: See main README
