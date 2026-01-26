/**
 * Numpad Keyboard Integration for Jogging and Dynamic Step Selection
 * 
 * This module implements full keyboard mapping for the numeric keypad (Numpad)
 * to allow users to jog the machine, adjust step increments, and manage job
 * execution without relying on a mouse or touch screen.
 * 
 * Numpad Mapping:
 * - 8: Y+ (Up) - Move gantry up by current Step value
 * - 2: Y- (Down) - Move gantry down by current Step value
 * - 4: X- (Left) - Move gantry left by current Step value
 * - 6: X+ (Right) - Move gantry right by current Step value
 * - 7, 9, 1, 3: Diagonal Jog (Top-Left, Top-Right, Bottom-Left, Bottom-Right)
 * - 5: STOP/ABORT - Immediate halt of all current movement commands
 * - +: Z-Axis Up - Retract the tool (Z-axis)
 * - -: Z-Axis Down - Extend the tool (Z-axis)
 * - 0 (Ins): Step UP - Cycle step size UP (e.g., 0.1 -> 1 -> 10 -> 100mm)
 * - . (Del): Step DOWN - Cycle step size DOWN (e.g., 100 -> 10 -> 1 -> 0.1mm)
 * - /: Home XY - Execute "Define XY Home"
 * - *: Home Z - Execute "Define Z Home"
 * - Enter: START/RESUME - Execute "Cycle Start" (with safety interlocks)
 */

// Available step sizes in mm
const STEP_SIZES = [0.1, 1, 10, 100];

// Enter key long-press configuration (in milliseconds)
const ENTER_LONG_PRESS_DURATION = 3000;

// State tracking for Enter key long-press
let enterPressStartTime = null;
let enterLongPressTimeout = null;
let enterKeyArmed = false;

/**
 * Get the current step size from the UI
 * @param {string} axis - Either 'M' for XY or 'Z' for Z-axis
 * @returns {number} Current step size
 */
function getCurrentStepSize(axis = 'M') {
	const elementId = axis === 'Z' ? 'disZ' : 'disM';
	const elem = id(elementId);
	if (!elem) {
		return 1; // Default fallback
	}
	const value = parseFloat(elem.textContent || elem.value || '1');
	return isNaN(value) ? 1 : value;
}

/**
 * Set the current step size in the UI
 * @param {string} axis - Either 'M' for XY or 'Z' for Z-axis
 * @param {number} value - New step size value
 */
function setCurrentStepSize(axis, value) {
	const elementId = axis === 'Z' ? 'disZ' : 'disM';
	const elem = id(elementId);
	if (elem) {
		// Update both textContent and value to support different element types
		if ('textContent' in elem) {
			elem.textContent = value.toString();
		}
		if ('value' in elem) {
			elem.value = value.toString();
		}
		// Trigger a change event to notify other components
		elem.dispatchEvent(new Event('change', { bubbles: true }));
	}
}

/**
 * Cycle the step size up or down
 * @param {boolean} up - true to cycle up, false to cycle down
 */
function cycleStepSize(up) {
	const currentStep = getCurrentStepSize('M');
	
	// Find the current index in STEP_SIZES
	let currentIndex = STEP_SIZES.indexOf(currentStep);
	
	// If current value not in list, find closest
	if (currentIndex === -1) {
		for (let i = 0; i < STEP_SIZES.length; i++) {
			if (currentStep < STEP_SIZES[i]) {
				currentIndex = up ? i : Math.max(0, i - 1);
				break;
			}
		}
		if (currentIndex === -1) {
			currentIndex = up ? STEP_SIZES.length - 1 : 0;
		}
	} else {
		// Move to next/previous in list
		if (up) {
			currentIndex = Math.min(STEP_SIZES.length - 1, currentIndex + 1);
		} else {
			currentIndex = Math.max(0, currentIndex - 1);
		}
	}
	
	const newStep = STEP_SIZES[currentIndex];
	setCurrentStepSize('M', newStep);
	
	// Also update Z step to match (common practice)
	setCurrentStepSize('Z', newStep);
	
	// Provide visual feedback
	console.log(`Step size changed to: ${newStep}mm`);
}

/**
 * Execute a jog command for XY axes
 * @param {string} axis - 'X' or 'Y'
 * @param {string} direction - '' for positive, '-' for negative
 */
function executeXYJog(axis, direction) {
	if (getChecked("lock_UI") !== "false") {
		console.log('Cannot jog: UI is locked');
		return;
	}
	
	if (typeof SendJogcommand !== 'function') {
		console.error('SendJogcommand function not available');
		return;
	}
	
	const stepSize = getCurrentStepSize('M');
	// Convention: Y10 for positive, Y-10 for negative (no + sign for positive)
	const cmd = `${axis}${direction}${stepSize}`;
	console.log(`Executing XY jog: ${cmd}`);
	
	try {
		SendJogcommand(cmd, 'XYfeedrate');
	} catch (error) {
		console.error('Error executing XY jog:', error);
	}
}

/**
 * Execute a combined XY jog command (for diagonal movement)
 * @param {string} xDirection - '' for X+, '-' for X-
 * @param {string} yDirection - '' for Y+, '-' for Y-
 */
function executeXYDiagonalJog(xDirection, yDirection) {
	if (getChecked("lock_UI") !== "false") {
		return;
	}
	
	const stepSize = getCurrentStepSize('M');
	// Build both X and Y commands
	const xCmd = `X${xDirection}${stepSize}`;
	const yCmd = `Y${yDirection}${stepSize}`;
	const combinedCmd = `${xCmd} ${yCmd}`;
	SendJogcommand(combinedCmd, 'XYfeedrate');
}

/**
 * Execute a jog command for Z axis
 * @param {string} direction - '' for positive (up), '-' for negative (down)
 */
function executeZJog(direction) {
	if (getChecked("lock_UI") !== "false") {
		return;
	}
	
	const stepSize = getCurrentStepSize('Z');
	// Convention: Z10 for positive, Z-10 for negative (no + sign for positive)
	const cmd = `Z${direction}${stepSize}`;
	SendJogcommand(cmd, 'Zfeedrate');
}

/**
 * Execute abort/stop command
 */
function executeAbort() {
	console.log('Executing ABORT command');
	
	// Check if SendPrinterCommand is available
	if (typeof SendPrinterCommand !== 'function') {
		console.error('SendPrinterCommand function not available');
		return;
	}
	
	// Send abort command directly
	try {
		SendPrinterCommand("abort", true);
		console.log('ABORT command sent successfully');
	} catch (error) {
		console.error('Error sending ABORT command:', error);
	}
}

/**
 * Execute home XY command
 */
function executeHomeXY() {
	if (getChecked('lock_UI') === "true") {
		return;
	}
	// Delay between homing commands to prevent command queue overflow
	const HOME_COMMAND_DELAY_MS = 100;
	
	SendHomecommand('G28 X0'); // Home X
	setTimeout(() => {
		SendHomecommand('G28 Y0'); // Home Y after allowing first command to be processed
	}, HOME_COMMAND_DELAY_MS);
	console.log('Home XY executed');
}

/**
 * Execute home Z command
 */
function executeHomeZ() {
	if (getChecked('lock_UI') === "true") {
		return;
	}
	SendHomecommand('G28 Z0');
	console.log('Home Z executed');
}

/**
 * Check if it's safe to start a job
 * @returns {boolean} true if safe to start
 */
function canStartJob() {
	// Check if machine is in IDLE state
	if (typeof grbl !== 'undefined' && grbl.stateName) {
		if (grbl.stateName !== 'Idle') {
			console.warn(`Cannot start job: Machine state is '${grbl.stateName}', expected 'Idle'`);
			return false;
		}
	}
	
	// Check if a G-Code file is loaded
	if (typeof gCodeLoaded !== 'undefined' && !gCodeLoaded) {
		console.warn('Cannot start job: No G-Code file loaded');
		return false;
	}
	
	return true;
}

/**
 * Execute cycle start/resume command
 * This function is only called after the long-press requirement is met
 */
function executeCycleStart() {
	if (!canStartJob()) {
		enterKeyArmed = false;
		return;
	}
	
	// Send the cycle start command
	// This uses the pause/resume mechanism which is state-aware
	if (typeof grblPanelResume === 'function') {
		grblPanelResume();
	} else {
		SendRealtimeCmd(0x7e); // Cycle Start/Resume
	}
	
	console.log('Cycle Start executed');
	enterKeyArmed = false;
}

/**
 * Handle Enter key press (start of long-press)
 */
function handleEnterKeyDown() {
	if (enterPressStartTime !== null) {
		return; // Already pressed
	}
	
	enterPressStartTime = Date.now();
	enterKeyArmed = false;
	
	// Set up a timeout for the long-press duration
	enterLongPressTimeout = setTimeout(() => {
		enterKeyArmed = true;
		console.log('Enter key armed - release to start cycle');
		// Optional: Show visual feedback that the key is armed
		// This could trigger a UI update to show "ARMED" status
	}, ENTER_LONG_PRESS_DURATION);
}

/**
 * Handle Enter key release (end of long-press)
 */
function handleEnterKeyUp() {
	if (enterPressStartTime === null) {
		return; // Not pressed
	}
	
	const pressDuration = Date.now() - enterPressStartTime;
	
	// Clear the timeout
	if (enterLongPressTimeout) {
		clearTimeout(enterLongPressTimeout);
		enterLongPressTimeout = null;
	}
	
	// Check if it was a long press and the key is armed
	if (enterKeyArmed && pressDuration >= ENTER_LONG_PRESS_DURATION) {
		executeCycleStart();
	} else if (pressDuration < ENTER_LONG_PRESS_DURATION) {
		console.log(`Enter key released too early (${pressDuration}ms < ${ENTER_LONG_PRESS_DURATION}ms required)`);
	}
	
	// Reset state
	enterPressStartTime = null;
	enterKeyArmed = false;
}

/**
 * Check if a key event is from the numpad
 * @param {KeyboardEvent} event
 * @returns {boolean}
 */
function isNumpadKey(event) {
	// Check if the key is from the numpad location
	if (event.location === KeyboardEvent.DOM_KEY_LOCATION_NUMPAD) {
		return true;
	}
	
	// Check for numpad-specific key codes
	const numpadCodes = [
		'Numpad0', 'Numpad1', 'Numpad2', 'Numpad3', 'Numpad4',
		'Numpad5', 'Numpad6', 'Numpad7', 'Numpad8', 'Numpad9',
		'NumpadAdd', 'NumpadSubtract', 'NumpadMultiply', 'NumpadDivide',
		'NumpadDecimal', 'NumpadEnter'
	];
	
	return numpadCodes.includes(event.code);
}

/**
 * Check if the dashboard/controls panel has focus
 * Keyboard listeners should only be active when the dashboard or jogging widget has focus
 * to prevent accidental triggers while typing in other input fields
 * @returns {boolean} true if numpad controls should be active
 */
function isNumpadControlActive() {
	// Check if we're on the main tab (dashboard) or tablet tab
	const maintab = id('maintab');
	const tablettab = id('tablettab');
	
	const mainTabVisible = maintab && maintab.style.display !== 'none';
	const tabletTabVisible = tablettab && tablettab.style.display !== 'none';
	
	if (!mainTabVisible && !tabletTabVisible) {
		return false;
	}
	
	// Check if an input or textarea has focus
	const activeElement = document.activeElement;
	if (activeElement && (activeElement.tagName === 'INPUT' || activeElement.tagName === 'TEXTAREA')) {
		// Exception: If the active element is part of the controls panel, allow numpad
		const controlPanel = id('controlPanel');
		if (controlPanel && !controlPanel.contains(activeElement)) {
			return false;
		}
	}
	
	// Check if UI is locked
	if (getChecked('lock_UI') === 'true') {
		// Even when locked, allow ABORT (handled separately)
		return false;
	}
	
	return true;
}

/**
 * Main keydown event handler for numpad
 * @param {KeyboardEvent} event
 */
function handleNumpadKeyDown(event) {
	// Only handle keys from the actual numpad
	if (!isNumpadKey(event)) {
		return;
	}
	
	console.log(`Numpad key detected: ${event.key} (code: ${event.code})`);
	
	// Special case: Always allow numpad 5 (ABORT) to work, even when locked
	// Note: Some keyboards may report 'Clear' instead of '5' when NumLock is off
	if (event.key === '5' || event.key === 'Clear') {
		console.log('Executing ABORT command');
		executeAbort();
		event.preventDefault();
		return;
	}
	
	// For all other keys, check if numpad control is active
	const isActive = isNumpadControlActive();
	console.log(`Numpad control active: ${isActive}`);
	if (!isActive) {
		console.log('Numpad control not active - checking conditions:');
		console.log('  - Main tab visible:', id('maintab') && id('maintab').style.display !== 'none');
		console.log('  - Tablet tab visible:', id('tablettab') && id('tablettab').style.display !== 'none');
		console.log('  - UI locked:', getChecked('lock_UI'));
		console.log('  - Active element:', document.activeElement?.tagName);
		return;
	}
	
	// Handle the key based on numpad mapping
	switch (event.key) {
		// XY Jogging
		case '8':
			executeXYJog('Y', ''); // Y+
			event.preventDefault();
			break;
		case '2':
			executeXYJog('Y', '-'); // Y-
			event.preventDefault();
			break;
		case '4':
			executeXYJog('X', '-'); // X-
			event.preventDefault();
			break;
		case '6':
			executeXYJog('X', ''); // X+
			event.preventDefault();
			break;
		
		// Diagonal Jogging
		case '7':
			executeXYDiagonalJog('-', ''); // Top-Left (X-, Y+)
			event.preventDefault();
			break;
		case '9':
			executeXYDiagonalJog('', ''); // Top-Right (X+, Y+)
			event.preventDefault();
			break;
		case '1':
			executeXYDiagonalJog('-', '-'); // Bottom-Left (X-, Y-)
			event.preventDefault();
			break;
		case '3':
			executeXYDiagonalJog('', '-'); // Bottom-Right (X+, Y-)
			event.preventDefault();
			break;
		
		// Z-Axis Control
		case '+':
			executeZJog('');
			event.preventDefault();
			break;
		case '-':
			executeZJog('-');
			event.preventDefault();
			break;
		
		// Step Size Cycling
		case '0':
		case 'Insert':
			cycleStepSize(true); // Cycle UP
			event.preventDefault();
			break;
		case '.':
		case 'Delete':
			cycleStepSize(false); // Cycle DOWN
			event.preventDefault();
			break;
		
		// Home Commands
		case '/':
			executeHomeXY();
			event.preventDefault();
			break;
		case '*':
			executeHomeZ();
			event.preventDefault();
			break;
		
		// Cycle Start (with long-press)
		case 'Enter':
			handleEnterKeyDown();
			event.preventDefault();
			break;
	}
}

/**
 * Main keyup event handler for numpad
 * @param {KeyboardEvent} event
 */
function handleNumpadKeyUp(event) {
	// Only handle keys from the actual numpad
	if (!isNumpadKey(event)) {
		return;
	}
	
	// Handle Enter key release for long-press detection
	if (event.key === 'Enter') {
		handleEnterKeyUp();
		event.preventDefault();
	}
}

/**
 * Initialize the numpad jog keyboard handler
 * This should be called after the page loads and other components are initialized
 */
function initNumpadJog() {
	// Add event listeners for numpad keys
	window.addEventListener('keydown', handleNumpadKeyDown);
	window.addEventListener('keyup', handleNumpadKeyUp);
	
	console.log('Numpad jogging keyboard handler initialized');
	console.log('Numpad mapping:');
	console.log('  8/2/4/6: Y+/Y-/X-/X+');
	console.log('  7/9/1/3: Diagonal jog');
	console.log('  5: ABORT/STOP');
	console.log('  +/-: Z up/down');
	console.log('  0/.: Step size up/down');
	console.log('  /: Home XY');
	console.log('  *: Home Z');
	console.log('  Enter (long-press 3s): Cycle Start');
}

// Initialize when the page is ready
// This will run after all other scripts have loaded
if (document.readyState === 'loading') {
	document.addEventListener('DOMContentLoaded', initNumpadJog);
} else {
	// DOM already loaded, initialize immediately
	initNumpadJog();
}
