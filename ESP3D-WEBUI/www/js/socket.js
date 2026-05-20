// import - Monitor_output_Update, id, HTMLDecode, setHTML, on_autocheck_position, enable_ping, grblHandleMessage, reportNone, clear_cmd_list, translate_text_item, UIdisableddlg, closeModal

let convertDHT2Fahrenheit = false;
let event_source;

let wsmsg = "";
let ws_source;
let serialPort = null;
let serialReader = null;
let serialWriter = null;
let serialReaderRunning = false;
let serialReadBuffer = "";
let serialPendingCommand = null;

const serialTextEncoder = new TextEncoder();
const serialTextDecoder = new TextDecoder();

const CancelCurrentUpload = () => {
	xmlhttpupload.abort();
	//http_communication_locked = false;
	console.log("Cancel Upload");
};

const check_ping = () => {
	if (use_serial_transport) {
		return;
	}
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

let reconnect_timer = null;

const serialConnectionProxy = {
	readyState: WebSocket.OPEN,
	send: (message) => writeToSerial(message),
	close: () => closeSerialTransport()
};

/** Interval (ms) between auto-reconnect attempts while the disconnect dialog is visible */
const RECONNECT_INTERVAL_MS = 3000;

const cancelReconnectTimer = () => {
	if (reconnect_timer !== null) {
		clearInterval(reconnect_timer);
		reconnect_timer = null;
	}
};

/** Restore state and reconnect after a disconnection without a full page reload */
const resetConnectionState = () => {
	cancelReconnectTimer();
	log_off = false;
	http_communication_locked = false;
	clear_cmd_list();
	on_autocheck_position(false);
	handlePing();
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
	// Only show the dialog if it is not already visible (avoids duplicate listmodal entries)
	const disabledModal = id("UIdisableddlg.html");
	if (!disabledModal || disabledModal.style.display === "none") {
		UIdisableddlg(lostcon);
	}
	// Auto-reconnect: try once every 3 seconds (well under the 1/second limit).
	// resetConnectionState() or a successful onopen will cancel this timer.
	cancelReconnectTimer();
	reconnect_timer = setInterval(startSocket, RECONNECT_INTERVAL_MS);
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

const serialStatusLinePrefixes = [
	"<",
	"[MSG:INFO: Heartbeat]",
	"X:",
	"FR:",
];

const isSerialStatusLine = (line) => serialStatusLinePrefixes.some((prefix) => line.startsWith(prefix));

const completeSerialPendingCommand = (isError = false, line = "") => {
	if (!serialPendingCommand) {
		return;
	}
	const pending = serialPendingCommand;
	serialPendingCommand = null;
	clearTimeout(pending.timeout);

	if (line && !isSerialStatusLine(line)) {
		pending.lines.push(line);
	}
	const response = pending.lines.join("\n").trim() || "ok";
	if (isError) {
		http_errorfn(pending.cmd, 400, response);
	} else {
		http_resultfn(pending.cmd, response === "ok" ? "" : response);
	}
};

const resetSerialPendingTimeout = () => {
	if (!serialPendingCommand) {
		return;
	}
	clearTimeout(serialPendingCommand.timeout);
	serialPendingCommand.timeout = setTimeout(() => completeSerialPendingCommand(false), 250);
};

const processSerialPendingCommandLine = (line) => {
	if (!serialPendingCommand) {
		return;
	}

	const trimmed = (line || "").trim();
	if (!trimmed || isSerialStatusLine(trimmed)) {
		return;
	}

	if (trimmed === "ok") {
		completeSerialPendingCommand(false);
		return;
	}

	if (trimmed.startsWith("error:") || trimmed.startsWith("ALARM:")) {
		completeSerialPendingCommand(true, trimmed);
		return;
	}

	serialPendingCommand.lines.push(trimmed);
	resetSerialPendingTimeout();
};

const processIncomingLine = (thismsg) => {
	last_ping = Date.now();
	Monitor_output_Update(thismsg);
	process_socket_response(thismsg);
	processSerialPendingCommandLine(thismsg);
	const noNeedToShowMsg = ["<", "ok T:", "X:", "FR:", "echo:E0 Flow"].some((msgStart) => thismsg.startsWith(msgStart));
	if (!noNeedToShowMsg && thismsg !== "ok") {
		console.log(thismsg);
	}

	// Close stuck connection dialog if any message received from machine
	// This indicates the machine is connected and communicating
	const connectModal = id("connectdlg.html");
	const activeOnMsg = getactiveModal();
	if (connectModal && connectModal.style.display !== "none" &&
		activeOnMsg && activeOnMsg.name === "connectdlg.html") {
		console.log("SOCKET FIX: Machine message received - closing stuck connection dialog");
		closeModal("Machine connected");
	}
};

const processIncomingData = (data) => {
	serialReadBuffer += data;
	const lines = serialReadBuffer.split("\n");
	serialReadBuffer = lines.pop();
	lines.forEach((line) => processIncomingLine(line.replace("\r", "").trim()));
};

const writeToSerial = async (message) => {
	if (!serialWriter || !serialPort) {
		throw new Error("USB serial is not connected");
	}
	const payload = message.endsWith("\n") ? message : `${message}\n`;
	await serialWriter.write(serialTextEncoder.encode(payload));
};

const closeSerialTransport = async () => {
	try {
		if (serialPendingCommand) {
			completeSerialPendingCommand(true, "USB serial disconnected");
		}
		if (serialReader) {
			await serialReader.cancel();
			serialReader.releaseLock();
		}
		if (serialWriter) {
			serialWriter.releaseLock();
		}
		if (serialPort) {
			await serialPort.close();
		}
	} catch (error) {
		console.warn("Error while closing USB serial transport:", error);
	} finally {
		serialReader = null;
		serialWriter = null;
		serialPort = null;
		serialReaderRunning = false;
		serialReadBuffer = "";
	}
};

const readSerialLoop = async () => {
	if (!serialReader || serialReaderRunning) {
		return;
	}
	serialReaderRunning = true;
	try {
		while (serialPort && serialReader) {
			const { value, done } = await serialReader.read();
			if (done) {
				break;
			}
			if (value) {
				processIncomingData(serialTextDecoder.decode(value, { stream: true }));
			}
		}
	} catch (error) {
		console.warn("USB serial read loop ended:", error);
	} finally {
		serialReaderRunning = false;
	}
};

const connectSerialTransport = async () => {
	if (!("serial" in navigator)) {
		return false;
	}

	if (!serialPort) {
		serialPort = await navigator.serial.requestPort();
	}

	if (!serialPort.readable || !serialPort.writable) {
		await serialPort.open({ baudRate: 115200 });
	}

	serialWriter = serialPort.writable.getWriter();
	serialReader = serialPort.readable.getReader();
	readSerialLoop();

	return true;
};

const startSocket = () => {
	if (use_serial_transport) {
		ws_source = serialConnectionProxy;
		console.log("Connected over USB serial");
		cancelReconnectTimer();
		handlePing();
		const disabledModal = id("UIdisableddlg.html");
		if (disabledModal && disabledModal.style.display !== "none") {
			log_off = false;
			http_communication_locked = false;
			closeModal("Reconnected");
		}
		if (typeof onWSOpenCallback === 'function') {
			onWSOpenCallback();
		}
		if (typeof restoreGCodeState === 'function') {
			restoreGCodeState();
		}
		return;
	}

	// Nullify handlers on any existing socket before replacing it.
	// This prevents stale onclose callbacks from scheduling extra reconnects
	// and frees the firmware's WebSocket connection slot more quickly.
	if (ws_source) {
		ws_source.onopen = null;
		ws_source.onclose = null;
		ws_source.onerror = null;
		ws_source.onmessage = null;
		try { ws_source.close(); } catch (e) { console.debug('Error closing previous WebSocket:', e); }
	}
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
		return;
	}
	ws_source.binaryType = "arraybuffer";
	ws_source.onopen = (e) => {
		console.log("Connected");
		// Reconnected successfully after a disconnection: cancel the auto-retry timer,
		// restore communication flags, and close the disconnect dialog.
		cancelReconnectTimer();
		// Always arm the ping watchdog on every (re)connect,
		// including the very first connection on page load.
		handlePing();
		const disabledModal = id("UIdisableddlg.html");
		if (disabledModal && disabledModal.style.display !== "none") {
			log_off = false;
			http_communication_locked = false;
			closeModal("Reconnected");
		}
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
					processIncomingLine(thismsg);
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
			const activeOnMsg2 = getactiveModal();
			if (connectModal && connectModal.style.display !== "none" &&
				activeOnMsg2 && activeOnMsg2.name === "connectdlg.html") {
				console.log("SOCKET FIX: Machine message received - closing stuck connection dialog");
				closeModal("Machine connected");
			}
		}
		//console.log(msg);
	};
};
