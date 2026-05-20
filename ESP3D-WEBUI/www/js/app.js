var ESP3D_authentication = false;
var async_webcommunication = false;
var websocket_port = 0;
var websocket_ip = '';
var use_serial_transport = false;
var esp_hostname = 'ESP3D WebUI';
var EP_HOSTNAME;
var EP_STA_SSID;
var EP_STA_PASSWORD;
var EP_STA_IP_MODE;
var EP_STA_IP_VALUE;
var EP_STA_GW_VALUE;
var EP_STA_MK_VALUE;
var EP_WIFI_MODE;
var EP_AP_SSID;
var EP_AP_PASSWORD;
var EP_AP_IP_VALUE;
var EP_BAUD_RATE = 112;
var EP_AUTH_TYPE = 119;
var EP_TARGET_FW = 461;
var EP_IS_DIRECT_SD = 850;
var EP_PRIMARY_SD = 851;
var EP_SECONDARY_SD = 852;
var EP_DIRECT_SD_CHECK = 853;
var SETTINGS_AP_MODE = 1;
var SETTINGS_STA_MODE = 2;
var SETTINGS_FALLBACK_MODE = 3;
var last_ping = 0;
var enable_ping = true;
var esp_error_message = '';
var esp_error_code = 0;
// WiFi SSID validation pattern - allows A-Z, a-z, 0-9, and special chars: . - _ # $ % & [ ] + , space
var SSID_PATTERN = '[A-Za-z0-9._#$%&\\[\\]+\\-, ]*';
var SSID_PATTERN_TITLE = 'Allowed characters: A-Z, a-z, 0-9, . - _ # $ % & [ ] + , space';
// Pre-compiled regex patterns for SSID validation (avoid recreating on each input event)
var SSID_FULL_PATTERN_REGEX = null;
var SSID_CHAR_PATTERN_REGEX = null;

// Function to get or create the full SSID pattern regex
var getSSIDFullPatternRegex = function() {
  if (!SSID_FULL_PATTERN_REGEX) {
    SSID_FULL_PATTERN_REGEX = new RegExp("^" + SSID_PATTERN + "$");
  }
  return SSID_FULL_PATTERN_REGEX;
};

// Function to get or create the single character pattern regex
var getSSIDCharPatternRegex = function() {
  if (!SSID_CHAR_PATTERN_REGEX) {
    SSID_CHAR_PATTERN_REGEX = /^[A-Za-z0-9._#$%&\[\]+\-, ]$/;
  }
  return SSID_CHAR_PATTERN_REGEX;
};

//Check for IE
//Edge
//Chrome
function browser_is(bname) {
	const ua = navigator.userAgent;
	switch (bname) {
		case 'IE':
			if (ua.indexOf('Trident/') !== -1) return true;
			break;
		case 'Edge':
			if (ua.indexOf('Edge') !== -1) return true;
			break;
		case 'Chrome':
			if (ua.indexOf('Chrome') !== -1) return true;
			break;
		case 'Firefox':
			if (ua.indexOf('Firefox') !== -1) return true;
			break;
		case 'MacOSX':
			if (ua.indexOf('Mac OS X') !== -1) return true;
			break;
		default:
			return false;
	}
	return false;
}

let failSafe = 10;

function loadApp() {
	console.log("Connect to board");

	const startUp = {
		connect: { msg: "", fldId: "connectdlg.html", errMsg: "Error loading connect dialog:" },
		controls: { msg: "", fldId: "controlPanel", errMsg: "Error loading controls panel:" },
		navBar: { msg: "", fldId: "navbar", errMsg: "Error loading navigation bar:" },
		tabletTab: { msg: "", fldId: "tablettab", errMsg: "Error loading tablet tab:" },
		debuggingTab: { msg: "", fldId: "debuggingtab", errMsg: "Error loading debugging tab:" }
	};

	const doPanelStartUp = (panel) => {
		if (panel.msg || !id(panel.fldId) || typeof panel.fn !== "function") {
			return;
		}

		//to check if javascript is disabled like in android preview
		displayNone("loadingmsg");

		panel.msg = "loading";
		try {
			panel.fn(true);
			panel.msg = "loaded";
		} catch (err) {
			console.error(panel.errMsg, err);
			panel.msg = "failed";
		}
	};

	let startUpInt = setInterval(() => {
		// Because things might not be fully resolved yet, we define the function references in here
		try {
			startUp.connect.fn = connectdlg;
			startUp.controls.fn = ControlsPanel;
			startUp.navBar.fn = navbar;
			startUp.tabletTab.fn = tabletInit;
			startUp.debuggingTab.fn = initDebuggingTab;
		} catch (error) {
			console.warn("Error setting up function references:", error);
			// Ensure that we always break out of this
			failSafe--;
			return;
		}


		// Check for various key HTML panels and load them up
		doPanelStartUp(startUp.connect);
		doPanelStartUp(startUp.controls);
		doPanelStartUp(startUp.navBar);
		doPanelStartUp(startUp.tabletTab);
		doPanelStartUp(startUp.debuggingTab);

		if ((startUp.connect.msg && startUp.controls.msg && startUp.navBar.msg && startUp.tabletTab.msg && startUp.debuggingTab.msg) || failSafe <= 0) {
			clearInterval(startUpInt);
			startUpInt = null;
		}

		// Ensure that we always break out of this
		failSafe--;
	}, 500);
}

window.addEventListener("load", (event) => {
	// Wait half a second before firing up
	setTimeout(() => { loadApp(); }, 1000);
});
