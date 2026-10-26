// Ordered command channel to the machine over the WebSocket.
//
// Everything sent here goes down one socket, so the firmware sees commands in the order the
// user issued them (HTTP /command requests can overtake each other and the socket). This is
// the same line protocol a USB serial connection speaks, so it can later drive USB too.
//
// Reply tracking: the firmware answers every line it runs from this socket with exactly one
// "ok" or "error:N", in order. Realtime bytes (0x85, '!', '~', '?') get no reply. We keep a
// FIFO of lines waiting for a reply and match replies to it.
//
// Limitation until every command moves here: commands still sent over HTTP /command also run
// on this socket and also get replies, so a reply can be credited to the wrong pending line.
// Errors are therefore reported as "probably from". An "ok" with nothing pending is ignored.

const wsCommands = (() => {
	const JOG_CANCEL = 0x85;
	// Drop entries that have waited this long; their reply was lost (e.g. flushed by firmware).
	const STALE_MS = 120000;

	// Connection health: anything received from the machine proves the link is alive. When
	// idle the machine sends little, so ask for a status report ('?', realtime: no "ok").
	const PROBE_MS = 1000;
	const QUIET_MS = 2500; // nothing heard for this long: link may be dead though "open"

	/** @type {{line: string, onOk?: Function, onError?: Function, sentAt: number}[]} */
	const pending = [];
	let lastState = "";
	let lastRxMs = 0;

	const socket = () => (typeof ws_source !== "undefined" ? ws_source : null);

	const isOpen = () => {
		const ws = socket();
		return !!ws && ws.readyState === WebSocket.OPEN;
	};

	/** Why the socket can't send right now, for messages ("connecting", "closed", ...). */
	const socketStatus = () => {
		const ws = socket();
		if (!ws) {
			return "no socket";
		}
		return ["connecting", "open", "closing", "closed"][ws.readyState] ?? `state ${ws.readyState}`;
	};

	const rawSend = (text) => {
		if (!isOpen()) {
			return false;
		}
		try {
			socket().send(text);
			return true;
		} catch (e) {
			console.warn("WebSocket send failed:", e);
			return false;
		}
	};

	const dropStale = () => {
		const cutoff = Date.now() - STALE_MS;
		while (pending.length && pending[0].sentAt < cutoff) {
			const c = pending.shift();
			console.warn(`No reply to '${c.line}'; dropped`);
		}
	};

	const report = (text) => {
		if (typeof addMessage === "function") {
			addMessage(text);
		} else {
			console.warn(text);
		}
	};

	/**
	 * Send one command line. Returns false (and sends nothing) if the socket isn't open.
	 * onOk() / onError(code) are called when its reply arrives.
	 */
	const send = (line, { onOk, onError } = {}) => {
		const text = line.endsWith("\n") ? line : `${line}\n`;
		if (!rawSend(text)) {
			return false;
		}
		dropStale();
		pending.push({ line: text.trim(), onOk, onError, sentAt: Date.now() });
		return true;
	};

	/** Send a realtime byte (no reply expected). Returns false if the socket isn't open. */
	const sendRealtime = (code) => {
		if (!rawSend(String.fromCharCode(code))) {
			return false;
		}
		// During a jog the firmware cancels the running line and drops every line it hasn't
		// read yet (RealtimeCmd.cpp). The oldest pending line is the one running; the rest
		// will never be answered.
		if (code === JOG_CANCEL && lastState === "Jog" && pending.length > 1) {
			for (const c of pending.splice(1)) {
				if (c.onError) {
					c.onError("flushed");
				}
			}
		}
		return true;
	};

	/** Call for every frame received on the socket, of any kind. */
	const noteActivity = () => {
		lastRxMs = Date.now();
	};

	/** "ok" (socket open, heard recently), "quiet" (open but silent) or "down" (not open). */
	const health = () => {
		if (!isOpen()) {
			return "down";
		}
		return Date.now() - lastRxMs > QUIET_MS ? "quiet" : "ok";
	};

	const msSinceRx = () => (lastRxMs ? Date.now() - lastRxMs : null);

	/** Feed every complete line received from the machine through here. */
	const handleLine = (msg) => {
		noteActivity();
		if (msg.startsWith("<")) {
			// Status report: <Idle|MPos:...>
			lastState = msg.slice(1).split(/[|:>]/)[0];
			return;
		}
		if (msg === "ok") {
			const c = pending.shift();
			if (c && c.onOk) {
				c.onOk();
			}
			return;
		}
		if (msg.startsWith("error:")) {
			const code = msg.slice("error:".length).trim();
			const c = pending.shift();
			if (c) {
				report(`Machine rejected '${c.line}' (probably): error ${code}`);
				if (c.onError) {
					c.onError(code);
				}
			} else {
				report(`Machine reported error ${code}`);
			}
		}
	};

	/** Call when a new socket opens: replies to lines sent on the old one will never come. */
	const reset = () => {
		pending.length = 0;
		noteActivity(); // a fresh open counts as hearing from the machine
	};

	// Keep the machine talking so a dead link shows up within seconds (browser only).
	if (typeof document !== "undefined") {
		setInterval(() => {
			if (isOpen()) {
				rawSend("?");
			}
		}, PROBE_MS);
	}

	return {
		send,
		sendRealtime,
		handleLine,
		reset,
		isOpen,
		socketStatus,
		noteActivity,
		health,
		msSinceRx,
		pendingCount: () => pending.length,
		lastState: () => lastState,
		JOG_CANCEL,
	};
})();

globalThis.wsCommands = wsCommands;
