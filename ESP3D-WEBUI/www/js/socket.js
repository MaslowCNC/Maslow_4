// import - Monitor_output_Update, id, HTMLDecode, setHTML, on_autocheck_position, enable_ping, grblHandleMessage, reportNone, clear_cmd_list, translate_text_item, UIdisableddlg, closeModal

let convertDHT2Fahrenheit = false;
let event_source;

let wsmsg = "";
let ws_source;

const CancelCurrentUpload = () => {
	xmlhttpupload.abort();
	//http_communication_locked = false;
	console.log("Cancel Upload");
};

const check_ping = () => {
	if (Date.now() - last_ping > 20000) {
		Disable_interface(true);
		console.log("No heart beat for more than 20s");
	}
};

let interval_ping = -1;
let ping_state_before_upload = null;

/** Turn ping on or off based on its current value */
const handlePing = () => {
	if (enable_ping) {
		// First clear any existing interval
		if (interval_ping) {
			clearInterval(interval_ping);
		}
		last_ping = Date.now();
		interval_ping = setInterval(() => check_ping(), 10 * 1000);
		console.log("enable ping");
	} else {
		clearInterval(interval_ping);
		interval_ping = -1;
		console.log("disable ping");
	}
};

/** Temporarily disable ping monitoring during uploads */
const disablePingForUpload = () => {
	// Store the current ping state so we can restore it later
	ping_state_before_upload = enable_ping;
	if (enable_ping) {
		enable_ping = false;
		handlePing();
		console.log("Ping disabled for upload");
	}
};

/** Restore ping monitoring after upload completes */
const restorePingAfterUpload = () => {
	if (ping_state_before_upload !== null) {
		enable_ping = ping_state_before_upload;
		handlePing();
		ping_state_before_upload = null;
		console.log("Ping state restored after upload");
	}
};

let log_off = false;

// Reconnect the WebSocket without a full page reload.
// A page reload is blocked by the firmware's http_block_during_motion setting
// while GCode is running, which prevents the user from regaining control after
// a dropped connection.  Reconnecting only the WebSocket bypasses that block.
const resetConnectionState = () => {
	log_off = false;
	http_communication_locked = false;
	// Clear any stale ping interval and restart it
	if (interval_ping !== -1) {
		clearInterval(interval_ping);
		interval_ping = -1;
	}
	handlePing();
	// Close the "You are disconnected" dialog
	closeModal("Reconnecting");
	// Open a fresh WebSocket connection
	startSocket();
};

const Disable_interface = (lostconnection) => {
	let lostcon = false;
	if (typeof lostconnection !== "undefined") lostcon = lostconnection;
	//block all communication
	http_communication_locked = true;
	log_off = true;
	if (interval_ping !== -1) {
		clearInterval(interval_ping);
	}
	//clear all waiting commands
	clear_cmd_list();
	//no camera
	id("camera_frame").src = "";
	//No auto check
	on_autocheck_position(false);
	reportNone();
	if (async_webcommunication) {
		event_source.removeEventListener("ActiveID", ActiveID_events, false);
		event_source.removeEventListener("InitID", Init_events, false);
		event_source.removeEventListener("DHT", DHT_events, false);
	}
	ws_source.close();
	document.title += `('${HTMLDecode(translate_text_item("Disabled"))})`;
	UIdisableddlg(lostcon);
};

const EventListenerSetup = () => {
	if (!async_webcommunication) {
		return;
	}
	if (!!window.EventSource) {
		event_source = new EventSource("/events");
		event_source.addEventListener("InitID", Init_events, false);
		event_source.addEventListener("ActiveID", ActiveID_events, false);
		event_source.addEventListener("DHT", DHT_events, false);
	}
};

let page_id = "";
/** Get/Set the current page_id */
const pageID = (value) => {
	if (typeof value !== "undefined") {
		page_id = value;
	}
	return page_id;
}

/** Initialise the page_id from the event data */
const Init_events = (e) => console.log(`connection id = ${pageID(e.data)}`);

const ActiveID_events = (e) => {
	if (pageID() === e.data) {
		return;
	}

	Disable_interface();
	console.log("I am disabled");
	event_source.close();
};

const DHT_events = (e) => {
	Handle_DHT(e.data);
};

const Handle_DHT = (data) => {
	const tdata = data.split(" ");
	if (tdata.length !== 2) {
		console.log(`DHT data invalid: ${data}`);
		return;
	}

	const temp = convertDHT2Fahrenheit
		? Number.parseFloat(tdata[0]) * 1.8 + 32
		: Number.parseFloat(tdata[0]);
	setHTML("DHT_humidity", `${Number.parseFloat(tdata[1]).toFixed(2).toString()}%`);
	const temps = `${temp.toFixed(2).toString()}&deg;${convertDHT2Fahrenheit ? "F" : "C"}`;
	setHTML("DHT_temperature", temps);
};

const process_socket_response = (msg) => msg.split("\n").forEach(grblHandleMessage);

const startSocket = () => {
	try {
		if (async_webcommunication) {
			ws_source = new WebSocket(`ws://${document.location.host}/ws`, [
				"arduino",
			]);
		} else {
			console.log(`Socket is ${websocket_ip}:${websocket_port}`);
			ws_source = new WebSocket(`ws://${websocket_ip}:${websocket_port}`, [
				"arduino",
			]);
		}
	} catch (exception) {
		console.error(exception);
	}
	ws_source.binaryType = "arraybuffer";
	ws_source.onopen = (e) => {
		console.log("Connected");
		// Fire the open callback first so that critical commands (e.g. $STOP)
		// are sent before any auto-reports start filling the TX buffer.
		if (typeof onWSOpenCallback === 'function') {
			onWSOpenCallback();
		}
		// Restore GCode state if any exists from previous session
		// Use typeof check since this might be called before tablet.js is fully loaded
		if (typeof restoreGCodeState === 'function') {
			restoreGCodeState();
		}
	};
	ws_source.onclose = (e) => {
		console.log("Disconnected");
		//seems sometimes it disconnect so wait 3s and reconnect
		//if it is not a log off
		if (!log_off) setTimeout(startSocket, 3000);
	};
	ws_source.onerror = (e) => {
		//Monitor_output_Update("[#]Error "+ e.code +" " + e.reason + "\n");
		console.log("ws error", e);
	};
	ws_source.onmessage = (e) => {
		let msg = "";
		//bin
		if (e.data instanceof ArrayBuffer) {
			const bytes = new Uint8Array(e.data);
			for (let i = 0; i < bytes.length; i++) {
				msg += String.fromCharCode(bytes[i]);
				if (bytes[i] === 10) {
					wsmsg += msg.replace("\r\n", "\n");
					const thismsg = wsmsg.trim();
					wsmsg = "";
					msg = "";
					Monitor_output_Update(thismsg);
					process_socket_response(thismsg);
					const noNeedToShowMsg = ["<", "ok T:", "X:", "FR:", "echo:E0 Flow"].some((msgStart) => thismsg.startsWith(msgStart));
					if (!noNeedToShowMsg && thismsg !== "ok") {
						console.log(thismsg);
					}
					
					// Close stuck connection dialog if any message received from machine
					// This indicates the machine is connected and communicating
					const connectModal = id("connectdlg.html");
					if (connectModal && connectModal.style.display !== "none") {
						console.log("SOCKET FIX: Machine message received - closing stuck connection dialog");
						closeModal("Machine connected");
					}
				}
			}
			wsmsg += msg;
		} else {
			msg += e.data;
			const tval = msg.split(":");
			if (tval.length >= 2) {
				if (tval[0] === "CURRENT_ID") {
					pageID(tval[1]);
					console.log(`connection id = ${pageID()}`);
				}
				if (enable_ping) {
					if (tval[0] === "PING") {
						pageID(tval[1]);
						// console.log("ping from id = " + pageID());
						last_ping = Date.now();
						if (interval_ping === -1)
							interval_ping = setInterval(() => {
								check_ping();
							}, 10 * 1000);
					}
				}
				if (tval[0] === "ACTIVE_ID") {
					if (pageID() !== tval[1]) {
						Disable_interface();
					}
				}
				if (tval[0] === "DHT") {
					Handle_DHT(tval[1]);
				}
				if (tval[0] === "ERROR") {
					esp_error_message = tval[2];
					esp_error_code = tval[1];
					console.error(`ERROR: ${tval[2]} code:${tval[1]}`);
					CancelCurrentUpload();
				}
				if (tval[0] === "MSG") {
					console.info(`MSG: ${tval[2]} code:${tval[1]}`);
				}
			}
			
			// Close stuck connection dialog if any message received from machine
			// This indicates the machine is connected and communicating  
			const connectModal = id("connectdlg.html");
			if (connectModal && connectModal.style.display !== "none") {
				console.log("SOCKET FIX: Machine message received - closing stuck connection dialog");
				closeModal("Machine connected");
			}
		}
		//console.log(msg);
	};
};
