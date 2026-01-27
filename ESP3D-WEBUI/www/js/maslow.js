// When we can change to proper ESM - uncomment this
// import M from "constants";

/** Maslow Status */
let maslowStatus = { homed: false, extended: false, state: 0 };

/** State Transitions Map - fetched from firmware at startup */
let stateTransitionsMap = null;

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
const updateDynamicButtons = () => {

	const stateLabel = document.getElementById("state-label");
	const mainStateLabel = document.getElementById("main-state-label");
	const mainStateLabelContainer = document.getElementById("main-state-label-container");

	const retractButton = document.getElementById("tablettab_cal_retract");
	const extendButton = document.getElementById("tablettab_cal_extend");
	const tenseButton = document.getElementById("tablettab_cal_tense");
	const relaxButton = document.getElementById("tablettab_cal_relax");
	const calibrateButton = document.getElementById("tablettab_cal_calibrate");

	const greenBackground = "#4aa85c"
	const greyBackground = "#a0a0a0"
	
	// State label background colors
	const redBackground = "#f8d7da"
	const blueBackground = "#cfe2ff"
	const greenStateBackground = "#d1e7dd"
	const yellowBackground = "#fff3cd"

	// State constants (matching firmware)
	const UNKNOWN = 0;
	const RETRACTING = 1;
	const RETRACTED = 2;
	const EXTENDING = 3;
	const EXTENDEDOUT = 4;
	const TAKING_SLACK = 5;
	const CALIBRATION_IN_PROGRESS = 6;
	const READY_TO_CUT = 7;
	const RELEASE_TENSION = 8;
	const CALIBRATION_COMPUTING = 9;

	// State names for display
	const stateNames = {
		[UNKNOWN]: "Unknown",
		[RETRACTING]: "Retracting",
		[RETRACTED]: "Retracted",
		[EXTENDING]: "Extending",
		[EXTENDEDOUT]: "Extended",
		[TAKING_SLACK]: "Taking Slack",
		[CALIBRATION_IN_PROGRESS]: "Calibrating",
		[READY_TO_CUT]: "Ready to Cut",
		[RELEASE_TENSION]: "Releasing Tension",
		[CALIBRATION_COMPUTING]: "Calibration Computing"
	};

	// State background colors
	const stateBackgrounds = {
		[UNKNOWN]: redBackground,
		[RETRACTING]: blueBackground,
		[RETRACTED]: greenStateBackground,
		[EXTENDING]: blueBackground,
		[EXTENDEDOUT]: yellowBackground,
		[TAKING_SLACK]: blueBackground,
		[CALIBRATION_IN_PROGRESS]: blueBackground,
		[READY_TO_CUT]: greenStateBackground,
		[RELEASE_TENSION]: blueBackground,
		[CALIBRATION_COMPUTING]: blueBackground
	};

	// Update state label
	const stateName = stateNames[maslowStatus.state] || "Unknown";
	stateLabel.innerHTML = "State: " + stateName;
	if (mainStateLabel) {
		mainStateLabel.innerHTML = "State: " + stateName;
		if (mainStateLabelContainer) {
			mainStateLabelContainer.style.backgroundColor = stateBackgrounds[maslowStatus.state] || redBackground;
		}
	}

	// Update button states based on allowed transitions
	// Relaxation button is always available (special case)
	relaxButton.style.backgroundColor = greenBackground;

	// Set button colors based on whether transitions are allowed
	const currentState = maslowStatus.state;
	
	retractButton.style.backgroundColor = isTransitionAllowed(currentState, RETRACTING) ? greenBackground : greyBackground;
	extendButton.style.backgroundColor = isTransitionAllowed(currentState, EXTENDING) ? greenBackground : greyBackground;
	tenseButton.style.backgroundColor = isTransitionAllowed(currentState, TAKING_SLACK) ? greenBackground : greyBackground;
	calibrateButton.style.backgroundColor = isTransitionAllowed(currentState, CALIBRATION_IN_PROGRESS) ? greenBackground : greyBackground;
	// Note: RELEASE_TENSION is not directly triggered by a button in most states
}




/** Perform maslow specific-ish info message handling */
const maslowInfoMsgHandling = (msg) => {
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
			maslowStatus.state = state;
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
function fetchStateTransitions() {
	// Send command to get state transitions
	SendGetHttp(
		"STATETRANS",
		(responseText) => {
			try {
				// Parse the JSON response
				// Expected format: {"stateTransitions":{"0":[1],"1":[2],...}}
				const data = JSON.parse(responseText);
				if (data.stateTransitions) {
					stateTransitionsMap = data.stateTransitions;
					console.log("State transitions loaded:", stateTransitionsMap);
					// Update buttons now that we have the transition map
					updateDynamicButtons();
				}
			} catch (error) {
				console.error("Failed to parse state transitions:", error);
			}
		},
		(error) => {
			console.error("Failed to fetch state transitions:", error);
			// If we can't fetch transitions, fall back to the UI working without dynamic button control
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

