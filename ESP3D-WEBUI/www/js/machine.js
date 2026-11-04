// Machine actions: named operations on the machine (retract belts, jog, stop, read a setting,
// ...) and one store of machine state. UI handlers call these instead of building command
// strings. This file knows what to send for each action and how to tell when it has finished;
// the lines themselves go through wsCommands (wscommands.js).
//
// Every action returns an Operation: a Promise that resolves when the action is DONE (the move
// has finished, the belts are retracted, the setting was read), with .accepted, a Promise that
// resolves when the firmware accepted the command that started it ("ok"). Both reject with a
// MachineError { reason, command, code?, detail }, reason being one of:
//   rejected    the firmware answered error:N (code is set)
//   not-sent    the connection was down; nothing reached the machine
//   disconnected the connection dropped before the reply came
//   timeout     no reply, or the operation never started
//   reset       cut short by stop()
//   cancelled   dropped by a jog cancel before it ran
//   failed      accepted, but the operation ended in a failure state
//   not-ready   a precondition failed; nothing was sent
//
// Units: millimetres and mm/min, always. Every motion line carries G21.

const machine = (() => {
	// Maslow calibration state machine (firmware MaslowEnums.h)
	const MASLOW = {
		UNKNOWN: 0,
		RETRACTING: 1,
		RETRACTED: 2,
		EXTENDING: 3,
		EXTENDEDOUT: 4,
		TAKING_SLACK: 5,
		CALIBRATION_IN_PROGRESS: 6,
		READY_TO_CUT: 7,
		RELEASE_TENSION: 8,
		CALIBRATION_COMPUTING: 9,
	};
	const MOVING_STATES = ["Run", "Jog", "Home"];
	// How long a Maslow operation may take to show its start state in a status report.
	const START_WAIT_MS = 3000;

	// ---------------------------------------------------------------- state store

	/**
	 * grbl:        FluidNC state name ("Idle", "Run", "Hold", "Jog", "Home", "Alarm", ...)
	 * grblDetail:  the part after ':' ("0" for a completed hold, "1" while stopping)
	 * maslow:      Maslow state number (MASLOW) from the MS status field, null until known
	 * mpos, wpos, wco: {x, y, z} in mm, null until known
	 * units:       the firmware's G-code units, "mm" or "inch" (machine actions always send G21)
	 * motorCurrent: {tl, tr, bl, br}, raw ADC 0-4095 (Debug tab converts to mA), from MC; null until known
 * job:         {pct, file, line} while a file runs, else null
	 * belts:       belt lengths tl/tr/bl/br (null = unknown) and position errors etl/etr/ebl/ebr (mm), from the BL/BE
 *              status fields (about once a second) and MINFO; kept between reports that
 *              don't carry them. MINFO also adds homed, extended, calibrationInProgress.
	 */
	const state = {
		grbl: "",
		grblDetail: "",
		maslow: null,
		mpos: null,
		wpos: null,
		wco: null,
		units: null,
		job: null,
		belts: null,
		motorCurrent: null,
	};
	let statusCount = 0; // status reports seen, to wait for a fresh one
	let beltLengthsFromStatus = false; // a BL field has been seen (firmware that reports it)
	const listeners = { state: new Set(), line: new Set() };

	/** Subscribe to "state" (a state snapshot after each change) or "line" (every line from
	 * the machine). Returns an unsubscribe function. */
	const on = (event, fn) => {
		listeners[event].add(fn);
		return () => listeners[event].delete(fn);
	};

	const emit = (event, value) => {
		for (const fn of [...listeners[event]]) {
			try {
				fn(value);
			} catch (e) {
				console.error(`machine: ${event} listener failed:`, e);
			}
		}
	};

	const snapshot = () => ({ ...state, conn: wsCommands.health() });

	const xyz = (values) => {
		const [x, y, z] = values.split(",").map(Number);
		return { x, y, z };
	};
	const minus = (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
	const plus = (a, b) => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });

	/** <State|MPos:x,y,z,...|FS:f,s|WCO:...|SD:pct,file,line> */
	const parseStatus = (msg) => {
		const fields = msg.slice(1, msg.lastIndexOf(">")).split("|");
		const [name, detail = ""] = fields[0].split(":");
		state.grbl = name;
		state.grblDetail = detail;
		let mpos = null;
		let wpos = null;
		let job = null;
		for (const field of fields.slice(1)) {
			const i = field.indexOf(":");
			const key = field.slice(0, i);
			const value = field.slice(i + 1);
			if (key === "MPos") {
				mpos = xyz(value);
			} else if (key === "WPos") {
				wpos = xyz(value);
			} else if (key === "WCO") {
				state.wco = xyz(value);
			} else if (key === "MS") {
				state.maslow = Number(value);
			} else if (key === "BL") {
				// "?" = that belt's length is unknown (e.g. it moved while the motors were off)
				const [tl, tr, bl, br] = value.split(",").map((v) => (v === "?" ? null : Number(v)));
				state.belts = { ...state.belts, tl, tr, bl, br };
				beltLengthsFromStatus = true;
			} else if (key === "BE") {
				const [etl, etr, ebl, ebr] = value.split(",").map(Number);
				state.belts = { ...state.belts, etl, etr, ebl, ebr };
			} else if (key === "MC") {
				const [tl, tr, bl, br] = value.split(",").map(Number);
				state.motorCurrent = { tl, tr, bl, br };
			} else if (key === "SD") {
				const [pct, file, line] = value.split(",");
				job = { pct: Number(pct), file, line: line ? Number(line) : undefined };
			}
		}
		if (mpos) {
			state.mpos = mpos;
			state.wpos = state.wco ? minus(mpos, state.wco) : state.wpos;
		} else if (wpos) {
			state.wpos = wpos;
			state.mpos = state.wco ? plus(wpos, state.wco) : state.mpos;
		}
		state.job = job;
		statusCount++;
	};

	/** Feed every complete line received from the machine through here (socket.js). */
	const handleLine = (msg) => {
		let changed = false;
		if (msg.startsWith("<")) {
			parseStatus(msg);
			changed = true;
		} else if (msg.startsWith("MINFO: ")) {
			try {
				const info = JSON.parse(msg.slice("MINFO: ".length));
				if (beltLengthsFromStatus) {
					// The status report's BL knows which lengths are unknown; MINFO gives raw
					// encoder values for those, so it doesn't override the lengths.
					delete info.tl;
					delete info.tr;
					delete info.bl;
					delete info.br;
				}
				state.belts = { ...state.belts, ...info };
				changed = true;
			} catch (e) {
				console.warn("machine: unreadable MINFO", msg);
			}
		} else if (msg.startsWith("[GC:")) {
			const units = msg.includes(" G20") ? "inch" : msg.includes(" G21") ? "mm" : null;
			if (units && units !== state.units) {
				state.units = units;
				changed = true;
			}
		}
		emit("line", msg);
		if (changed) {
			emit("state", snapshot());
		}
	};

	// ---------------------------------------------------------------- operations

	const machineError = (reason, command, extra = {}) =>
		Object.assign(new Error(extra.detail ?? `${command}: ${reason}`), { reason, command, ...extra });

	// wsCommands' error codes → MachineError
	const toError = (command, code) => {
		if (/^\d+$/.test(String(code))) {
			return machineError("rejected", command, { code: Number(code), detail: `${command}: error ${code}` });
		}
		const reason =
			{ "not sent": "not-sent", disconnected: "disconnected", timeout: "timeout", reset: "reset", flushed: "cancelled" }[code] ??
			"failed";
		return machineError(reason, command);
	};

	/** Operations still running, so stop() can end them. */
	const running = new Set();

	/**
	 * Make an Operation. start({accept, resolve, reject, ended}) does the work; it may be async,
	 * and a throw rejects the operation. Rejecting after accept() leaves .accepted resolved.
	 * ended resolves once the operation has settled either way (including stop() ending it);
	 * pass it to waitFor so its listener goes away.
	 */
	const operation = (name, start) => {
		let acceptOk;
		let acceptFail;
		const accepted = new Promise((res, rej) => {
			acceptOk = res;
			acceptFail = rej;
		});
		accepted.catch(() => {}); // most callers only await the operation itself
		let markEnded;
		const ended = new Promise((res) => {
			markEnded = res;
		});
		const done = new Promise((resolve, reject) => {
			const entry = {
				name,
				reject: (err) => {
					running.delete(entry);
					markEnded();
					acceptFail(err);
					reject(err);
				},
			};
			running.add(entry);
			const ctl = {
				accept: () => acceptOk(),
				resolve: (value) => {
					running.delete(entry);
					markEnded();
					acceptOk();
					resolve(value);
				},
				reject: entry.reject,
				ended,
			};
			(async () => start(ctl))().catch(ctl.reject);
		});
		done.accepted = accepted;
		return done;
	};

	/** An operation that fails at once (precondition), sending nothing. */
	const notReady = (name, detail) =>
		operation(name, ({ reject }) => reject(machineError("not-ready", name, { detail })));

	/**
	 * Send one line; resolves on ok (with the collected lines if collect), rejects with a
	 * MachineError. Quiet by default: the caller of the action reports failures, so wsCommands
	 * doesn't also post "Machine rejected" and grbl.js doesn't treat it as a fault.
	 */
	const sendLine = (line, { collect = false, quiet = true } = {}) =>
		new Promise((resolve, reject) => {
			const sent = wsCommands.send(line, {
				collect,
				quiet,
				onOk: (lines) => resolve(lines ?? []),
				onError: (code) => reject(toError(line, code)),
			});
			if (!sent) {
				reject(machineError("not-sent", line, { detail: `${line}: not sent (connection ${wsCommands.socketStatus()})` }));
			}
		});

	/** Send a realtime byte; rejects with not-sent if the socket is down. */
	const sendRealtime = (code, name) =>
		wsCommands.sendRealtime(code) ? Promise.resolve() : Promise.reject(machineError("not-sent", name));

	/**
	 * Resolve with a snapshot once predicate(snapshot) is true. Rejects with "failed" if
	 * failIf(snapshot) is true first, or "timeout" after timeoutMs (0 = never). Pass the
	 * operation's ended promise so the wait stops listening when stop() ends the operation.
	 */
	const waitFor = (predicate, { failIf = () => false, timeoutMs = 0, what = "condition", failDetail, ended } = {}) =>
		new Promise((resolve, reject) => {
			let timer = null;
			let off = () => {};
			const finish = () => {
				off();
				clearTimeout(timer);
			};
			const check = (s) => {
				if (predicate(s)) {
					finish();
					resolve(s);
				} else if (failIf(s)) {
					finish();
					reject(machineError("failed", what, { detail: failDetail ? failDetail(s) : `${what} failed` }));
				}
			};
			off = on("state", check);
			if (ended) {
				ended.then(finish);
			}
			if (timeoutMs) {
				timer = setTimeout(() => {
					finish();
					reject(machineError("timeout", what, { detail: `${what}: timed out` }));
				}, timeoutMs);
			}
			check(snapshot());
		});

	/**
	 * Start watching for a motion to finish; call before sending the motion line, because its
	 * "ok" and the status reports that follow can arrive together. wait(ended) then resolves
	 * once the machine is Idle again. A short move can finish between two status reports, so
	 * Idle counts once a moving state has been seen or two fresh reports have arrived (the 1 s
	 * status probe guarantees those come).
	 */
	const trackMotion = (what) => {
		const countAtStart = statusCount;
		let sawMoving = false;
		const offTrack = on("state", (s) => {
			if (MOVING_STATES.includes(s.grbl)) {
				sawMoving = true;
			}
		});
		return {
			stop: offTrack,
			wait: (ended) =>
				waitFor((s) => s.grbl === "Idle" && (sawMoving || statusCount - countAtStart >= 2), {
					failIf: (s) => s.grbl === "Alarm",
					what,
					failDetail: () => `${what}: machine alarmed`,
					ended,
				}).finally(offTrack),
		};
	};

	const stateName = (n) => Object.keys(MASLOW).find((k) => MASLOW[k] === n) ?? `state ${n}`;

	/**
	 * A Maslow belt operation: its "ok" only means it started. It is done when the Maslow state
	 * reaches `success` and the firmware is Idle again; `failure` states or an alarm fail it.
	 */
	const maslowOperation = (name, command, { start, success, failure = [] }) =>
		operation(name, async ({ accept, resolve, ended }) => {
			// Already in the success state (e.g. retract when retracted): only the start state
			// then shows the operation began, or a stale report would look like it had finished.
			const stateBefore = state.maslow;
			await sendLine(command);
			accept();
			// The firmware changes state before it answers "ok", and every status report carries
			// the state (MS). A report already on its way when the ok arrived can be from just
			// before the change, so a refusal (the state didn't move) is only concluded from the
			// second report after the ok. Ask for one now; the UI's 1 s probe brings the next.
			const countAtOk = statusCount;
			wsCommands.sendRealtime(0x3f);
			await waitFor((s) => s.maslow === start || (s.maslow === success && stateBefore !== success), {
				failIf: () => statusCount - countAtOk >= 2,
				failDetail: (s) => `${name} didn't start: the machine stayed in ${stateName(s.maslow)}`,
				timeoutMs: START_WAIT_MS,
				what: `${name} to start`,
				ended,
			});
			await waitFor((s) => s.maslow === success && s.grbl === "Idle", {
				failIf: (s) => failure.includes(s.maslow) || s.grbl === "Alarm",
				what: name,
				failDetail: (s) => `${name} failed: ${s.grbl === "Alarm" ? "machine alarmed" : `now ${stateName(s.maslow)}`}`,
				ended,
			});
			resolve();
		});

	/** An operation that is done when the firmware accepts its line. */
	const simpleOperation = (name, line) =>
		operation(name, async ({ resolve }) => {
			await sendLine(line);
			resolve();
		});

	const fmt = (n) => Number(n.toFixed(3)).toString();

	const axisWords = (target) =>
		["x", "y", "z"]
			.filter((a) => target[a] !== undefined)
			.map((a) => {
				if (!Number.isFinite(target[a])) {
					throw new TypeError(`machine: ${a} must be a number of mm, got ${target[a]}`);
				}
				return `${a.toUpperCase()}${fmt(target[a])}`;
			});

	// XY motion needs known belt lengths; Z alone doesn't (the existing jog rule).
	const xyNotReady = (target) =>
		(target.x !== undefined || target.y !== undefined) && state.maslow !== MASLOW.READY_TO_CUT;

	// ---------------------------------------------------------------- actions

	// Belts and calibration
	const retractBelts = () =>
		maslowOperation("retract belts", "$ALL", { start: MASLOW.RETRACTING, success: MASLOW.RETRACTED });
	const extendBelts = () =>
		maslowOperation("extend belts", "$EXT", {
			start: MASLOW.EXTENDING,
			success: MASLOW.EXTENDEDOUT,
			failure: [MASLOW.UNKNOWN],
		});
	const applyTension = () =>
		maslowOperation("apply tension", "$TKSLK", {
			start: MASLOW.TAKING_SLACK,
			success: MASLOW.READY_TO_CUT,
			failure: [MASLOW.EXTENDEDOUT],
		});
	const calibrate = () =>
		maslowOperation("calibrate", "$CAL", {
			start: MASLOW.CALIBRATION_IN_PROGRESS,
			success: MASLOW.READY_TO_CUT,
			failure: [MASLOW.EXTENDEDOUT],
		});
	const releaseTension = () =>
		maslowOperation("release tension", "$CMP", { start: MASLOW.RELEASE_TENSION, success: MASLOW.EXTENDEDOUT });
	const setZStop = () => simpleOperation("set Z stop", "$SETZSTOP");
	const resetCalibration = () => simpleOperation("reset calibration", "$CALRESET");

	// Motion
	/** Relative jog by {x, y, z} mm at feed mm/min. Done when the jog has finished. */
	const jog = (delta, { feed } = {}) => {
		const words = axisWords(delta);
		if (!words.length) {
			throw new TypeError("machine.jog: no axis given");
		}
		if (!(feed > 0)) {
			throw new TypeError(`machine.jog: feed must be > 0 mm/min, got ${feed}`);
		}
		if (xyNotReady(delta)) {
			return notReady("jog", "Can't jog X/Y: belt lengths are unknown (machine not ready to cut)");
		}
		const line = `$J=G91 G21 F${fmt(feed)} ${words.join(" ")}`;
		return operation("jog", async ({ accept, resolve, ended }) => {
			const motion = trackMotion("jog");
			ended.then(motion.stop);
			await sendLine(line);
			accept();
			await motion.wait(ended);
			resolve();
		});
	};

	/** Stop the running jog (decelerates) and drop queued jogs. */
	const jogCancel = () =>
		operation("jog cancel", async ({ accept, resolve, ended }) => {
			await sendRealtime(wsCommands.JOG_CANCEL, "jog cancel");
			accept();
			await waitFor((s) => s.grbl !== "Jog", { what: "jog cancel", ended });
			resolve();
		});

	/**
	 * Move to {x, y, z} mm in work coordinates (machine coordinates with machineCoords), as a
	 * rapid (G0) or at feed mm/min (G1). Done when the move has finished.
	 */
	const moveTo = (target, { feed, machineCoords = false } = {}) => {
		const words = axisWords(target);
		if (!words.length) {
			throw new TypeError("machine.moveTo: no axis given");
		}
		if (xyNotReady(target)) {
			return notReady("move", "Can't move X/Y: belt lengths are unknown (machine not ready to cut)");
		}
		const motion = feed ? `G1 F${fmt(feed)}` : "G0";
		const line = `G90 G21 ${machineCoords ? "G53 " : ""}${motion} ${words.join(" ")}`;
		return operation("move", async ({ accept, resolve, ended }) => {
			const motion = trackMotion("move");
			ended.then(motion.stop);
			await sendLine(line);
			accept();
			await motion.wait(ended);
			resolve();
		});
	};

	// Stopping and pausing
	/** End any running operation now: 0x18, then $STOP (raise Z, save). Done when $STOP is answered. */
	const stop = () => {
		for (const entry of [...running]) {
			entry.reject(machineError("reset", entry.name, { detail: `${entry.name}: stopped` }));
		}
		return operation("stop", ({ accept, resolve, reject }) => {
			const sent = wsCommands.stop({
				onOk: () => resolve(),
				onError: (code) => reject(toError("$STOP", code)),
			});
			if (!sent) {
				reject(machineError("not-sent", "stop", { detail: `stop: not sent (connection ${wsCommands.socketStatus()})` }));
				return;
			}
			accept();
		});
	};

	/** Feed hold a running program. Done when the hold has completed (Hold:0). */
	const pause = () => {
		if (state.grbl !== "Run") {
			return notReady("pause", `Nothing to pause (machine is ${state.grbl || "unknown"})`);
		}
		return operation("pause", async ({ accept, resolve, ended }) => {
			await sendRealtime(0x21, "pause");
			accept();
			await waitFor((s) => s.grbl === "Hold" && s.grblDetail === "0", {
				failIf: (s) => s.grbl === "Alarm",
				what: "pause",
				ended,
			});
			resolve();
		});
	};

	/** Resume from a hold. Done when the machine is running (or Idle, if nothing was left). */
	const resume = () => {
		if (state.grbl !== "Hold") {
			return notReady("resume", `Nothing to resume (machine is ${state.grbl || "unknown"})`);
		}
		return operation("resume", async ({ accept, resolve, ended }) => {
			await sendRealtime(0x7e, "resume");
			accept();
			await waitFor((s) => s.grbl === "Run" || s.grbl === "Idle", {
				failIf: (s) => s.grbl === "Alarm",
				what: "resume",
				ended,
			});
			resolve();
		});
	};

	// Settings and status
	/** Read a config value, e.g. getSetting("kinematics/MaslowKinematics/fixedZ") → "false". */
	const getSetting = (path) =>
		operation(`read ${path}`, async ({ resolve }) => {
			const prefix = `$/${path}=`.toLowerCase();
			const lines = await sendLine(`$/${path}`, { collect: true });
			const hit = lines.find((l) => l.toLowerCase().startsWith(prefix));
			if (!hit) {
				throw machineError("failed", `$/${path}`, { detail: `$/${path}: no value in the reply` });
			}
			resolve(hit.slice(prefix.length));
		});

	/** Set a config value (in memory; saveConfig() writes the config file). */
	const setSetting = (path, value) => simpleOperation(`set ${path}`, `$/${path}=${value}`);

	/** Write the running config to the config file. */
	const saveConfig = () => simpleOperation("save config", "$CO");

	/** Ask for a fresh status report (which carries the Maslow state), MINFO and G-code modes. */
	const refreshStatus = () =>
		operation("refresh status", async ({ resolve }) => {
			await sendRealtime(0x3f, "status");
			await Promise.all([sendLine("$MINFO"), sendLine("$G")]);
			resolve();
		});

	return {
		// state
		get state() {
			return snapshot();
		},
		on,
		handleLine,
		MASLOW,
		// belts and calibration
		retractBelts,
		extendBelts,
		applyTension,
		calibrate,
		releaseTension,
		setZStop,
		resetCalibration,
		// motion
		jog,
		jogCancel,
		moveTo,
		// stopping
		stop,
		pause,
		resume,
		// settings and status
		getSetting,
		setSetting,
		saveConfig,
		refreshStatus,
		// for building more actions and for tests
		waitFor,
		runningCount: () => running.size,
	};
})();

globalThis.machine = machine;
