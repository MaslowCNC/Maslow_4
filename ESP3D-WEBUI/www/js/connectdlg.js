// import conErr, displayBlock, displayInline, displayNone, id, closeModal, setactiveModal, showModal, SendGetHttp, logindlg, EventListenerSetup, startSocket,

// Connection state to prevent multiple concurrent connection attempts
let connectionInProgress = false;
const BLUETOOTH_CONNECTION_DEFAULTS = {
	fwVersion: "Bluetooth LE",
	targetFirmware: "grbl-embedded",
	primarySd: "/sd/",
	secondarySd: "/sd/",
};

const resetConnectMessages = () => {
	if (id("connecting_msg_text")) {
		id("connecting_msg_text").textContent = "Please wait...";
	}
	if (id("failed_connect_msg_text")) {
		id("failed_connect_msg_text").textContent = "Connection failed! is your FW correct?";
	}
};

const updateBluetoothConnectVisibility = () => {
	const btBtn = id("connectbtbtn");
	const btNote = id("connectbt_note");
	if (!btBtn || !btNote) {
		return;
	}
	const supported = typeof webBluetoothSupported === "function" && webBluetoothSupported();
	btBtn.disabled = !supported;
	btNote.textContent = supported
		? "Bluetooth requires a Chromium-based browser running on HTTPS or localhost."
		: "Bluetooth is unavailable here. Open the UI from HTTPS or localhost in Chrome or Edge.";
};

/** Connect Dialog */
const connectdlg = (getFw = false) => {
	// Prevent multiple concurrent connection attempts
	if (connectionInProgress && getFw) {
		console.log("Connection already in progress, skipping duplicate attempt");
		return;
	}

	const modal = setactiveModal("connectdlg.html");
	if (modal == null) {
		return;
	}

	showModal();
	resetConnectMessages();
	updateBluetoothConnectVisibility();
	if (id("connectbtbtn") && !id("connectbtbtn").dataset.bound) {
		id("connectbtbtn").addEventListener("click", retryBluetoothConnect);
		id("connectbtbtn").dataset.bound = "true";
	}

	if (getFw) {
		connectionInProgress = true;
		retryconnect();
	}
};

const getFWdata = (response) => {
	const tlist = response.split("#");
	//FW version:0.9.200 # FW target:smoothieware # FW HW:Direct SD # primary sd:/ext/ # secondary sd:/sd/ # authentication: yes
	if (tlist.length < 3) {
		return false;
	}
	//FW version
	let sublist = tlist[0].split(":");
	if (sublist.length !== 2) {
		return false;
	}
	fw_version = sublist[1].toLowerCase().trim();
	//FW target
	sublist = tlist[1].split(":");
	if (sublist.length !== 2) {
		return false;
	}
	target_firmware = sublist[1].toLowerCase().trim();
	//FW HW
	sublist = tlist[2].split(":");
	if (sublist.length !== 2) {
		return false;
	}
	const sddirect = sublist[1].toLowerCase().trim();
	direct_sd = sddirect === "direct sd";
	//primary sd
	sublist = tlist[3].split(":");
	if (sublist.length !== 2) {
		return false;
	}
	primary_sd = sublist[1].toLowerCase().trim();

	//secondary sd
	sublist = tlist[4].split(":");
	if (sublist.length !== 2) {
		return false;
	}
	secondary_sd = sublist[1].toLowerCase().trim();

	//authentication
	sublist = tlist[5].split(":");
	if (sublist.length !== 2) {
		return false;
	}
	ESP3D_authentication = sublist[0].trim() === "authentication" && sublist[1].trim() === "yes";
	//async communications
	if (tlist.length > 6) {
		sublist = tlist[6].split(":");
		if (
			sublist[0].trim() === "webcommunication" &&
			sublist[1].trim() === "Async"
		) {
			async_webcommunication = true;
		} else {
			async_webcommunication = false;
			websocket_port = sublist[2].trim();
			if (sublist.length > 3) {
				websocket_ip = sublist[3].trim();
			} else {
				console.log("No IP for websocket, use default");
				websocket_ip = document.location.hostname;
			}
		}
	}
	if (tlist.length > 7) {
		sublist = tlist[7].split(":");
		if (sublist[0].trim() === "hostname") esp_hostname = sublist[1].trim();
	}

	if (tlist.length > 8) {
		sublist = tlist[8].split(":");
		if (sublist[0].trim() === "axis") {
			grblaxis = Number.parseInt(sublist[1].trim());
		}
	}

	EventListenerSetup();
	startSocket();

	return true;
};

const connectfailed = (error_code, response) => {
	connectionInProgress = false; // Clear connection state on failure
	displayBlock("connectbtn");
	displayBlock("connectbtbtn");
	displayBlock("failed_connect_msg");
	displayNone("connecting_msg");

	id("connectbtn").addEventListener("click", retryconnect);

	conErr(error_code, response, "FW identification error");
};

const connectsuccess = (response) => {
	connectionInProgress = false; // Clear connection state on success
	if (getFWdata(response)) {
		console.log(`FW identification:${response}`);
		if (ESP3D_authentication) {
			closeModal("Connection successful");
			displayInline("menu_authentication");
			logindlg(initUI, true);
		} else {
			displayNone("menu_authentication");
			initUI();
		}
	} else {
		console.log(response);
		connectfailed(406, "Wrong data");
	}
};

const retryconnect = () => {
	connectionInProgress = true; // Set connection state when retrying
	displayNone("connectbtn");
	displayNone("connectbtbtn");
	displayNone("failed_connect_msg");
	displayBlock("connecting_msg");
	resetConnectMessages();

	id("connectbtn").removeEventListener("click", retryconnect);

	const cmd = buildHttpCommandCmd(httpCmdType.plain, "[ESP800]");
	SendGetHttp(cmd, connectsuccess, connectfailed);
};

const bluetoothConnectFailed = (error) => {
	connectionInProgress = false;
	displayBlock("connectbtn");
	displayBlock("connectbtbtn");
	displayBlock("failed_connect_msg");
	displayNone("connecting_msg");
	if (id("failed_connect_msg_text")) {
		id("failed_connect_msg_text").textContent = error?.message || "Bluetooth connection failed.";
	}
	console.error("Bluetooth connection failed:", error);
};

const bluetoothConnectSuccess = () => {
	connectionInProgress = false;
	displayNone("menu_authentication");
	fw_version = BLUETOOTH_CONNECTION_DEFAULTS.fwVersion;
	target_firmware = BLUETOOTH_CONNECTION_DEFAULTS.targetFirmware;
	direct_sd = false;
	primary_sd = BLUETOOTH_CONNECTION_DEFAULTS.primarySd;
	secondary_sd = BLUETOOTH_CONNECTION_DEFAULTS.secondarySd;
	ESP3D_authentication = false;
	async_webcommunication = false;
	esp_hostname = bt_device?.name || "Maslow Bluetooth";
	initUI();
};

const retryBluetoothConnect = async () => {
	connectionInProgress = true;
	displayNone("connectbtn");
	displayNone("connectbtbtn");
	displayNone("failed_connect_msg");
	displayBlock("connecting_msg");
	if (id("connecting_msg_text")) {
		id("connecting_msg_text").textContent = "Select your Maslow Bluetooth device...";
	}

	try {
		await connectBluetoothTransport();
		bluetoothConnectSuccess();
	} catch (error) {
		bluetoothConnectFailed(error);
	}
};

// Helper function to force close connection dialog if it's stuck
const forceCloseConnectionDialog = () => {
	const connectModal = id("connectdlg.html");
	if (connectModal && connectModal.style.display !== "none") {
		const activeModal = getactiveModal();
		if (activeModal && activeModal.name === "connectdlg.html") {
			// Connection dialog is the topmost modal – close it normally.
			console.log("Force closing stuck connection dialog");
			closeModal("Force closed");
		} else {
			// Another modal (e.g. a safety warning) is on top of the stack.
			// Hide the connection dialog element directly and remove any of its
			// entries from the modal stack so the active modal is not disturbed.
			connectModal.style.display = "none";
			for (let i = listmodal.length - 1; i >= 0; i--) {
				if (listmodal[i].name === "connectdlg.html") {
					listmodal.splice(i, 1);
				}
			}
		}
	}
	connectionInProgress = false;
};

// Handle visibility change to fix stuck connection dialog when tab doesn't have focus
const handleVisibilityChange = () => {
	// Only act when tab becomes visible again
	if (!document.hidden) {
		// Small delay to allow any pending operations to complete
		setTimeout(() => {
			const connectModal = id("connectdlg.html");
			// Check if connection dialog is still showing but connection is no longer in progress
			// and main UI is already loaded (indicating successful connection)
			if (connectModal && 
				connectModal.style.display !== "none" && 
				!connectionInProgress && 
				id("main_ui") && 
				!id("main_ui").classList.contains("hide_it")) {
				console.log("Tab regained focus - force closing stuck connection dialog");
				forceCloseConnectionDialog();
			}
		}, 100);
	}
};

// Set up the visibility change listener when the page loads
if (typeof document !== "undefined") {
	document.addEventListener("visibilitychange", handleVisibilityChange);
}
