import { COMMANDS } from "./commands.js";
import { restarted, spindleFault } from "../flow/incidents.js";
import { readGcode } from "../gcode/reader.js";
import { MACHINE_SETTINGS, defaultSettings, settingError } from "./settings.js";
import { parseSettingsList, valueError, espSetCommand, withValues } from "./settingslist.js";
import { sampleValues, sampleAnswer } from "./settingslist.sample.js";
import { NET_IDLE, NET_SETTINGS, netChanges } from "./network.js";
import { FLASH_IDLE, MAX_EDIT, flashActions } from "./flashfiles.js";
import { createFlashFs } from "./flashfiles.sample.js";

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

// What the simulated board reports for its network ([ESP400], [ESP420]) and sees when it scans ([ESP410])
const SIM_NET = { ssid: "Workshop", mode: "STA>AP", apSsid: "maslow", hostname: "maslow" };
const SIM_APS = [
  { ssid: "Workshop", signal: 82, locked: true }, { ssid: "Workshop-5G", signal: 64, locked: true },
  { ssid: "Makerspace Guest", signal: 41, locked: false }, { ssid: "NETGEAR-2F", signal: 18, locked: true }
];

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
    // As the real machine's report (TS): unknown | checking | loaded | none | loading | removing | needsHoming
    tool: "unknown",
    settingZ: false, zFromStickout: null, zSetCount: 0,  // zSetCount goes up each time Z is set from a stick-out
    spindle: "off",               // off | on | recovering | recoveryFailed
    motorTest: null,              // the last belt motor test: { passed: [arm], failed: [{ arm, problem }] }
    error: null,                  // { reason } while in Alarm
    job: null,                    // { file, line, total, running }
    gcode: null,                  // the chosen file, read: { file, status: "ready", ...readGcode() } (machine.js has the same)
    lastJob: null,                // { file, result: "success" | "stopped", line }
    motion: null,                 // tracing | parking
    result: null,                 // the last setup step's result, e.g. { step: "tension", outcome: "nofit", message }
    restored: false,              // came back from a reboot with belt lengths restored
    machine: "Maslow Mini",       // as the real machine reports it from [ESP800]
    extendDist: 0,                // 0 = extend until the user presses Done extending
    workArea: { spoil: 12.7 },
    settings: defaultSettings(),  // work area (0 = automatic), park, scale, belt limits (device/settings.js)
    settingsList: { status: "idle" },  // Settings › Advanced's raw lists: { status, items, detail, save } (device/settingslist.js)
    // the anchors as kinematics/MaslowKinematics has them (machine.js has the same shape): a 3000 x 2100 mm frame
    frame: { tl: [0, 2100], tr: [3000, 2100], bl: [0, 0], br: [3000, 0], z: [100, 56, 34, 78], arm: 153.4, vertical: false },
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
    net: NET_IDLE,                // the network settings, as the machine adapter has them (device/network.js)
    netSaved: { ...SIM_NET },     // what the board would use after a restart
    flash: FLASH_IDLE,            // the board's own files (device/flashfiles.js), read when Settings › Advanced › Files opens
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

  // Settings the command box can read ($/name) and write ($/name=value), seeded from the state
  const settings = {
    Maslow_Park_X: "0", Maslow_Park_Y: "0", Maslow_Park_Z: "10"
  };
  const setting = (name) => ({
    Maslow_Extend_Dist: s.extendDist, Maslow_Work_Area_X: s.workArea.w, Maslow_Work_Area_Y: s.workArea.h,
    Maslow_spoilboardThickness: s.workArea.spoil, Maslow_workThickness: s.stock
  })[name] ?? settings[name];
  // Every setting [ESP400] lists (settingslist.sample.js), path → text. The ones Settings › Frame and belts edits come
  // from the state, so both sections always agree.
  const raw = sampleValues();
  const r3 = (v) => Number(v).toFixed(3);
  function rawValues() {
    for (const [k, d] of Object.entries(MACHINE_SETTINGS)) raw[d.path] = d.integer ? String(s.settings[k]) : r3(s.settings[k]);
    Object.assign(raw, { Maslow_spoilboardThickness: r3(s.workArea.spoil), Maslow_workThickness: r3(s.stock),
      Maslow_Extend_Dist: r3(s.extendDist), Maslow_vertical: s.frame.vertical ? "1" : "0" });
    return raw;
  }
  // A saved raw setting that the state also holds, put into the state
  function mirrored(changes) {
    const patch = {}, settings = { ...s.settings };
    for (const { path, value } of changes) {
      const k = Object.keys(MACHINE_SETTINGS).find((key) => MACHINE_SETTINGS[key].path === path);
      if (k) settings[k] = Number(value);
      if (path === "Maslow_spoilboardThickness") patch.workArea = { ...s.workArea, spoil: Number(value) };
      if (path === "Maslow_workThickness") patch.stock = Number(value);
      if (path === "Maslow_Extend_Dist") patch.extendDist = Number(value);
      if (path === "Maslow_vertical") patch.frame = { ...s.frame, vertical: value === "1" };
    }
    return { ...patch, settings };
  }

  // The lines the firmware would send back for a typed line, ending in ok or error:N
  function simReply(text) {
    const [cmd, value] = text.split("=");
    const upper = cmd.toUpperCase();
    if (text === "?") return [`<${s.fluid}|MPos:${s.pos.x.toFixed(3)},${s.pos.y.toFixed(3)},${s.pos.z.toFixed(3)}|FS:0,0|MS:${s.maslow}>`];
    if (/^\[ESP\d+\]/i.test(text)) return [`(simulator) ${text.slice(0, 8)} is answered over HTTP on the machine`];
    if (upper === "$I") return ["[VER:3.9 FluidNC Maslow (simulator):]", "[OPT:PHS]", "ok"];
    if (upper === "$SS") return ["[MSG:INFO: (simulator) startup log: FluidNC Maslow, board Maslow Mini]", "ok"];
    if (upper === "$MINFO") return [`MINFO: ${JSON.stringify({ state: s.maslow, tl: s.belts[0], tr: s.belts[1], bl: s.belts[2], br: s.belts[3] })}`, "ok"];
    if (upper === "$GSTATE") return [`[MSG:INFO: Current state: ${s.maslow}]`, "ok"];
    if (cmd.startsWith("$/")) {
      const name = cmd.slice(2);
      if (value !== undefined) { settings[name] = value; return ["ok"]; }
      const v = setting(name);
      return v === undefined ? ["error:3"] : [`$/${name}=${v}`, "ok"];
    }
    if (/^(\$|[GMgm]\d)/.test(text)) return ["ok"];
    return ["error:1"];
  }

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

  // The board's flash files: an in-memory filesystem that answers like the firmware's /files (flashfiles.sample.js).
  // A short wait on each answer, so "Uploading…" and "Saving…" show as they would on the board.
  const flashFs = createFlashFs();
  const later = (v) => new Promise((r) => setTimeout(() => r(v), 150));
  const flash = flashActions({
    get: () => s.flash,
    set: (patch) => set({ flash: { ...s.flash, ...patch } }),
    log: (line) => log(line),
    lock: () => (!s.connected ? "Waiting for the device to answer." : s.job ? "Not while a job runs." : s.motion ? "Not while the machine moves." : null),
    io: {
      list: (dir) => later(flashFs.request(new URLSearchParams({ path: dir }))),
      act: (query) => later(flashFs.request(query)),
      async upload(path, content) {
        // An uploaded file is kept in this page: as text when the editor could open it, otherwise as it is
        if (typeof content !== "string" && content.size <= MAX_EDIT && /\.(ya?ml|json|txt|cfg|ini|md|bak)$/i.test(path)) content = await content.text();
        return later(flashFs.write(path, typeof content === "string" ? content : { size: content.size, blob: content }));
      },
      read: async (path) => { const c = flashFs.read(path); return later(c == null || typeof c === "string" ? c : null); },
      download(path, name) {
        const c = flashFs.read(path);
        if (c == null) throw new Error("the simulator only has this file's size, not its contents");
        const url = URL.createObjectURL(typeof c === "string" ? new Blob([c], { type: "text/plain" }) : c);
        Object.assign(document.createElement("a"), { href: url, download: name }).click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
      }
    }
  });

  const actions = {
    ...flash,
    ackIncident() { set({ incident: null }); },
    connect() { connectQuestion(false); },

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
        [["Bit out: beam clear for 500 ms", () => { log("[MSG:INFO: SpindleBoard: Tool removed: beam clear for 500ms]"); set({ tool: "none", pending: null }); }]]);
    },
    // The tool stays "none" (as the machine reports it) until a bit breaks the beam
    watchForTool() {
      ask("Waiting for the bit", "The board watches the beam. When an inserted bit breaks it, the board lowers Z until the beam is clear for 400 ms.",
        [["Bit inserted: it breaks the beam", () => { log("[MSG:INFO: SpindleBoard: Tool loading: beam interrupted, lowering Z until the beam clears]");
          set({ tool: "loading" });
          ask("Loading the tool", "Z lowers until the beam has been clear for 400 ms. About 13 s on the machine.",
            [["Bit seated", () => { log("[MSG:INFO: SpindleBoard: Tool loaded: beam clear for 400ms]", "[MSG:INFO: SpindleBoard: spindle re-homed Z - reset XY Z position to 0]");
              set({ tool: "loaded", pos: { ...s.pos, z: 0 }, pending: null }); }]]); }]]);
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
    // Several machine settings at once, as the machine adapter sends them: the changed ones, then $CO
    setSettings({ values = {}, label = "Settings" }) {
      const changed = Object.entries(values).filter(([k, v]) => s.settings[k] !== v);
      const bad = changed.map(([k, v]) => [k, settingError(k, v)]).find(([, e]) => e);
      if (bad) { log(`Not sent: ${label} (${bad[0]}: ${bad[1]})`); emit(); return; }
      if (!changed.length) return;
      log(...changed.map(([k, v]) => `$/${MACHINE_SETTINGS[k].path}=${v}`), "$CO");
      set({ settings: { ...s.settings, ...Object.fromEntries(changed) } });
    },

    // Settings › Advanced: [ESP400], and saving the changed ones as the machine adapter does
    readSettingsList() {
      log("[ESP400]");
      set({ settingsList: { status: "ready", items: parseSettingsList(sampleAnswer(rawValues())) } });
    },
    saveSettingsList({ changes = [] }) {
      const items = s.settingsList.items || [];
      const byPath = new Map(items.map((i) => [i.path, i]));
      const bad = changes.map((c) => [c.path, byPath.get(c.path) ? valueError(byPath.get(c.path), c.value) : "Unknown setting"]).find(([, e]) => e);
      if (bad) { log(`Not sent: settings (${bad[0]}: ${bad[1]})`); set({ settingsList: { ...s.settingsList, save: { state: "failed", detail: `${bad[0]}: ${bad[1]}` } } }); return; }
      if (!changes.length) return;
      for (const c of changes) {
        const i = byPath.get(c.path);
        log(i.store === "flash" ? espSetCommand(i, i.secret ? "********" : c.value) : `$/${c.path}=${c.value}`);
        raw[c.path] = c.value;
      }
      if (changes.some((c) => c.store === "config")) log("$CO");
      set({ ...mirrored(changes), settingsList: { status: "ready", items: withValues(items, changes),
        save: { state: "saved", restart: changes.some((c) => byPath.get(c.path).restart) } } });
    },

    run({ file }) {
      log(`$SD/Run=/${file}`);
      set({ fluid: "Run", spindle: "on", job: { file, line: 0, total: 3260, running: true }, lastJob: null });
      jobQuestion();
    },
    discardStopped() { log("$SD/Discard"); set({ lastJob: null }); },
    continueJob({ line } = {}) {
      const lj = s.lastJob, from = line ?? lj?.line;
      log(`$SD/Resume=${lj?.file},${from}`);
      set({ fluid: "Run", spindle: "on", job: { file: lj.file, line: from, total: 3260, running: true }, lastJob: null });
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
      set({ spindle: "off", pending: null });   // $STOP turns the spindle off
    },
    clearError() {
      log("$X");
      set({ fluid: "Idle", error: null, maslow: state(4), result: { step: "error", outcome: "cleared" } });
    },
    // Spindle on / off from the jog pad (M3 S12000 / M5); the real machine reports it in the status report's FS field
    spindleOn() { log("M3 S12000"); set({ spindle: "on" }); },
    spindleOff() { log("M5"); set({ spindle: "off" }); },
    // The belt motor test ($TEST): each motor pays its belt out for at most 100 ms, then encoder and magnet are checked
    motorTest() {
      log("$TEST");
      set({ motorTest: null });
      const ARMS = ["Top Left", "Top Right", "Bottom Left", "Bottom Right"];
      const done = (failed) => {
        const passed = ARMS.filter(a => !failed.some(f => f.arm === a));
        log(...passed.map(a => `[MSG:INFO: All tests passed on ${a}]`), ...failed.map(f => `[MSG:WARN: ${f.problem} on ${f.arm}]`));
        set({ motorTest: { passed, failed }, pending: null,
          ...(failed.some(f => f.problem !== "Magnet not detected") ? { fluid: "Alarm", error: { reason: `${failed[0].problem} on ${failed[0].arm}` } } : {}) });
      };
      ask("Testing the belt motors", "Each motor runs outward for at most 100 ms, then its encoder and magnet are checked. Under a second.", [
        ["All four arms pass", () => done([])],
        ["Encoder missing on Top Right", () => done([{ arm: "Top Right", problem: "Encoder not found" }]), "bad"],
        ["Magnet not detected on Bottom Left", () => done([{ arm: "Bottom Left", problem: "Magnet not detected" }]), "bad"]
      ]);
    },
    // As the firmware: homing zeroes machine Z at the top of travel and the work Z offset with it
    rehomeZ() {
      log("$Spindle/Home");
      set({ spindle: "off", tool: "checking" });
      ask("Re-homing Z", "Z rises to the top of travel, and the beam shows whether a bit is loaded.", [
        ["Z homed: a bit is loaded", () => set({ tool: "loaded", pos: { ...s.pos, z: 0 }, wco: { ...s.wco, z: 0 }, pending: null })],
        ["Z homed: no bit", () => set({ tool: "none", pos: { ...s.pos, z: 0 }, wco: { ...s.wco, z: 0 }, pending: null })]]);
    },
    // The network settings, over HTTP on the machine. Reading and scanning answer after a moment, as the board does.
    readNetwork() {
      log("[ESP400]", "[ESP420]");
      set({ net: { ...s.net, read: "reading" } });
      const n = s.netSaved;
      setTimeout(() => set({ net: { ...s.net, read: "ready", settings: { ...n },
        // In hotspot mode (first boot) the board couldn't join its network, so it runs the hotspot (STA>AP)
        status: n.mode === "AP" ? { sta: null, ap: { ssid: n.apSsid, ip: "192.168.0.1" } }
          : s.hotspot ? { sta: { ssid: n.ssid, connected: false, ip: "0.0.0.0" }, ap: { ssid: n.apSsid, ip: "192.168.0.1" } }
          : { sta: { ssid: n.ssid, connected: true, ip: "192.168.1.42" }, ap: null } } }), 400);
    },
    scanNetworks() {
      log("[ESP410]");
      set({ net: { ...s.net, scan: { state: "scanning", list: s.net.scan?.list || [] } } });
      setTimeout(() => set({ net: { ...s.net, scan: { state: "done", list: SIM_APS } } }), 1500);
    },
    saveNetwork(form) {
      const current = s.net.settings || s.netSaved;
      const { changes, error } = netChanges(form, current);
      if (error) { log(`Not sent: network settings (${error})`); set({ net: { ...s.net, save: { state: "failed", detail: error } } }); return; }
      if (!changes.length) return;
      // Passwords aren't logged, here or on the machine
      log(...changes.map(([k, v]) => `[ESP401]P=${NET_SETTINGS[k].name} V=${NET_SETTINGS[k].secret ? "…" : v}`));
      const { password, apPassword, ...shown } = Object.fromEntries(changes);
      set({ netSaved: { ...s.netSaved, ...shown }, hotspot: s.hotspot && shown.mode !== "AP",
        net: { ...s.net, settings: { ...current, ...shown }, save: { state: "saved" }, restartNeeded: true } });
    },
    reboot() {
      log("[ESP444]RESTART");
      set({ connected: false, pending: null, net: { ...s.net, restartNeeded: false, save: null } });
      setTimeout(() => connectQuestion(true), 1200);
    },
    jog({ dx = 0, dy = 0, dz = 0 }) { set({ pos: { x: +(s.pos.x + dx).toFixed(3), y: +(s.pos.y + dy).toFixed(3), z: +(s.pos.z + dz).toFixed(3) } }); },
    // Setting a home moves the work zero, not the machine: the machine position stays as it was
    // The new home in machine coordinates; without one, here
    setHomeXY({ x = s.pos.x, y = s.pos.y } = {}) { log(`setWorkZero x=${x} y=${y}`); set({ wco: { ...s.wco, x, y } }); },
    resetHome() { log("setWorkZero x=0 y=0"); set({ wco: { ...s.wco, x: 0, y: 0 } }); },
    setZ({ z = s.pos.z } = {}) { log(`setWorkZero z=${z}`); set({ wco: { ...s.wco, z } }); },
    // The Set Z helper: work Z 0 at machine Z z, then Z lowered there (machine.js does it as two commands)
    lowerToWork({ z }) { log(`setWorkZero z=${z}`, `G53 G1 Z${z}`); set({ wco: { ...s.wco, z }, pos: { ...s.pos, z } }); },
    // The demo files aren't real, so each reads as a generated sample of its size; an uploaded file is read for real
    chooseFile({ file }) {
      const f = s.files.find(x => x.name === file);
      set({ gcode: f ? { file, status: "ready", ...(f.text != null ? readGcode(f.text) : readGcode(sampleGcode(f))) } : null });
    },
    deleteFile({ name }) { log(`Deleted ${name}`); set({ files: s.files.filter(f => f.name !== name) }); },
    // A line typed in the command box: echo it and answer roughly as the firmware would. Nothing moves.
    command({ line }) {
      const text = String(line || "").trim();
      if (!text) return;
      log(`> ${text}`);
      if (!s.connected) { log(`Not sent: ${text} (not connected)`); emit(); return; }
      log(...simReply(text));
      emit();
    },
    // Upload: pick a real G-code file from this computer, as on the machine. It stays in this page (not saved anywhere),
    // so its preview, size, placement and fit check are the file's own. Outside a browser (tests) there's nothing to
    // pick, so a made-up file is added as before.
    uploadFile() {
      const fake = () => { const n = `upload-${s.files.length + 1}.nc`; set({ files: [...s.files, { name: n, size: "40 KB", w: 200, h: 150, minutes: 6, depth: 2 }] }); return n; };
      if (typeof document === "undefined") return fake();
      return new Promise((resolve) => {
        const input = Object.assign(document.createElement("input"), { type: "file", accept: ".nc,.gcode,.gc,.ngc,.tap,.txt" });
        input.onchange = async () => {
          const file = input.files[0];
          if (!file) return resolve(null);
          const text = await file.text();
          const entry = { name: file.name, size: `${Math.max(1, Math.round(file.size / 1024))} KB`, text };
          log(`Uploaded ${file.name} (kept in this page only)`);
          set({ files: [...s.files.filter(f => f.name !== file.name), entry] });
          if (s.gcode?.file === file.name) actions.chooseFile({ file: file.name });   // the same name again: show the new one
          resolve(file.name);
        };
        input.click();
      });
    }
  };

  function jobQuestion() {
    ask("Cutting", "The job runs. With a real device, progress comes from the status report.", [
      ["Job finished", () => { log("SD: job sent", "Job finished");
        set({ fluid: "Run", spindle: "off", lastJob: { file: s.job.file, result: "success" }, job: null, motion: "parking", pending: null });
        ask("Parking", "A finished job parks by itself.", [["Parked", () => set({ motion: null, fluid: "Idle", pending: null })]]); }],
      ["Spindle fault", () => { log("[MSG:WARN: SpindleBoard: driver over-current (nFAULT retries) - spindle stopped]", "[MSG:INFO: SpindleBoard: machine stopped after the spindle fault - ending the job ($STOP)]");
        // As the machine reports it: TS 7 while it recovers, and the job left STOPPED
        set({ fluid: "Idle", spindle: "recovering", tool: "checking", lastJob: { file: s.job.file, result: "stopped", line: 1240 }, job: null });
        ask("Spindle fault: recovering", "The job ends, Z lifts and re-homes by itself. About 8 s on the machine.", [
          ["Recovery finished", () => { log("[MSG:INFO: SpindleBoard: Z re-homed after the spindle fault; work Z zero restored - ready to cut]"); set({ spindle: "off", tool: "loaded", pending: null, incident: spindleFault(s.lastJob?.line) }); }],
          // The firmware gave up before re-homing: Z needs homing (TS 1)
          ["Recovery failed", () => set({ spindle: "recoveryFailed", tool: "needsHoming", pending: null, incident: spindleFault(s.lastJob?.line, false) }), "bad"]
        ]); }, "bad"],
      ["Emergency stop (position error)", () => { log("[MSG:ERR: Emergency stop! Stopping all motors]");
        set({ fluid: "Alarm", spindle: "off", error: { reason: "Position error over 15 mm on the top right belt" }, job: null, pending: null }); }, "bad"]
    ]);
  }

  // Events a tester can cause at any time, from the simulator panel
  const events = {
    disconnect() { log("Connection lost"); set({ connected: false, pending: null }); },
    // Maslow_vertical, as Find anchors saves it after detecting the orientation
    toggleVertical() { set({ frame: { ...s.frame, vertical: !s.frame.vertical } }); },
    reconnect() { connectQuestion(false); },
    emergencyStop() { log("[MSG:ERR: Emergency stop! Stopping all motors]");
      set({ fluid: "Alarm", error: { reason: "Emergency button pressed" }, job: null, motion: null, spindle: "off", pending: null }); },
    boardReset() {
      const before = s.maslow;
      log("Connection reset by the board"); set({ connected: false, pending: null });
      setTimeout(() => connected(0, { belts: [0, 0, 0, 0], incident: [1, 3, 5, 6, 7, 8, 10].includes(before) ? restarted(before, 0) : null }), 1500);
    }
  };

  // Belt motor currents and belt errors, as the real machine's MC (raw ADC, 0-4095) and BE (mm) fields report them.
  // Made up, so the Motors view and the job recorder have something to show: a light hold when idle and tensioned,
  // more while the belts or a job move. During a job the file line advances, the load swings between the top and
  // bottom motors as the sled moves, and now and then one motor spikes (a hard plunge, a snag) with a belt error.
  const BUSY = [1, 3, 5, 6, 8];
  let spike = null;   // { arm, ticks } while one motor spikes
  setInterval(() => {
    if (!s.connected) return;
    const moving = BUSY.includes(s.maslow) || s.job || s.motion;
    let mc, be = [0, 0, 0, 0], job = s.job;
    if (s.job) {
      job = { ...s.job, line: Math.min(s.job.total || Infinity, s.job.line + 1 + Math.floor(Math.random() * 4)) };
      const ph = job.line / 90;   // where the sled is, as a slow swing between the top and bottom of the work
      const up = 0.5 + 0.5 * Math.sin(ph), side = 0.5 + 0.5 * Math.sin(ph * 0.63 + 1);
      if (!spike && Math.random() < 0.004) spike = { arm: Math.floor(Math.random() * 4), ticks: 3 + Math.floor(Math.random() * 6) };
      const load = [up * (1 - side), up * side, (1 - up) * (1 - side), (1 - up) * side].map((x) => 500 + 1100 * x);
      const noise = () => (Math.random() - 0.5) * 220;
      mc = load.map((v, i) => v + noise() + (spike?.arm === i ? 1500 + Math.random() * 600 : 0));
      be = load.map((_, i) => +((Math.random() - 0.5) * 0.08 + (spike?.arm === i ? 0.4 + Math.random() * 0.8 : 0)).toFixed(3));
      if (spike && --spike.ticks <= 0) spike = null;
    } else {
      const base = moving ? 900 : s.maslow === 7 ? 60 : 0;
      const jitter = () => (moving ? (Math.random() - 0.5) * 900 : Math.random() * 10);
      mc = [base, base, base * 0.7, base * 0.7].map((v) => v + jitter());
    }
    const [tl, tr, bl, br] = mc.map((v) => Math.max(0, Math.min(4095, Math.round(v))));
    set({ motorCurrent: { tl, tr, bl, br }, beltError: be, job });
  }, 200);

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
