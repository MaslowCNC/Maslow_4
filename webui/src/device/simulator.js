import { COMMANDS } from "./commands.js";
import { restarted, spindleFault } from "../flow/incidents.js";
import { readGcode } from "../gcode/reader.js";

// A stand-in for the machine. It holds the state the firmware would report and, whenever the real device would
// answer (a step finishing, a fault, a tool being found), it asks the tester through `pending` instead of guessing.
// A future firmware adapter implements the same interface: getState(), subscribe(fn), send(action, payload).
//
// Maslow states, as in the firmware: 0 UNKNOWN, 1 RETRACTING, 2 RETRACTED, 3 EXTENDING, 4 EXTENDEDOUT,
// 5 TAKING_SLACK, 6 CALIBRATION_IN_PROGRESS, 7 READY_TO_CUT, 8 RELEASE_TENSION.

const FILES = [
  { name: "sign-panel.nc", size: "182 KB", w: 482, h: 300, minutes: 18, depth: 3.0 },
  { name: "coaster-set.nc", size: "64 KB", w: 300, h: 200, minutes: 9, depth: 4.0 },
  { name: "shelf-bracket.nc", size: "311 KB", w: 610, h: 240, minutes: 26, depth: 12.0 }
];

const time = () => new Date().toTimeString().slice(0, 8);

// Stand-in G-code for a demo file, so the preview has something to draw: a w × h outline with rounded corners and a
// circle in the middle, cut in 3 mm passes down to the file's depth. Work coordinates, X 0 Y 0 at the lower left.
export function sampleGcode({ name, w, h, depth }) {
  const r = Math.min(20, w / 4, h / 4), R = Math.min(w, h) / 4, cx = w / 2, cy = h / 2;
  const n = (v) => +v.toFixed(3);
  const out = [`(${name}: simulated sample)`, "G21 G90 G17", "G0 Z5", "M3 S18000"];
  for (let z = -Math.min(3, depth); ; z = Math.max(z - 3, -depth)) {
    out.push(`G0 X${n(r)} Y0`, `G1 Z${n(z)} F600`, `G1 X${n(w - r)} Y0 F2000`, `G3 X${n(w)} Y${n(r)} I0 J${n(r)}`,
      `G1 X${n(w)} Y${n(h - r)}`, `G3 X${n(w - r)} Y${n(h)} I${n(-r)} J0`, `G1 X${n(r)} Y${n(h)}`,
      `G3 X0 Y${n(h - r)} I0 J${n(-r)}`, `G1 X0 Y${n(r)}`, `G3 X${n(r)} Y0 I${n(r)} J0`, "G0 Z5",
      `G0 X${n(cx + R)} Y${n(cy)}`, `G1 Z${n(z)} F600`, `G2 X${n(cx + R)} Y${n(cy)} I${n(-R)} J0 F2000`, "G0 Z5");
    if (z <= -depth) break;
  }
  out.push("M5", "G0 X0 Y0", "M30");
  return out.join("\n") + "\n";
}

function initialState() {
  return {
    connected: false, everConnected: false, hotspot: false, connectId: 0,
    maslow: 0, fluid: "Idle",
    tool: "unknown",              // unknown | checking | loaded | none | removing | removed | waiting
    settingZ: false, zFromStickout: null, zSetCount: 0,  // zSetCount goes up each time Z is set from a stick-out
    spindle: "off",               // off | on | recovering | recoveryFailed
    error: null,                  // { reason } while in Alarm
    job: null,                    // { file, line, total, running }
    gcode: null,                  // the chosen file, read: { file, status: "ready", ...readGcode() } (machine.js has the same)
    lastJob: null,                // { file, result: "success" | "stopped" | "fault", line }
    motion: null,                 // tracing | parking
    result: null,                 // the last setup step's result, e.g. { step: "tension", outcome: "nofit", message }
    restored: false,              // came back from a reboot with belt lengths restored
    machine: "Maslow Mini",       // as the real machine reports it from [ESP800]
    extendDist: 0,                // 0 = extend until the user presses Done extending
    workArea: { w: 1200, h: 1200, spoil: 12.7 },
    stock: 6.35,
    anchorsSaved: true,
    belts: [0, 0, 0, 0],
    // Two positions, as FluidNC keeps them. pos is the machine position (MPos): X 0, Y 0 is where Apply tension
    // leaves the sled, measured from the anchors; Z 0 is where the bit is seated. wco is the work offset the user
    // sets with Set home XY and Set Z, so the work position is pos − wco (FluidNC's WPos = MPos − WCO).
    pos: { x: 0, y: 0, z: 0 },
    wco: { x: 0, y: 0, z: 0 },
    files: FILES,
    log: [],
    incident: null,               // something the user must acknowledge first (flow/incidents.js)
    pending: null                 // the question the simulator is asking the tester
  };
}

export function createSimulator() {
  let s = initialState();
  const listeners = new Set();
  const emit = () => listeners.forEach(fn => fn(s));
  const set = (patch) => { s = { ...s, ...patch }; emit(); };
  const log = (...lines) => { s = { ...s, log: [...s.log, ...lines.map(l => `${time()} ${l}`)].slice(-200) }; };
  // Ask the tester what the device reports. Each option is [label, apply, tone].
  const ask = (title, detail, options) => set({ pending: { title, detail, options: options.map(([label, apply, tone]) => ({ label, apply, tone })) } });
  const state = (n) => { log(`[MSG:INFO: Current state: ${n}]`); return n; };

  function connectQuestion(afterReboot) {
    ask(afterReboot ? "The board restarted. What does it report?" : "The page connected. What state is the device in?",
      "Pick what the firmware reports. With a real device this comes from $GSTATE and $MINFO.",
      [
        ...(afterReboot ? [] : [["First boot: page opened through the hotspot", () => set({ connected: true, everConnected: true, hotspot: true, pending: null })]]),
        ["No saved belt lengths (Unknown)", () => connected(0, { belts: [0, 0, 0, 0], restored: false })],
        ["Belts retracted", () => connected(2, { belts: [0, 0, 0, 0] })],
        ["Belts extended, lengths restored from the encoders", () => connected(4, { belts: [838.7, 700.1, 717.6, 515.4], restored: true })],
        ...(afterReboot ? [] : [
          ["Still tensioned: Ready to cut (a page reload, no reboot)", () => connected(7, { belts: [838.7, 700.1, 717.6, 515.4], tool: "unknown", pos: { x: 0, y: 0, z: 0 } })],
          ["Emergency stop already latched (Alarm)", () => connected(4, { fluid: "Alarm", error: { reason: "Position error over 15 mm on the top right belt" } })]
        ])
      ]);
  }
  function connected(maslow, extra = {}) {
    log("Connected", `[MSG:INFO: Current state: ${maslow}]`);
    set({ connected: true, everConnected: true, hotspot: false, maslow, fluid: "Idle", tool: "unknown", job: null, motion: null,
      result: null, spindle: "off", error: null, pending: null, connectId: s.connectId + 1, ...extra });
  }

  const actions = {
    ackIncident() { set({ incident: null }); },
    connect() { connectQuestion(false); },
    joinNetwork() { set({ hotspot: false }); connectQuestion(false); },

    retract() {
      log("[MSG:INFO: Retracting all belts]");
      set({ maslow: state(1), fluid: "Home", result: null, restored: false, lastJob: null });
      ask("Retracting belts", "Each belt winds in until it meets resistance. About 16 s on the machine.",
        [["Retract finished", () => { log("[MSG:INFO: Belts pulled tight with offsets …]"); set({ maslow: state(2), fluid: "Idle", belts: [0, 0, 0, 0], pending: null }); }]]);
    },
    extend() {
      log("[MSG:INFO: Extending all belts]");
      set({ maslow: state(3), fluid: "Home", result: null, lastJob: null, pending: null });
    },
    doneExtending() {
      log("(planned) Done extending");
      set({ maslow: state(4), fluid: "Idle", belts: [790, 790, 790, 790], result: null });
    },
    tension() {
      set({ maslow: state(5), fluid: "Home", result: null, restored: false, lastJob: null });
      ask("Applying tension", "Takes up the slack, then checks the position against the saved anchors. About 30 s on the machine.", [
        ["Position confirmed", () => { log("[MSG:INFO: Center point deviation within 12.000mm, your coordinate system is accurate]");
          set({ maslow: state(7), fluid: "Idle", tool: "unknown", pos: { ...s.pos, x: 0, y: 0 }, pending: null }); }],
        ["Belt lengths don't fit the anchors", () => { log("[MSG:ERR: Apply Tension failed: the measured belt lengths do not fit the anchor locations, maybe try running Find Anchors again?]");
          set({ maslow: state(4), fluid: "Idle", result: { step: "tension", outcome: "nofit" }, pending: null }); }, "bad"],
        ["A belt wound in more than 300 mm", () => { log("[MSG:WARN: Maslow Apply Tension retraction warning: Belt Top Right retracted 300.140mm while applying tension (limit 300.000mm). A belt may not be anchored.]");
          set({ maslow: state(4), fluid: "Idle", result: { step: "tension", outcome: "notattached" }, pending: null }); }, "bad"]
      ]);
    },
    release() {
      set({ maslow: state(8), fluid: "Home" });
      ask("Releasing tension", "Lets about 10 mm of each belt out. About 1.3 s on the machine.",
        [["Tension released", () => set({ maslow: state(4), fluid: "Idle", result: { step: "release", outcome: "ok" }, pending: null })]]);
    },
    findAnchors() {
      log("[MSG:INFO: Calibration grid: 5x5 …]");
      set({ maslow: state(6), fluid: "Home", result: null, lastJob: null });
      ask("Finding anchor points", "Measures 33 points. About 11 minutes on the machine.", [
        ["Anchors found", () => { log("[MSG:INFO: Find Anchors fit: rms=0.441mm …]", "[MSG:INFO: Find Anchors complete]");
          set({ maslow: state(7), fluid: "Idle", anchorsSaved: true, tool: "unknown", pos: { ...s.pos, x: 0, y: 0 }, pending: null }); }],
        ["Couldn't find the anchors", () => { log("[MSG:ERR: Find Anchors recompute failed]");
          set({ maslow: state(4), fluid: "Idle", result: { step: "anchors", outcome: "failed" }, pending: null }); }, "bad"],
        ["The board reset in the middle (issue 12)", () => { log("Connection reset by the board");
          set({ connected: false, pending: null }); setTimeout(() => connected(0, { belts: [0, 0, 0, 0] }), 1500); }, "bad"]
      ]);
    },

    toolCheck() {
      log("[MSG:INFO: SpindleBoard: Z homing: raising to find the top-of-travel beam]");
      set({ tool: "checking" });
      ask("Checking for a tool", "The spindle rises through the top-of-travel beam. Under a second with a bit loaded.", [
        ["Beam broken: a bit is loaded", () => { log("[MSG:INFO: SpindleBoard: Z homed: top-of-travel beam interrupted, tool loaded]", "[MSG:INFO: SpindleBoard: spindle re-homed Z - reset XY Z position to 0]");
          set({ tool: "loaded", pos: { ...s.pos, z: 0 }, pending: null }); }],
        ["No beam: no bit loaded", () => { log("[MSG:WARN: SpindleBoard: Z homing: no beam … - no tool loaded]"); set({ tool: "none", pending: null }); }]
      ]);
    },
    removeTool() {
      log("[MSG:INFO: SpindleBoard: Tool removal: raising Z to eject any loaded tool]");
      set({ tool: "removing" });
      ask("Removing the tool", "Z rises until the bit has passed the beam and it's clear for 500 ms. About 14 s.",
        [["Bit out: beam clear for 500 ms", () => { log("[MSG:INFO: SpindleBoard: Tool removed: beam clear for 500ms]"); set({ tool: "removed", pending: null }); }]]);
    },
    watchForTool() {
      set({ tool: "waiting" });
      ask("Waiting for the bit", "The board watches the beam. When an inserted bit breaks it, the board lowers Z until the beam is clear for 400 ms.",
        [["Bit inserted and seated", () => { log("[MSG:INFO: SpindleBoard: Tool loaded: beam clear for 400ms]", "[MSG:INFO: SpindleBoard: spindle re-homed Z - reset XY Z position to 0]");
          set({ tool: "loaded", pos: { ...s.pos, z: 0 }, pending: null }); }]]);
    },
    setZFromStickout({ stickout }) {
      log(`(planned) Set Z from a ${stickout} mm stick-out`);
      set({ settingZ: true });
      ask("Setting Z", "Planned firmware command: set Z from the measured stick-out.",
        // Where work Z zero lands below the seated position depends on the machine; the simulator uses 40 mm − stick-out
        [["Z set", () => set({ settingZ: false, zFromStickout: stickout, zSetCount: s.zSetCount + 1,
          wco: { ...s.wco, z: +(s.pos.z - (40 - stickout)).toFixed(3) }, pending: null })]]);
    },

    setExtendDist({ mm }) { log(`$/Maslow_Extend_Dist=${mm}`, "$CO"); set({ extendDist: mm }); },
    setSpoilboard({ mm }) { log(`$/Maslow_spoilboardThickness=${mm}`, "$CO"); set({ workArea: { ...s.workArea, spoil: mm } }); },
    setStock({ stock }) { log(`$/Maslow_workThickness=${stock}`); set({ stock }); },

    run({ file }) {
      log(`$SD/Run=/${file}`);
      set({ fluid: "Run", spindle: "on", job: { file, line: 0, total: 3260, running: true }, lastJob: null });
      jobQuestion();
    },
    continueJob() {
      const lj = s.lastJob;
      log(`(planned) Continue ${lj?.file} from line ${lj?.line}`);
      set({ fluid: "Run", spindle: "on", job: { file: lj.file, line: lj.line, total: 3260, running: true }, lastJob: null });
      jobQuestion();
    },
    trace() {
      set({ motion: "tracing", fluid: "Run" });
      ask("Tracing the boundary", "The sled moves around the job's edge with Z raised.",
        [["Trace finished", () => set({ motion: null, fluid: "Idle", pending: null })]]);
    },
    park() {
      set({ motion: "parking", fluid: "Run" });
      ask("Parking", "The sled moves to the park position.", [["Parked", () => set({ motion: null, fluid: "Idle", pending: null })]]);
    },

    stop() {
      log("$STOP");
      const m = s.maslow;
      if (s.job) { set({ fluid: "Idle", spindle: "off", lastJob: { file: s.job.file, result: "stopped", line: 1240 }, job: null, pending: null }); return; }
      if (s.motion) { set({ motion: null, fluid: "Idle", pending: null }); return; }
      if (s.tool === "checking" || s.tool === "removing") { set({ tool: "unknown", pending: null }); return; }
      if (s.settingZ) { set({ settingZ: false, pending: null }); return; }
      if (m === 1) { set({ maslow: state(0), fluid: "Idle", belts: [0, 0, 0, 0], pending: null }); return; }        // a stopped retract loses the lengths
      if (m === 3) { set({ maslow: state(4), fluid: "Idle", result: { step: "extend", outcome: "stopped" }, pending: null }); return; }
      if (m === 5 || m === 6 || m === 8) { set({ maslow: state(4), fluid: "Idle", result: { step: m === 6 ? "anchors" : "tension", outcome: "stopped" }, pending: null }); return; }
      set({ pending: null });
    },
    clearError() {
      log("$X");
      set({ fluid: "Idle", error: null, maslow: state(4), result: { step: "error", outcome: "cleared" } });
    },
    rehomeZ() {
      log("$Spindle/Home");
      ask("Re-homing Z", "Z rises to the top of travel.", [["Z homed", () => set({ spindle: "off", tool: "unknown", pos: { ...s.pos, z: 0 }, pending: null })]]);
    },
    reboot() {
      log("[ESP444]RESTART");
      set({ connected: false, pending: null });
      setTimeout(() => connectQuestion(true), 1200);
    },
    jog({ dx = 0, dy = 0, dz = 0 }) { set({ pos: { x: +(s.pos.x + dx).toFixed(3), y: +(s.pos.y + dy).toFixed(3), z: +(s.pos.z + dz).toFixed(3) } }); },
    // Setting a home moves the work zero, not the machine: the machine position stays as it was
    setHomeXY() { log("G10 L20 P0 X0 Y0"); set({ wco: { ...s.wco, x: s.pos.x, y: s.pos.y } }); },
    setZ() { log("G10 L20 P0 Z0"); set({ wco: { ...s.wco, z: s.pos.z } }); },
    // The demo files aren't real, so each reads as a generated sample of its size
    chooseFile({ file }) {
      const f = s.files.find(x => x.name === file);
      set({ gcode: f ? { file, status: "ready", ...readGcode(sampleGcode(f)) } : null });
    },
    uploadFile() { const n = `upload-${s.files.length + 1}.nc`; set({ files: [...s.files, { name: n, size: "40 KB", w: 200, h: 150, minutes: 6, depth: 2 }] }); return n; }
  };

  function jobQuestion() {
    ask("Cutting", "The job runs. With a real device, progress comes from the status report.", [
      ["Job finished", () => { log("SD: job sent", "Job finished");
        set({ fluid: "Run", spindle: "off", lastJob: { file: s.job.file, result: "success" }, job: null, motion: "parking", pending: null });
        ask("Parking", "A finished job parks by itself.", [["Parked", () => set({ motion: null, fluid: "Idle", pending: null })]]); }],
      ["Spindle fault", () => { log("[MSG:WARN: SpindleBoard: driver over-current (nFAULT retries) - spindle stopped]", "[MSG:INFO: SpindleBoard: machine stopped after the spindle fault - ending the job ($STOP)]");
        set({ fluid: "Idle", spindle: "recovering", lastJob: { file: s.job.file, result: "fault", line: 1240 }, job: null });
        ask("Spindle fault: recovering", "The job ends, Z lifts and re-homes by itself. About 8 s on the machine.", [
          ["Recovery finished", () => { log("[MSG:INFO: SpindleBoard: Z re-homed after the spindle fault; work Z zero restored - ready to cut]"); set({ spindle: "off", tool: "unknown", pending: null, incident: spindleFault(s.lastJob?.line) }); }],
          ["Recovery failed", () => set({ spindle: "recoveryFailed", pending: null }), "bad"]
        ]); }, "bad"],
      ["Emergency stop (position error)", () => { log("[MSG:ERR: Emergency stop! Stopping all motors]");
        set({ fluid: "Alarm", spindle: "off", error: { reason: "Position error over 15 mm on the top right belt" }, job: null, pending: null }); }, "bad"]
    ]);
  }

  // Events a tester can cause at any time, from the simulator panel
  const events = {
    disconnect() { log("Connection lost"); set({ connected: false, pending: null }); },
    reconnect() { connectQuestion(false); },
    emergencyStop() { log("[MSG:ERR: Emergency stop! Stopping all motors]");
      set({ fluid: "Alarm", error: { reason: "Emergency button pressed" }, job: null, motion: null, spindle: "off", pending: null }); },
    boardReset() {
      const before = s.maslow;
      log("Connection reset by the board"); set({ connected: false, pending: null });
      setTimeout(() => connected(0, { belts: [0, 0, 0, 0], incident: [1, 3, 5, 6, 7, 8, 10].includes(before) ? restarted(before, 0) : null }), 1500);
    }
  };

  // Belt motor currents, as the real machine's MC field reports them (raw ADC, 0-4095). Made up, so the Motors
  // view has something to show: a light hold when idle and tensioned, more while the belts or a job move.
  const BUSY = [1, 3, 5, 6, 8];
  setInterval(() => {
    if (!s.connected) return;
    const moving = BUSY.includes(s.maslow) || s.job || s.motion;
    const base = moving ? 900 : s.maslow === 7 ? 60 : 0;
    const jitter = () => (moving ? Math.round((Math.random() - 0.5) * 900) : Math.round(Math.random() * 10));
    const mc = { tl: base + jitter(), tr: base + jitter(), bl: Math.round(base * 0.7) + jitter(), br: Math.round(base * 0.7) + jitter() };
    for (const k of Object.keys(mc)) mc[k] = Math.max(0, Math.min(4095, mc[k]));
    set({ motorCurrent: mc });
  }, 500);

  return {
    getState: () => s,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    send(action, payload = {}) {
      const fn = actions[action];
      if (!fn) { console.warn("Unknown device action", action); return; }
      const c = COMMANDS[action];
      if (c) console.debug(`[device] ${action} → ${c.cmd || "(no command)"} (${c.status})`);
      return fn(payload);
    },
    answer(i) { const p = s.pending; if (p) p.options[i].apply(); },
    event(name) { events[name](); },
    isSimulated: true
  };
}
