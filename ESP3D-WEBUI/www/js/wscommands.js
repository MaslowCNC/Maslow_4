// Ordered command channel to the machine over the WebSocket.
//
// Every line command the UI sends goes through here (SendPrinterCommand routes to send()), so
// the firmware sees commands in the order the user issued them, and this layer sees every line
// on the tab's firmware channel. Only [ESP...] commands still use HTTP; those are answered in the
// HTTP body, never with an "ok" on the socket. This is the same line protocol a USB serial
// connection speaks, so it can later drive USB too.
//
// Reply tracking: the firmware answers every line it runs from this socket with exactly one
// "ok" or "error:N", in order. Realtime bytes (0x85, '!', '~', '?', 0x18) get no reply and are
// sent at once, never queued. Lines are sent one at a time (see pump()), so each reply belongs
// to the one line on the wire. An "ok" with nothing on the wire (e.g. a reply to a line sent
// on an earlier socket) is ignored.

const wsCommands = (() => {
	const JOG_CANCEL = 0x85;
	const RESET = 0x18;
	// A line with no reply after this long is given up on (its reply was lost) so the queue
	// keeps moving. The clock only runs while the machine isn't moving: Maslow $ commands wait
	// for queued motion to finish before they run and reply.
	const REPLY_TIMEOUT_MS = 5000;
	const BUSY_STATES = ["Run", "Jog", "Home", "Hold", "Door"];

	// Connection health: anything received from the machine proves the link is alive. When
	// idle the machine sends little, so ask for a status report ('?', realtime: no "ok").
	const PROBE_MS = 1000;
	const QUIET_MS = 2500; // nothing heard for this long: link may be dead though "open"

	// One line on the wire at a time: the next is sent when the previous one's ok/error arrives.
	// The firmware drops whole lines without a reply when a channel's input queue is full
	// (Channel::queue_push), so we pace. Firmware before the WSChannel output-mutex fix also
	// lost replies under bursts; pacing keeps this UI safe on those builds too.
	/** @typedef {{line: string, onOk?: Function, onError?: Function, quiet?: boolean, lines?: string[], sentAt?: number, quietSince?: number}} Cmd */
	/** @type {Cmd | null} */
	let inflight = null;
	/** @type {Cmd[]} */
	const outbox = [];
	let lastState = "";
	let lastRxMs = 0;

	// Debug log of every send and the reply matched to it, in the browser console.
	// Turn on with wsCommands.setDebug(true); it is remembered in this browser across reloads.
	const DEBUG_KEY = "wsCommandsDebug";
	let debug = false;
	try {
		debug = typeof localStorage !== "undefined" && localStorage.getItem(DEBUG_KEY) === "1";
	} catch (e) {
		// storage blocked: stay off
	}
	const log = (text) => {
		if (debug) {
			console.log(`[ws] ${text}`);
		}
	};
	const setDebug = (on) => {
		debug = !!on;
		try {
			if (debug) {
				localStorage.setItem(DEBUG_KEY, "1");
			} else {
				localStorage.removeItem(DEBUG_KEY);
			}
		} catch (e) {
			// not persisted; still applies to this page
		}
		return debug;
	};
	const waited = (c) => `${Date.now() - c.sentAt} ms`;
	const pendingCount = () => (inflight ? 1 : 0) + outbox.length;

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

	const report = (text) => {
		if (typeof addMessage === "function") {
			addMessage(text);
		} else {
			console.warn(text);
		}
	};

	const fail = (c, why) => {
		log(`✕ '${c.line}' ${why}`);
		if (c.onError) {
			c.onError(why);
		}
	};

	/** Put the next queued line on the wire if nothing is waiting for a reply. */
	const pump = () => {
		while (!inflight && outbox.length) {
			const c = outbox.shift();
			if (!rawSend(`${c.line}\n`)) {
				fail(c, "not sent");
				for (const rest of outbox.splice(0)) {
					fail(rest, "not sent");
				}
				return;
			}
			c.sentAt = Date.now();
			c.quietSince = c.sentAt;
			inflight = c;
			log(`→ '${c.line}' (${outbox.length} queued behind it)`);
		}
	};

	/** Give up on a line whose reply never came, so the lines behind it can go. */
	const checkTimeout = () => {
		if (inflight && Date.now() - inflight.quietSince > REPLY_TIMEOUT_MS) {
			const c = inflight;
			inflight = null;
			report(`No reply from the machine to '${c.line}' after ${waited(c)}; carrying on`);
			fail(c, "timeout");
			pump();
		}
	};

	/**
	 * Send one command line. Returns false (and sends nothing) if the socket isn't open.
	 * Lines go out one at a time, in order; onOk() / onError(code) run when its reply arrives.
	 * urgent: go ahead of lines still queued here (not ahead of the one on the wire).
	 * quiet: don't show "Machine rejected" for an error reply (the caller handles it).
	 * collect: onOk(lines) receives the lines that arrived while this line was on the wire
	 *   (its output, e.g. "$/path=value"; unrelated log lines can be among them).
	 */
	const send = (line, { onOk, onError, urgent = false, quiet = false, collect = false } = {}) => {
		const text = line.trim();
		if (!isOpen()) {
			log(`not sent (socket ${socketStatus()}): '${text}'`);
			return false;
		}
		checkTimeout();
		const c = { line: text, onOk, onError, quiet, lines: collect ? [] : undefined };
		if (urgent) {
			outbox.unshift(c);
		} else {
			outbox.push(c);
		}
		pump();
		return true;
	};

	/** Send a realtime byte (no reply expected). Returns false if the socket isn't open. */
	const sendRealtime = (code) => {
		if (!rawSend(String.fromCharCode(code))) {
			return false;
		}
		log(`→ realtime 0x${code.toString(16)} (state ${lastState || "unknown"})`);
		// During a jog the firmware cancels the running jog and drops this channel's unread lines
		// (RealtimeCmd.cpp). Lines still queued here would have been dropped there, so drop them.
		// The line on the wire still gets its reply.
		if (code === JOG_CANCEL && lastState === "Jog") {
			for (const c of outbox.splice(0)) {
				fail(c, "flushed");
			}
		}
		// A soft reset discards every channel's unread input (Protocol.cpp, allChannels.flushRx),
		// so nothing still waiting will be answered.
		if (code === RESET) {
			const dropped = inflight ? [inflight, ...outbox.splice(0)] : outbox.splice(0);
			inflight = null;
			for (const c of dropped) {
				fail(c, "reset");
			}
		}
		return true;
	};

	const ANOTHER_INTERFACE_BUSY = "120";
	const RESTART_WAIT_MS = 1500;
	const RESTART_POLL_MS = 25;
	let restarts = 0; // restart banners seen ("Grbl 3.x [FluidNC ...]")

	/**
	 * Stop whatever the machine is doing, at once, in any state.
	 * 1. 0x18 (soft reset): realtime, so it acts the moment it arrives. The firmware stops
	 *    stepping, ends any job, stops the spindle and belt motors. On a Maslow it syncs the
	 *    belts to the encoders and goes Idle rather than alarming (Protocol.cpp
	 *    protocol_do_rt_reset), since the encoders still know where the belts are.
	 * 2. $STOP once the firmware has restarted (its banner arrives): raises Z clear of the work
	 *    and saves positions. Sent after the restart because the restart discards every
	 *    channel's unread input.
	 * Returns false if the socket isn't open.
	 */
	const stop = ({ onOk, onError } = {}) => {
		if (!isOpen()) {
			return false;
		}
		const sendStopLine = () =>
			send("$STOP", {
				urgent: true,
				quiet: true,
				onOk,
				onError: (code) => {
					if (code === ANOTHER_INTERFACE_BUSY) {
						// The 0x18 before it already stopped the machine; count it as stopped.
						log("$STOP answered 120 (another interface is running a job)");
						if (onOk) {
							onOk();
						}
						return;
					}
					if (/^\d+$/.test(code)) {
						report(`Machine rejected '$STOP': error ${code}`);
					}
					if (onError) {
						onError(code);
					}
				},
			}) || (onError && onError("not sent"));
		const restartsBefore = restarts;
		sendRealtime(RESET);
		const start = Date.now();
		const waitForRestart = () => {
			if (restarts === restartsBefore && Date.now() - start < RESTART_WAIT_MS) {
				setTimeout(waitForRestart, RESTART_POLL_MS);
				return;
			}
			log(`stop: ${restarts === restartsBefore ? "no restart banner" : "restarted"} after ${Date.now() - start} ms; sending $STOP`);
			sendStopLine();
		};
		setTimeout(waitForRestart, RESTART_POLL_MS);
		return true;
	};

	/** True for a one-character command that the firmware acts on immediately (no reply). */
	const isRealtime = (text) => {
		if (text.length !== 1) {
			return false;
		}
		const code = text.charCodeAt(0);
		return text === "?" || text === "!" || text === "~" || code === RESET || code >= 0x80;
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

	/**
	 * Feed every complete line received from the machine through here. Returns true when the
	 * line was an expected error for a line sent with quiet: the caller should not process it
	 * further (no fault handling).
	 */
	const handleLine = (msg) => {
		noteActivity();
		if (msg.startsWith("Grbl ")) {
			restarts++; // restart banner, e.g. after a soft reset
			return false;
		}
		if (msg.startsWith("<")) {
			// Status report: <Idle|MPos:...>
			lastState = msg.slice(1).split(/[|:>]/)[0];
			if (inflight && BUSY_STATES.includes(lastState)) {
				inflight.quietSince = Date.now(); // moving: a slow reply is expected
			}
			return false;
		}
		if (msg !== "ok" && !msg.startsWith("error:")) {
			if (inflight && inflight.lines && msg) {
				inflight.lines.push(msg);
			}
			return false;
		}
		if (msg === "ok") {
			const c = inflight;
			if (!c) {
				log("← ok, nothing waiting");
				return false;
			}
			inflight = null;
			log(`← ok for '${c.line}' after ${waited(c)}`);
			if (c.onOk) {
				c.onOk(c.lines);
			}
			pump();
			return false;
		}
		if (msg.startsWith("error:")) {
			const code = msg.slice("error:".length).trim();
			const c = inflight;
			if (!c) {
				log(`← error ${code}, nothing waiting`);
				report(`Machine reported error ${code}`);
				return false;
			}
			inflight = null;
			log(`← error ${code} for '${c.line}' after ${waited(c)}`);
			if (!c.quiet) {
				report(`Machine rejected '${c.line}': error ${code}`);
			}
			if (c.onError) {
				c.onError(code);
			}
			pump();
			return !!c.quiet;
		}
		return false;
	};

	/** Call when a new socket opens: replies to lines sent on the old one will never come. */
	const reset = () => {
		const forgotten = inflight ? [inflight, ...outbox] : [...outbox];
		if (forgotten.length) {
			log(`socket reopened; forgot ${forgotten.length}: ${forgotten.map((c) => `'${c.line}'`).join(", ")}`);
		}
		inflight = null;
		outbox.length = 0;
		noteActivity(); // a fresh open counts as hearing from the machine
		for (const c of forgotten) {
			if (c.onError) {
				c.onError("disconnected");
			}
		}
	};

	// Keep the machine talking so a dead link shows up within seconds (browser only).
	if (typeof document !== "undefined") {
		setInterval(() => {
			if (isOpen()) {
				rawSend("?");
			}
			checkTimeout();
		}, PROBE_MS);
	}

	return {
		send,
		sendRealtime,
		stop,
		isRealtime,
		handleLine,
		checkTimeout,
		reset,
		isOpen,
		socketStatus,
		noteActivity,
		health,
		msSinceRx,
		setDebug,
		pendingCount,
		lastState: () => lastState,
		JOG_CANCEL,
		RESET,
	};
})();

globalThis.wsCommands = wsCommands;
