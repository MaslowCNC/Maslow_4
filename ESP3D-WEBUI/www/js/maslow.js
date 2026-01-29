// When we can change to proper ESM - uncomment this
// import M from "constants";

/** Maslow Status 
 * Initializes with state 0 (UNKNOWN) until firmware sends actual state update
 */
let maslowStatus = { homed: false, extended: false, state: 0 };

/** State Transitions Map - fetched from firmware at startup */
let stateTransitionsMap = null;

/** State Definitions - fetched from firmware at startup */
let stateDefinitions = null;

/** Callback to handle STATEDEFS response when it arrives via WebSocket */
let pendingStateDataCallback = null;

/** This keeps track of when we saw the last heartbeat from the machine */
//I think this is not used anymore and can be removed now
let lastHeartBeatTime = new Date().getTime();

const err = "error: ";
// When we can change to proper ESM - prefix these const strings and functions with 'export' (minus the quotes of course)
const MaslowErrMsgKeyValueCantUse = `${err}Could not use supplied key-value pair.`;
const MaslowErrMsgNoKey = `${err}No key supplied for value.`;
const MaslowErrMsgNoValue = `${err}No value supplied for key.`;
const MaslowErrMsgNoMatchingKey = `${err}Could not find key for value in reference table.`;
const MaslowErrMsgKeyValueSuffix = "This is probably a programming error\nKey-Value pair supplied was:";

/*
* Updates the dynamic buttons to reflect the current state of the machine
UNKNOWN 0
    -Retract All
    -Apply Tension

RETRACTING 1
    -No buttons

RETRACTED 2
    -Retract All
    -Extend All

EXTENDING 3
    -No Buttons

EXTENDEDOUT 4 //Extended is a reserved word
    -Retract All
    -Apply Tension
    -Calibrate

TAKING_SLACK 5
    -No buttons

CALIBRATION_IN_PROGRESS 6
    -No buttons

READY_TO_CUT 7
    -Retract All
    -Apply Tension
    -Release Tension
*/

/** Check if jogging is allowed based on current state */
const isJoggingAllowed = () => {
	// READY_TO_CUT is state 7
	return maslowStatus.state === 7;
};

/** Update tablet tab jog button visual state based on whether jogging is allowed */
const updateTabletJogUIState = () => {
	console.log("  updateTabletJogUIState: checking jogging state...");
	console.log("  Jogging allowed:", isJoggingAllowed());
	
	// Purple jog arrow button IDs on the tablet tab (XY arrows only - Z buttons always enabled)
	const jogButtonIds = [
		'tablettab_topLeft', 'tablettab_top', 'tablettab_topRight',
		'tablettab_left', 'tablettab_right',
		'tablettab_bottomLeft', 'tablettab_bottom', 'tablettab_bottomRight'
	];

	// Get the state name from firmware definitions
	const currentStateInfo = stateDefinitions ? stateDefinitions[maslowStatus.state] : null;
	const stateName = currentStateInfo ? currentStateInfo.name : "current state";
	const tooltipText = `Jogging is only available when in state Ready To Cut (currently in ${stateName})`;

	jogButtonIds.forEach(buttonId => {
		const button = document.getElementById(buttonId);
		if (!button) {
			console.log(`  WARNING: Jog button "${buttonId}" not found in DOM!`);
			return;
		}

		if (!isJoggingAllowed()) {
			// Apply light grey overlay effect
			button.style.opacity = "0.5";
			button.style.cursor = "not-allowed";
			button.title = tooltipText;
			
			// Add a visual overlay div if it doesn't exist
			let overlay = button.querySelector('.jog-disabled-overlay');
			if (!overlay) {
				overlay = document.createElement("div");
				overlay.className = "jog-disabled-overlay";
				overlay.style.position = "absolute";
				overlay.style.top = "0";
				overlay.style.left = "0";
				overlay.style.width = "100%";
				overlay.style.height = "100%";
				overlay.style.backgroundColor = "rgba(211, 211, 211, 0.6)";
				overlay.style.cursor = "not-allowed";
				overlay.style.zIndex = "10";
				overlay.title = tooltipText; // Set tooltip on overlay so it shows
				
				// Ensure button has position relative for overlay
				if (button.style.position !== "relative" && button.style.position !== "absolute") {
					button.style.position = "relative";
				}
				button.appendChild(overlay);
			} else {
				// Update tooltip if overlay already exists
				overlay.title = tooltipText;
			}
		} else {
			// Remove grey overlay effect
			button.style.opacity = "";
			button.style.cursor = "";
			button.title = "";
			
			// Remove overlay div if it exists
			const overlay = button.querySelector('.jog-disabled-overlay');
			if (overlay) {
				overlay.remove();
			}
		}
	});
};

const updateDynamicButtons = () => {
	console.log("=== updateDynamicButtons called ===");
	console.log("Current state:", maslowStatus.state);
	console.log("stateDefinitions loaded:", !!stateDefinitions);
	console.log("stateTransitionsMap loaded:", !!stateTransitionsMap);
	
	const stateLabel = document.getElementById("state-label");
	const mainStateLabel = document.getElementById("main-state-label");
	const mainStateLabelContainer = document.getElementById("main-state-label-container");

	console.log("State label element exists:", !!stateLabel);
	console.log("Main state label element exists:", !!mainStateLabel);

	const greenBackground = "#4aa85c"
	const greyBackground = "#a0a0a0"
	
	// Get current state info from fetched definitions
	// Show "Unknown" if definitions aren't loaded yet or state not found
	const currentStateInfo = stateDefinitions ? stateDefinitions[maslowStatus.state] : null;
	const stateName = currentStateInfo ? currentStateInfo.name : "Unknown";
	const stateBackgroundColor = currentStateInfo ? currentStateInfo.backgroundColor : "#f8d7da";
	
	console.log("State name:", stateName);
	console.log("State background color:", stateBackgroundColor);
	
	// Update state label - always show current state even if definitions aren't loaded
	if (stateLabel) {
		stateLabel.innerHTML = "State: " + stateName;
	} else {
		console.log("WARNING: state-label element not found!");
	}
	if (mainStateLabel) {
		mainStateLabel.innerHTML = "State: " + stateName;
		if (mainStateLabelContainer) {
			mainStateLabelContainer.style.backgroundColor = stateBackgroundColor;
		}
	}

	// If we don't have state definitions yet, skip button updates
	if (!stateDefinitions) {
		console.log("No state definitions loaded yet - skipping button updates");
		return;
	}
	
	console.log("Proceeding with button updates...");

	// Build button to state map dynamically from state definitions
	// Only include states that have button labels
	const buttonToStateMap = {};
	for (const stateId in stateDefinitions) {
		const stateDef = stateDefinitions[stateId];
		if (stateDef && stateDef.buttonLabel) {
			// Map button IDs based on button labels
			// This matches the original button IDs in the HTML
			const buttonIdMap = {
				"Retract All": "tablettab_cal_retract",
				"Extend All": "tablettab_cal_extend",
				"Apply Tension": "tablettab_cal_tense",
				"Find Anchor Locations": "tablettab_cal_calibrate",
				"Release Tension": "tablettab_cal_relax"
			};
			const buttonId = buttonIdMap[stateDef.buttonLabel];
			if (buttonId) {
				buttonToStateMap[buttonId] = stateDef.id;
			}
		}
	}
	
	console.log("Button to state map:", buttonToStateMap);

	// Update each button based on whether its transition is allowed
	for (const [buttonId, targetState] of Object.entries(buttonToStateMap)) {
		const button = document.getElementById(buttonId);
		console.log(`Checking button ${buttonId} (target state ${targetState}):`, !!button);
		if (button) {
			const allowed = isTransitionAllowed(maslowStatus.state, targetState);
			console.log(`  Transition ${maslowStatus.state} -> ${targetState} allowed:`, allowed);
			button.style.backgroundColor = allowed ? greenBackground : greyBackground;
			
			// Update button text from state definitions if available
			const targetStateInfo = stateDefinitions[targetState];
			if (targetStateInfo && targetStateInfo.buttonLabel) {
				button.textContent = targetStateInfo.buttonLabel;
			}
		} else {
			console.log(`  WARNING: Button element "${buttonId}" not found in DOM!`);
		}
	}
	
	// Update tablet tab jog UI state
	console.log("Updating tablet jog UI state...");
	updateTabletJogUIState();
	
	// Update control buttons row based on state
	console.log("Updating control buttons row...");
	updateControlButtonsRow();
	
	console.log("=== updateDynamicButtons completed ===");
}

/** Update the control buttons row on the Maslow tab based on current state */
const updateControlButtonsRow = () => {
	console.log("  updateControlButtonsRow: starting...");
	
	// If we don't have state definitions yet, skip the update
	if (!stateDefinitions) {
		console.log("  No state definitions - skipping control buttons row update");
		return;
	}
	
	const READY_TO_CUT = 7;
	const CALIBRATION_IN_PROGRESS = 6;
	
	// Get the control buttons row container
	const controlButtonsRow = document.getElementById("tablettab_control_buttons_row");
	const releaseTensionBtn = document.getElementById("tablettab_release_tension");
	
	console.log("  Control buttons row element exists:", !!controlButtonsRow);
	console.log("  Release tension button element exists:", !!releaseTensionBtn);
	
	if (!controlButtonsRow) {
		console.log("  WARNING: tablettab_control_buttons_row element not found!");
		return;
	}
	
	// Always show the release tension button (make it visible)
	if (releaseTensionBtn) {
		releaseTensionBtn.style.display = "block";
		
		// Color it based on whether transition is allowed
		const releaseTensionState = 8; // RELEASE_TENSION state
		const allowed = isTransitionAllowed(maslowStatus.state, releaseTensionState);
		releaseTensionBtn.style.backgroundColor = allowed ? "#4aa85c" : "#a0a0a0";
		releaseTensionBtn.style.cursor = allowed ? "pointer" : "not-allowed";
		releaseTensionBtn.style.pointerEvents = allowed ? "auto" : "none";
	}
	
	// Clear the row first
	controlButtonsRow.innerHTML = "";
	
	if (maslowStatus.state === READY_TO_CUT) {
		// Show play/pause, stop, and alert/idle buttons in ready-to-cut state
		controlButtonsRow.style.gridTemplateColumns = "33% 33% 33%";
		
		controlButtonsRow.innerHTML = `
			<div id="tablettab_gcode_play" class="maslow-grid-item maslow-grid-item-btn"
				style="background-color: #4aa85c;"><canvas id="playBtn" style="width: 100%; height: 100%"></canvas></div>
			<div id="tablettab_gcode_stop" class="maslow-grid-item maslow-grid-item-btn"
				style="background-color: #ce654c;"><canvas id="stopBtn" style="width: 100%; height: 100%"></canvas></div>
			<div id="systemStatus" class="maslow-grid-item system-status">Idle</div>
		`;
		
		// Redraw the play and stop button canvases
		drawPlayButton();
		drawStopButton();
	} else {
		// In other states, show buttons for allowed transitions (except release tension)
		// Build list of allowed transitions for current state
		const currentStateInfo = stateDefinitions[maslowStatus.state];
		const allowedTransitions = currentStateInfo ? currentStateInfo.allowedTransitions : [];
		
		// Filter out RELEASE_TENSION (state 8) since it has its own dedicated button
		const RELEASE_TENSION = 8;
		const filteredTransitions = allowedTransitions.filter(stateId => stateId !== -1 && stateId !== RELEASE_TENSION);
		
		// For calibration state, also add stop button
		let buttons = [];
		
		// Add transition buttons
		filteredTransitions.forEach(targetStateId => {
			const targetStateInfo = stateDefinitions[targetStateId];
			if (targetStateInfo && targetStateInfo.buttonLabel) {
				buttons.push({
					type: 'transition',
					label: targetStateInfo.buttonLabel,
					stateId: targetStateId,
					color: "#4aa85c" // Green for allowed transitions
				});
			}
		});
		
		// Add stop button for calibration state
		if (maslowStatus.state === CALIBRATION_IN_PROGRESS) {
			buttons.push({
				type: 'stop',
				label: 'Stop',
				color: "#ce654c" // Red for stop button
			});
		}
		
		// Calculate grid columns based on number of buttons
		const numButtons = buttons.length;
		if (numButtons === 0) {
			controlButtonsRow.style.gridTemplateColumns = "100%";
			controlButtonsRow.innerHTML = `<div class="maslow-grid-item" style="background-color: #f2f0e4;"></div>`;
		} else {
			const columnWidth = Math.floor(100 / numButtons);
			controlButtonsRow.style.gridTemplateColumns = buttons.map(() => `${columnWidth}%`).join(" ");
			
			// Create HTML for each button
			const buttonsHTML = buttons.map(btn => {
				if (btn.type === 'stop') {
					return `<div id="tablettab_calibration_stop" class="maslow-grid-item maslow-grid-item-btn"
						style="background-color: ${btn.color}; cursor: pointer;">${btn.label}</div>`;
				} else {
					// Transition button - need unique ID based on state
					const buttonId = `tablettab_transition_${btn.stateId}`;
					return `<div id="${buttonId}" class="maslow-grid-item maslow-grid-item-btn" data-target-state="${btn.stateId}"
						style="background-color: ${btn.color}; cursor: pointer;">${btn.label}</div>`;
				}
			}).join("");
			
			controlButtonsRow.innerHTML = buttonsHTML;
			
			// Attach click handlers to transition buttons
			buttons.forEach(btn => {
				if (btn.type === 'transition') {
					const buttonId = `tablettab_transition_${btn.stateId}`;
					const buttonElement = document.getElementById(buttonId);
					if (buttonElement) {
						buttonElement.addEventListener('click', () => {
							// Send the state transition command
							requestStateTransition(btn.stateId);
						});
					}
				} else if (btn.type === 'stop') {
					const stopBtn = document.getElementById('tablettab_calibration_stop');
					if (stopBtn) {
						stopBtn.addEventListener('click', () => {
							// Send stop command
							SendPrinterCommand("!");
						});
					}
				}
			});
		}
	}
}

/** Request a state transition */
const requestStateTransition = (targetState) => {
	// Send the state change command to firmware
	SendPrinterCommand(`$MaslowState=${targetState}`);
}



/** Perform maslow specific-ish info message handling */
const maslowInfoMsgHandling = (msg) => {
	// Check for STATEDEFS JSON response (comes through WebSocket, not HTTP callback)
	// This will be a JSON object starting with { and containing "states" field
	if (msg.trim().startsWith('{') && msg.includes('"states"')) {
		console.log("*** maslowInfoMsgHandling: intercepted STATEDEFS JSON response");
		if (pendingStateDataCallback) {
			console.log("*** maslowInfoMsgHandling: calling pendingStateDataCallback with JSON");
			// Call the callback with the JSON message
			pendingStateDataCallback(msg.trim());
			// Clear the callback
			pendingStateDataCallback = null;
		} else {
			console.log("*** maslowInfoMsgHandling: WARNING - received state data JSON but no callback registered");
		}
		return true; // Mark as handled
	}
	
	if (msg.startsWith('MINFO: ')) {
		try {
			const parsedStatus = JSON.parse(msg.substring(7));

			// Iterate through the keys of the parsed JSON. This is more reliable than assigning it directly which sometimes seems to produce garbage
			for (const key in parsedStatus) {
				if (parsedStatus.hasOwnProperty(key)) {
					// Check if the key exists in maslowStatus
					if (key in maslowStatus) {
						maslowStatus[key] = parsedStatus[key];
					}
				}
			}
		} catch (error) {
			console.error("Parsing the 'MINFO' message failed, the maslow status has not been changed. This is probably a programmer error.");
		}
		return true;
	}

	if (msg.startsWith('[MSG:INFO: Heartbeat')) {
		lastHeartBeatTime = new Date().getTime();
		return true;
	}

	//Parse state messages like [MSG:INFO: Current state: 0]
	if (msg.startsWith("[MSG:INFO: Current state:")) {
		const m = msg.match(/Current state:\s*(\d+)/);
		if (m) {
			const state = Number(m[1]);
			//If the state is in the range of 0-7, update the maslowStatus
			if (state < 0 || state > 9) {
				console.error("Invalid state received from machine: " + state);
				return false;
			}
			console.log("*** STATE CHANGE: Machine state changed from", maslowStatus.state, "to", state);
			maslowStatus.state = state;
			console.log("*** STATE CHANGE: calling updateDynamicButtons()...");
			updateDynamicButtons();
		}
		return true;
	}

	//Catch the calibration complete message and alert the user...this locks up the UI which is bad...should be handled better
	if (msg.startsWith("[MSG:INFO: Calibration complete")) {
		showCalibrationCompleteMessage();
		return true;
	}

	return false;
};


/// Show a modal message when calibration is complete
function showCalibrationCompleteMessage() {
  const message = "Calibration complete. You do not need to do calibration ever again unless your frame changes size. You might want to store a backup of your maslow.yaml file in case you need to restore it later.";
  // Create the modal dynamically
  const modal = document.createElement('div');
  modal.id = 'calibration-complete-modal';
  modal.style.cssText = `
    position: fixed;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    background-color: white;
    padding: 20px;
    border: 1px solid black;
    box-shadow: 0 4px 8px rgba(0,0,0,0.1);
    z-index: 1000;
  `;

  const messageElement = document.createElement('p');
  messageElement.textContent = message;

  const closeButton = document.createElement('button');
  closeButton.textContent = 'Close';
    closeButton.style.cssText = `
    margin-top: 10px;
    padding: 5px 10px;
    cursor: pointer;
  `;
  closeButton.onclick = function() {
    document.body.removeChild(modal);
  };

  modal.appendChild(messageElement);
  modal.appendChild(closeButton);
  document.body.appendChild(modal);
}

/**
 * Fetch state transitions from firmware
 * This function sends the STATETRANS command to get allowed state transitions
 */
/**
 * Fetch unified state data from firmware
 * This function sends the STATEDEFS command to get all state information including
 * names, button labels, colors, and allowed transitions
 */
function fetchStateData() {
	console.log("*** fetchStateData: sending $STATEDEFS command to firmware...");
	
	// Set up a callback to handle the response when it arrives via WebSocket
	// The response won't come through SendGetHttp's callback - it comes through the WebSocket
	pendingStateDataCallback = (jsonText) => {
		console.log("*** fetchStateData: received JSON response via WebSocket");
		console.log("*** fetchStateData: JSON text length:", jsonText.length);
		
		try {
			// Parse the JSON response
			// Expected format: {"states":[{"id":0,"name":"Unknown","buttonLabel":"","backgroundColor":"#f8d7da","allowedTransitions":[1]},...]}
			console.log("*** fetchStateData: attempting to parse JSON...");
			const data = JSON.parse(jsonText);
			console.log("*** fetchStateData: parsed JSON successfully");
			console.log("*** fetchStateData: data object:", data);
			
			if (data.states) {
				console.log("*** fetchStateData: Found", data.states.length, "state definitions");
				stateDefinitions = {};
				stateTransitionsMap = {};
				
				// Convert array to objects keyed by state id
				data.states.forEach(state => {
					console.log(`  Processing state ${state.id}: ${state.name}`);
					stateDefinitions[state.id] = state;
					// Build transitions map
					stateTransitionsMap[state.id] = state.allowedTransitions;
				});
				
				console.log("*** fetchStateData: State data loaded successfully:");
				console.log("  stateDefinitions:", stateDefinitions);
				console.log("  stateTransitionsMap:", stateTransitionsMap);
				
				// Update UI with complete state data
				console.log("*** fetchStateData: calling updateDynamicButtons()...");
				updateDynamicButtons();
			} else {
				console.error("*** fetchStateData: ERROR - no 'states' field in response data!");
				console.error("  Response data keys:", Object.keys(data));
			}
		} catch (error) {
			console.error("*** fetchStateData: ERROR - Failed to parse state data:", error);
			console.error("  Error message:", error.message);
			console.error("  Error stack:", error.stack);
		}
	};
	
	// Send command to get complete state data  
	// Use $STATEDEFS to call the Maslow custom command (not [ESP800] which is ESP3D system state)
	// Note: The response will come through the WebSocket (handled by pendingStateDataCallback above),
	// NOT through this HTTP callback which will receive an empty response
	const cmd = buildHttpCommandCmd(httpCmdType.plain, "$STATEDEFS");
	SendGetHttp(
		cmd,
		(responseText) => {
			// This callback receives an empty response because the actual JSON comes via WebSocket
			console.log("*** fetchStateData: HTTP callback called (response comes via WebSocket instead)");
			console.log("*** fetchStateData: HTTP responseText length:", responseText ? responseText.length : 0);
			// Don't process anything here - the real response is handled by pendingStateDataCallback
		},
		(error) => {
			console.error("*** fetchStateData: ERROR - HTTP request failed:", error);
			// Clear the pending callback if the command itself failed to send
			pendingStateDataCallback = null;
		}
	);
}


/**
 * Check if a state transition is allowed
 * @param {number} fromState - Current state
 * @param {number} toState - Target state
 * @returns {boolean} True if transition is allowed
 */
function isTransitionAllowed(fromState, toState) {
	if (!stateTransitionsMap) {
		// If we don't have the map yet, disable all transitions for safety
		// until the firmware provides the valid transition data
		return false;
	}
	
	const allowedStates = stateTransitionsMap[fromState.toString()];
	if (!allowedStates) {
		return false;
	}
	
	return allowedStates.includes(toState);
}

/** Perform maslow specific-ish error message handling */
const maslowErrorMsgHandling = (msg) => {
	if (!msg.startsWith("error:")) {
		// Nothing to see here - move along
		return "";
	}

	// And extra information for certain error codes
	const msgExtra = {
		"8": " - Command requires idle state. Unlock machine?",
		"152": " - Configuration is invalid. Maslow.yaml file may be corrupt. Turning off and back on again can often fix this issue.",
		"153": " - Configuration is invalid. ESP32 probably did a panic reset. Config changes cannot be saved. Try restarting",
	};

	return `${msg}${msgExtra[msg.split(":")[1]] || ""}`;
}

const cfgDef = {
	Retract_Current_Threshold: { name: "retractionForce", type: "A", cmd: "Maslow_Retract_Current_Threshold" },
	spoilboardThickness: { name: "spoilboardThickness", type: "A", cmd: "Maslow_spoilboardThickness" },
	workThickness: { name: "workThickness", type: "A", cmd: "Maslow_workThickness" },
	Acceptable_Calibration_Threshold: { name: "acceptableCalibrationThreshold", type: "A", cmd: "Maslow_Acceptable_Calibration_Threshold" },
	Extend_Dist: { name: "extendDist", type: "A", cmd: "Maslow_Extend_Dist" },
	Scale_X: { name: "scaleX", type: "A", cmd: "Maslow_Scale_X" },
	Scale_Y: { name: "scaleY", type: "A", cmd: "Maslow_Scale_Y" },
	beltEndExtension: { name: "beltEndExtension", type: "A", cmd: "kinematics/MaslowKinematics/beltEndExtension" },
	armLength: { name: "armLength", type: "A", cmd: "kinematics/MaslowKinematics/armLength" },
	trX: { name: "tr.x", type: "D", cmd: "kinematics/MaslowKinematics/trX", alsoSet: "machineWidth" },
	trY: { name: "tr.y", type: "D", cmd: "kinematics/MaslowKinematics/trY", alsoSet: "machineHeight" },
	trZ: { name: "tr.z", type: "D", cmd: "kinematics/MaslowKinematics/trZ" },
	tlX: { name: "tl.x", type: "D", cmd: "kinematics/MaslowKinematics/tlX" },
	tlY: { name: "tl.y", type: "D", cmd: "kinematics/MaslowKinematics/tlY" },
	tlZ: { name: "tl.z", type: "D", cmd: "kinematics/MaslowKinematics/tlZ" },
	brX: { name: "br.x", type: "D", cmd: "kinematics/MaslowKinematics/brX" },
	brY: { name: "br.y", type: "Null", cmd: "kinematics/MaslowKinematics/brY" },
	brZ: { name: "br.z", type: "D", cmd: "kinematics/MaslowKinematics/brZ" },
	blX: { name: "bl.x", type: "Null", cmd: "kinematics/MaslowKinematics/blX" },
	blY: { name: "bl.y", type: "Null", cmd: "kinematics/MaslowKinematics/blY" },
	blZ: { name: "bl.z", type: "D", cmd: "kinematics/MaslowKinematics/blZ" },
	Work_Area_X: { name: "workAreaX", type: "A", cmd: "Maslow_Work_Area_X" },
	Work_Area_Y: { name: "workAreaY", type: "A", cmd: "Maslow_Work_Area_Y" },
	Work_Area_Center_Offset_X: { name: "workAreaCenterOffsetX", type: "A", cmd: "Maslow_Work_Area_Center_Offset_X" },
	Work_Area_Center_Offset_Y: { name: "workAreaCenterOffsetY", type: "A", cmd: "Maslow_Work_Area_Center_Offset_Y" },
};

/** Handle Maslow specific configuration messages
 * These would have all started with `$/Maslow_` which is expected to have been stripped away before calling this function
 */
const maslowMsgHandling = (msg) => {
	const keyValue = msg.split("=");
	const errMsgSuffix = `${MaslowErrMsgKeyValueSuffix}${msg}`;
	if (keyValue.length !== 2) {
		return maslowErrorMsgHandling(`${MaslowErrMsgKeyValueCantUse} ${errMsgSuffix}`);
	}
	const key = keyValue[0] || "";
	const value = (keyValue[1] || "").trim();
	if (!key) {
		return maslowErrorMsgHandling(`${MaslowErrMsgNoKey} ${errMsgSuffix}`);
	}
	if (!value) {
		return maslowErrorMsgHandling(`${MaslowErrMsgNoValue} ${errMsgSuffix}`);
	}

	const stdAction = (id, value) => {
		const val = ("fnDisp" in cfgVal && typeof cfgVal.fnDisp === "function") ? cfgVal.fnDisp(value) : value;
		globalThis.setValue(id, val);
		// Handle loadedValues as an object for compatibility with tests
		if (!globalThis.loadedValues) {
			globalThis.loadedValues = {};
		}
		globalThis.loadedValues[id] = val; // Store the transformed value
	};

	const stdDimensionAction = (value) => Number.parseFloat(value);

	const cfgVal = cfgDef[key];
	if (typeof cfgVal !== "object") {
		return maslowErrorMsgHandling(`${MaslowErrMsgNoMatchingKey} ${errMsgSuffix}`);
	}
	switch (cfgVal.type) {
		case "A":
			// Check if this is a global variable assignment (like Acceptable_Calibration_Threshold)
			if (cfgVal.name === "acceptableCalibrationThreshold") {
				globalThis.acceptableCalibrationThreshold = stdDimensionAction(value);
			}
			stdAction(cfgVal.name, value);
			break;
		case "D": {
			let dimEnt = globalThis.initialGuess || {};
			if (!cfgVal.name) {
				// Well this is dangerous - so let's not do anything we'll regret very quickly
				return maslowErrorMsgHandling(`error: No 'name' value specified for '${key}' in the reference table. ${errMsgSuffix}`);
			}
			// Set the final value
			const parts = cfgVal.name.split(".");
			let target = globalThis.initialGuess || {};
			for (let i = 0; i < parts.length - 1; i++) {
				if (!target[parts[i]]) {
					target[parts[i]] = {};
				}
				target = target[parts[i]];
			}
			target[parts[parts.length - 1]] = stdDimensionAction(value);
			
			// If there's an alsoSet property, also store the value as a standard action
			if (cfgVal.alsoSet) {
				if (!globalThis.loadedValues) {
					globalThis.loadedValues = {};
				}
				globalThis.loadedValues[cfgVal.alsoSet] = value;
			}
		}
			break;
		default:
			// do nothing - a 'null' action
			break;
	}

	// Success - return an empty string
	return "";
}

// Helper functions for setValue and loadedValues
globalThis.setValue = globalThis.setValue || function(id, value) {
	const element = document.getElementById(id);
	if (element) {
		element.value = value;
	}
};

// Initialize loadedValues as an object (for compatibility with existing code/tests)
globalThis.loadedValues = globalThis.loadedValues || {};

const checkHomed = () => {
	if (maslowStatus.state != 7) { // If the state is not 'ready to cut'
		console.log("Maslow is not ready to move, current state: " + maslowStatus.state);
		const err_msg = `${M} is not ready to move.`;
		alert(err_msg);

		// Write to the console too, in case the system alerts are not visible
		const msgWindow = id('messages');
		if (msgWindow) {
			msgWindow.textContent = `${msgWindow.textContent}\n${err_msg}`;
			msgWindow.scrollTop = msgWindow.scrollHeight;
		}
	}

	return maslowStatus.state == 7; // Return true if the state is 'ready to cut'
}

/** Short hand convenience call to SendPrinterCommand with some preset values.
 * Uses the global function get_position, which is also a SendPrinterCommand with presets
 */
const sendCommand = (cmd) => {
	SendPrinterCommand(cmd, true, get_Position);
}

// The following functions are all defined as global functions, and are used by tablettab.html and other places
// They rely on the global function SendPrinterCommand defined in printercmd.js

/** Get all of the config (not corner) keys in the confiiguration definition */
const allConfigKeys = () => Object.keys(cfgDef).filter((key) => cfgDef[key].type === "A");

/** Used to populate the config popup when it loads */
const loadConfigValues = () => {
	// Load Maslow configuration values
	// biome-ignore lint/complexity/noForEach: <explanation>
	allConfigKeys().forEach((key) => {
		const cfgVal = cfgDef[key];
		const cmd = `$/${cfgVal.cmd || `${M}_${key}`}`;
		SendPrinterCommand(cmd);
	});

	// Load WiFi settings separately
	loadWiFiSettings();
};

/** Load all of the corner values */
const loadCornerValues = () => {
	// biome-ignore lint/complexity/noForEach: <explanation>
	Object.keys(cfgDef).filter((key) => cfgDef[key].type === "D").forEach((key) => {
		const cfgVal = cfgDef[key];
		const cmd = `$/${cfgVal.cmd || `${M}_${key}`}`;
		SendPrinterCommand(cmd);
	});
};

const saveConfigValues = () => {
	// Get all of the config data as entered, and as already loaded
	for (const key of allConfigKeys()) {
		const cfgVal = cfgDef[key];
		cfgVal.val = getValue(cfgVal.name);
		cfgVal.loadedVal = globalThis.loadedValues ? globalThis.loadedValues[cfgVal.name] : undefined;
	};

	// Save the individual values
	for (const key of allConfigKeys()) {
		const cfgVal = cfgDef[key];
		const value = typeof cfgVal.val === "undefined"
			? cfgVal.loadedVal
			: ("fnVal" in cfgVal && typeof cfgVal.fnVal === "function") ? cfgVal.fnVal(cfgVal.val) : cfgVal.val;
		if (value !== cfgVal.loadedVal) {
			const cmd = `$/${cfgVal.cmd || `${M}_${key}`}=${value}`;
			sendCommand(cmd);
		}
	};

	// Save WiFi settings separately
	saveWiFiSettings();

	refreshSettings(current_setting_filter);
	saveMaslowYaml();
	loadCornerValues();

	hideModal('configuration-popup');
}

/** Load WiFi settings from ESP settings system */
const loadWiFiSettings = () => {
	// Request all ESP settings to get WiFi SSID and Password
	// The response will be handled by the existing settings system
	const cmd = buildHttpCommandCmd(httpCmdType.plain, "[ESP400]");
	SendGetHttp(cmd, processWiFiSettingsResponse, (error, response) => {
		console.error("Failed to load WiFi settings:", error, response);
	});
};

/** Process ESP settings response to extract WiFi settings */
const processWiFiSettingsResponse = (response) => {
	try {
		const data = JSON.parse(response);
		if (data.EEPROM && Array.isArray(data.EEPROM)) {
			data.EEPROM.forEach(setting => {
				if (setting.H === "Sta/SSID") {
					const value = setting.V || "";
					setValue("wifiSSID", value);
					if (!globalThis.loadedValues) {
						globalThis.loadedValues = {};
					}
					globalThis.loadedValues["wifiSSID"] = value;
					globalThis.wifiSSIDPos = setting.P;
					globalThis.wifiSSIDType = setting.T;
				} else if (setting.H === "Sta/Password") {
					const value = setting.V || "";
					setValue("wifiPassword", value);
					if (!globalThis.loadedValues) {
						globalThis.loadedValues = {};
					}
					globalThis.loadedValues["wifiPassword"] = value;
					globalThis.wifiPasswordPos = setting.P;
					globalThis.wifiPasswordType = setting.T;
				}
			});
		}
	} catch (e) {
		console.error("Error parsing WiFi settings:", e);
	}
};

/** Save WiFi settings using ESP401 command */
const saveWiFiSettings = () => {
	const ssid = getValue("wifiSSID");
	const password = getValue("wifiPassword");
	const loadedSSID = globalThis.loadedValues ? globalThis.loadedValues["wifiSSID"] : undefined;
	const loadedPassword = globalThis.loadedValues ? globalThis.loadedValues["wifiPassword"] : undefined;

	// Only save if values have changed and we have the position/type info
	if (ssid !== loadedSSID && globalThis.wifiSSIDPos !== undefined) {
		const cmd = buildHttpCommandCmd(
			httpCmdType.plain,
			`[ESP401]P=${globalThis.wifiSSIDPos} T=${globalThis.wifiSSIDType} V=${encodeURIComponent(ssid)}`
		);
		SendGetHttp(cmd);
	}

	if (password !== loadedPassword && globalThis.wifiPasswordPos !== undefined) {
		const cmd = buildHttpCommandCmd(
			httpCmdType.plain,
			`[ESP401]P=${globalThis.wifiPasswordPos} T=${globalThis.wifiPasswordType} V=${encodeURIComponent(password)}`
		);
		SendGetHttp(cmd);
	}
};

