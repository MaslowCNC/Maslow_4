// When we can change to proper ESM - uncomment this
// import M from "constants";

/** Maslow Status */
let maslowStatus = { homed: false, extended: false, state: 0, zTrusted: false };

/** Maslow state constants (mirror firmware Maslow.h defines) */
const MASLOW_STATE_READY_TO_CUT = 7;

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
    -Park (State-Dependent Button: moves to machine 0,0)
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

	// #define UNKNOWN 0
	// #define RETRACTING 1
	// #define RETRACTED 2
	// #define EXTENDING 3
	// #define EXTENDEDOUT 4 //Extended is a reserved word
	// #define TAKING_SLACK 5
	// #define CALIBRATION_IN_PROGRESS 6
	// #define READY_TO_CUT 7
	// #define RELEASE_TENSION 8
	// #define CALIBRATION_COMPUTING 9

	switch (maslowStatus.state) {
		case 0: 
			stateLabel.innerHTML = "State: Unknown";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Unknown";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = redBackground;
			}

			//Set the retract and extend buttons to have a green background 
			retractButton.style.backgroundColor = greenBackground;
			extendButton.style.backgroundColor = greenBackground;

			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;
			break;
		case 1:
			stateLabel.innerHTML = "State: Retracting";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Retracting";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = blueBackground;
			}

			retractButton.style.backgroundColor = greyBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;

			break;
		case 2:
			stateLabel.innerHTML = "State: Retracted";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Retracted";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = greenStateBackground;
			}

			retractButton.style.backgroundColor = greenBackground;
			extendButton.style.backgroundColor = greenBackground;

			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;

			break;
		case 3:
			stateLabel.innerHTML = "State: Extending";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Extending";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = blueBackground;
			}
			
			retractButton.style.backgroundColor = greyBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;
			break;
		case 4:
			stateLabel.innerHTML = "State: Extended";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Extended";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = yellowBackground;
			}

			retractButton.style.backgroundColor = greenBackground;
			tenseButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greenBackground;
			extendButton.style.backgroundColor = greenBackground;
			
			relaxButton.style.backgroundColor = greenBackground;

			break;
		case 5:
			stateLabel.innerHTML = "State: Taking Slack";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Taking Slack";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = blueBackground;
			}

			retractButton.style.backgroundColor = greyBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;
			break;
		case 6:
			stateLabel.innerHTML = "State: Finding Anchors";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Finding Anchors";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = blueBackground;
			}

			retractButton.style.backgroundColor = greyBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;
			break;
		case 7:
			stateLabel.innerHTML = "State: Ready to Cut";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Ready to Cut";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = greenStateBackground;
			}

			retractButton.style.backgroundColor = greenBackground;
			relaxButton.style.backgroundColor = greenBackground;

			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			calibrateButton.style.backgroundColor = greyBackground;

			// Load park settings so the park button uses configured values
			if (typeof loadParkSettings === 'function') {
				loadParkSettings();
			}

			break;
		case 8:
			stateLabel.innerHTML = "State: Releasing Tension";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Releasing Tension";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = blueBackground;
			}

			retractButton.style.backgroundColor = greyBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;

			// No buttons are active in this state
			break;
		case 9:
			stateLabel.innerHTML = "State: Find Anchors Computing";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Find Anchors Computing";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = blueBackground;
			}
			// No buttons are active in this state
			retractButton.style.backgroundColor = greyBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;
			break;
		default:
			stateLabel.innerHTML = "State: Unknown";
			if (mainStateLabel) {
				mainStateLabel.innerHTML = "State: Unknown";
				if (mainStateLabelContainer) mainStateLabelContainer.style.backgroundColor = redBackground;
			}

			retractButton.style.backgroundColor = greenBackground;
			extendButton.style.backgroundColor = greyBackground;
			tenseButton.style.backgroundColor = greyBackground;
			relaxButton.style.backgroundColor = greenBackground;
			calibrateButton.style.backgroundColor = greyBackground;
			break;
	}
	
	// Update the new Maslow action button when state changes
	if (typeof updateMaslowActionButton === 'function') {
		updateMaslowActionButton();
	}

	// Update run controls so the Start button reflects whether Maslow is ready to cut
	if (typeof setRunControls === 'function') {
		setRunControls();
	}

	// Reflect Z-trust status on Z-related buttons
	updateZButtonColors();

	// Reset stop button colors when action completes (state update received)
	if (typeof resetStopButtonColors === 'function') {
		resetStopButtonColors();
	}
}

/**
 * Updates the color of Z-axis buttons to indicate whether the Z-axis zero
 * position is trusted.  When the machine has been in an UNKNOWN state and the
 * Z-zero has not been re-confirmed, the buttons are highlighted in red to
 * prompt the operator to run "Set Z Stop" before starting a cut.
 */
const updateZButtonColors = () => {
	const zTrusted = maslowStatus.zTrusted;
	const untrustedColor = "#f64646";  // red – Z position not known
	const defaultColor   = "#f2f0e4"; // original button background color

	const zUpBtn    = document.getElementById("tablettab_zUp");
	const zDownBtn  = document.getElementById("tablettab_zDown");
	const zHomeBtn  = document.getElementById("tablettab_set_z_home");

	[zUpBtn, zDownBtn, zHomeBtn].forEach((btn) => {
		if (!btn) return;
		if (zTrusted) {
			btn.style.backgroundColor = defaultColor;
			btn.title = "";
		} else {
			btn.style.backgroundColor = untrustedColor;
			btn.title = "Z-axis zero has not been set since the machine was in an unknown state. Use Setup → Set Z Stop to establish the Z home position before cutting.";
		}
	});
};




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

		// Reset stop button colors when firmware responds to our command
		if (typeof resetStopButtonColors === 'function') {
			resetStopButtonColors();
		}
		// Reflect any change to zTrusted immediately
		updateZButtonColors();
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
	Park_Z: { name: "parkZ", type: "A", cmd: "Maslow_Park_Z" },
	Park_X: { name: "parkX", type: "A", cmd: "Maslow_Park_X" },
	Park_Y: { name: "parkY", type: "A", cmd: "Maslow_Park_Y" },
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
	if (maslowStatus.state != MASLOW_STATE_READY_TO_CUT) { // If the state is not 'ready to cut'
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

	return maslowStatus.state == MASLOW_STATE_READY_TO_CUT; // Return true if the state is 'ready to cut'
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

/** Load park position settings from firmware (queried on demand) */
const loadParkSettings = () => {
	['Park_Z', 'Park_X', 'Park_Y'].forEach((key) => {
		const cfgVal = cfgDef[key];
		if (cfgVal) {
			SendPrinterCommand(`$/${cfgVal.cmd}`);
		}
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
			// Immediately update loadedValues so the new value is available without
			// waiting for the async WebSocket round-trip from loadParkSettings()
			if (!globalThis.loadedValues) globalThis.loadedValues = {};
			globalThis.loadedValues[cfgVal.name] = value;
		}
	};

	// Save WiFi settings separately
	saveWiFiSettings();

	refreshSettings(current_setting_filter);
	saveMaslowYaml();
	loadCornerValues();
	loadParkSettings();

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

