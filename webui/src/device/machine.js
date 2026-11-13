// The real machine, through Tristan's machine.js and wscommands.js (ESP3D-WEBUI/www/js), used as they are.
// It has the same interface as the simulator, getState(), subscribe(fn) and send(action, payload), and builds
// the same state shape from machine.state, so the screens don't change.
//
// Actions so far: Stop, the belt steps of Setup (retract, extend, apply tension, release tension, find
// anchors), running a file and Trace boundary. Each goes through a machine.js action, and its Operation is kept in state.op so the screens can show
// "starting", then nothing (done) or why it failed. The screen itself still comes from the machine's state, so
// every connection shows the same thing. Jogging, files, jobs and the tool steps come next.

import { restarted, jobFailed, holding } from "../flow/incidents.js";
import { readGcode, boundaryPath } from "../gcode/reader.js";

// What the firmware's close codes mean (WSChannel.h). For these the page shows a notice and doesn't reconnect.
const CLOSE_NOTICES = { 4001: "othertab", 4002: "toomany" };
const RECONNECT_MS = 3000;
const SETTINGS = {
  workW: "Maslow_Work_Area_X", workH: "Maslow_Work_Area_Y",
  spoil: "Maslow_spoilboardThickness", stock: "Maslow_workThickness", extend: "Maslow_Extend_Dist"
};
// The board type [ESP800] reports (firmware BoardId), as the machine's name. The Unified Control Board is the Mini's.
const MACHINES = { maslow41: "Maslow 4.1", unified: "Maslow Mini", unified36v: "Maslow Mini 36 V" };

// UI action → machine.js action, for the actions this page sends so far
const ACTIONS = {
  retract: "retractBelts", extend: "extendBelts", tension: "applyTension",
  release: "releaseTension", findAnchors: "calibrate"
};

// Jog speeds, as the current UI's defaults (prefDefs.js: xy_feedrate, z_feedrate)
const JOG_FEED = { xy: 2500, z: 300 };

const time = () => new Date().toTimeString().slice(0, 8);
const baseName = (path) => String(path || "").split("/").pop();
// The status report names a running file as /sd/<path>; the web server serves the SD card under /SD/
const sdUrl = (path) => `/SD/${String(path).replace(/^\/?sd\//i, "").replace(/^\//, "")}`;
const kb = (bytes) => (bytes == null ? "" : `${Math.max(1, Math.round(bytes / 1024))} KB`);

export function createMachine({ host = location.host } = {}) {
  const listeners = new Set();
  let m = null;            // Tristan's machine object, once loaded
  let ws = null;
  let s = {
    connected: false, everConnected: false, hotspot: false, connectId: 0, closed: null, readOnly: false,
    incident: null,          // something the user must acknowledge before the flow goes on (flow/incidents.js)
    op: null,                // { action, phase: "starting" | "running" | "failed", detail, maslowAt } for this page's last action
    beltsKnown: 0,           // how many belt lengths the machine knows (BL "?" = unknown)
    toolSteps: false,        // the tool check, measure and load aren't connected yet (they need the tool field)
    notYet: {                // actions this page can't send yet, and why; their buttons are disabled with the reason
      setHomeXY: "Needs a machine.js action (G10 L20), to add with Tristan",
      setZ: "Needs a machine.js action (G10 L20), to add with Tristan",
      park: "Needs a machine.js action, to add with Tristan",
      // Extend distance 0 means "until Done extending", which today's firmware doesn't support: it would extend 0 mm
      extendUntilDone: "0 needs the Done extending firmware change. Until then, Extend stops at the distance set here."
    },
    machine: null,           // the machine's name from [ESP800], e.g. "Maslow Mini"; null until known
    extendDist: null,        // Maslow_Extend_Dist, mm
    maslow: null, fluid: "", statusSeen: false,
    tool: "unknown", settingZ: false, zSetCount: 0, spindle: "off", error: null,
    job: null, lastJob: null, motion: null, result: null, restored: false,
    gcode: null,             // the chosen file, read: { file, status: "reading" | "ready" | "failed", detail?, ...readGcode() }
    workArea: { w: 0, h: 0, spoil: 0 }, stock: 0, anchorsSaved: true,
    belts: [0, 0, 0, 0], motorCurrent: null,
    pos: { x: 0, y: 0, z: 0 }, wco: { x: 0, y: 0, z: 0 },
    files: [], log: [], pending: null
  };

  // Files read from the SD card through the G-code reader (gcode/reader.js), each fetched once: url → Promise of
  // readGcode()'s result. Job progress and Trace boundary share them. A failed read is forgotten so it can be retried.
  const readings = {};
  function readFile(path) {
    const url = sdUrl(path);
    if (!readings[url]) {
      readings[url] = fetch(url).then(r => r.ok ? r.text() : Promise.reject(new Error(`HTTP ${r.status}`)))
        .then(readGcode);
      readings[url].catch(() => { delete readings[url]; });
    }
    return readings[url];
  }

  // The file the user has chosen, read for the preview and Trace boundary. The UI sends chooseFile whenever its
  // choice changes; an upload has already put the file in readings, so choosing it needs no fetch.
  let chosen = null;
  function chooseFile(file) {
    chosen = file || null;
    if (!chosen) { set({ gcode: null }); return; }
    set({ gcode: { file, status: "reading" } });
    readFile(file).then(
      (r) => { if (chosen === file) set({ gcode: { file, status: "ready", ...r } }); },
      (e) => {
        if (chosen !== file) return;
        log(`Couldn't read ${file}: ${e.message}`);
        set({ gcode: { file, status: "failed", detail: e.message } });
      });
  }

  // Job progress: the report's SD percent is bytes read, not progress (issue 19), so progress is the running line
  // out of the file's line count. The file is read when its job first shows up (or earlier, for Trace boundary).
  const lines = {};   // file path → line count, or "?" while it's being counted
  function lineCount(path) {
    if (!path) return "?";
    if (lines[path] == null) {
      lines[path] = "?";
      readFile(path)
        .then(r => { lines[path] = r.lineCount; if (s.job) set({ job: { ...s.job, total: lines[path] } }); })
        .catch(e => { log(`Couldn't read ${baseName(path)} to count its lines: ${e.message}`); emit(); });
    }
    return lines[path];
  }

  // Trace boundary: the chosen file's extent as a closed rectangle, then back to the work position, through
  // machine.tracePath as the current UI does (toolpath-displayer.js, traceBoundary). Reading the file can take a
  // moment, so this returns an Operation of its own whose .accepted follows tracePath's. If Stop or another action
  // replaces this one while the file is read, nothing is sent.
  function traceFile(file) {
    const token = s.op;   // run() has just set it
    let accepted;
    const acceptedP = new Promise((resolve, reject) => { accepted = { resolve, reject }; });
    const notReady = (detail) => Object.assign(new Error(detail), { reason: "not-ready", detail });
    const done = (async () => {
      if (!file) throw notReady("Choose a file first");
      let job;
      try {
        job = await readFile(file);
      } catch (e) {
        throw notReady(`Couldn't read ${file}: ${e.message}`);
      }
      if (s.op !== token) throw Object.assign(new Error("stopped"), { reason: "reset" });
      if (!job.extent) throw notReady(`${file} has no cutting moves to trace`);
      const st = m.state;
      const w = st.wpos || (st.mpos && st.wco
        ? { x: st.mpos.x - st.wco.x, y: st.mpos.y - st.wco.y } : null);
      const op = m.tracePath(boundaryPath(job.extent, w));
      op.accepted.then(accepted.resolve, accepted.reject);
      return op;
    })();
    done.catch(accepted.reject);
    acceptedP.catch(() => {});
    return Object.assign(done, { accepted: acceptedP });
  }

  const emit = () => listeners.forEach(fn => fn(s));
  const set = (patch) => { s = { ...s, ...patch }; emit(); };
  const log = (line) => { s = { ...s, log: [...s.log, `${time()} ${line}`].slice(-200) }; };

  // machine.state → the shape the screens read (the simulator's)
  function fromMachine(ms) {
    const b = ms.belts || {};
    const alarm = ms.grbl === "Alarm";
    // A failure message belongs to the state it happened in: once the machine moves on, drop it
    const op = s.op?.phase === "failed" && ms.maslow !== s.op.maslowAt ? null : s.op;
    // Held with no job running and nothing of ours in progress: the user has to clear it with Stop
    let incident = s.incident;
    if (ms.grbl === "Hold" && !ms.job && !op && !incident) incident = holding();
    if (incident?.kind === "holding" && ms.grbl !== "Hold") incident = null;
    return {
      incident, op, beltsKnown: [b.tl, b.tr, b.bl, b.br].filter(v => v != null).length,
      maslow: ms.maslow, fluid: ms.grbl,
      pos: ms.mpos || s.pos, wco: ms.wco || s.wco,
      belts: [b.tl, b.tr, b.bl, b.br].map(v => v ?? 0),
      beltUnknown: [b.tl, b.tr, b.bl, b.br].map(v => v == null),
      motorCurrent: ms.motorCurrent,
      job: ms.job ? { file: baseName(ms.job.file), line: ms.job.line ?? 0, total: lineCount(ms.job.file), running: true } : null,
      lastJob: ms.stopped ? { file: ms.stopped.file, result: "stopped", line: ms.stopped.line } : null,
      error: alarm ? (s.error || { reason: "The machine is in Alarm" }) : null
    };
  }

  // Connected = the socket is open and the machine has been heard recently
  function health() {
    const h = globalThis.wsCommands ? globalThis.wsCommands.health() : "down";
    const connected = h === "ok" && s.statusSeen;
    if (connected !== s.connected) set({ connected, everConnected: s.everConnected || connected });
  }

  async function readSettings() {
    const read = (path) => m.getSetting(path).then(Number).catch(() => null);
    const [w, h, spoil, stock, extend] = await Promise.all(Object.values(SETTINGS).map(read));
    set({ workArea: { w: w ?? 0, h: h ?? 0, spoil: spoil ?? 0 }, stock: stock ?? 0, extendDist: extend });
  }

  // Which machine this is: [ESP800] reports the board ("board:unified"). Over HTTP, like the current UI.
  async function readMachine() {
    try {
      const r = await fetch(`/command?plain=${encodeURIComponent("[ESP800]")}`);
      const tag = (await r.text()).match(/board:\s*([a-z0-9]+)/i)?.[1];
      set({ machine: MACHINES[tag] || (tag ? `Unknown board (${tag})` : "Unknown machine") });
    } catch (e) {
      log(`Couldn't read which machine this is: ${e.message}`);
      emit();
    }
  }

  // Save one Maslow setting and write the config file, as the current UI does
  async function saveSetting(path, value, label, patch) {
    try {
      await m.setSetting(path, value);
      await m.saveConfig();
      log(`${label} saved: ${value} mm`);
      set(patch);
    } catch (err) {
      log(`Couldn't save the ${label.toLowerCase()}: ${err.detail || err.reason}`);
      emit();
    }
  }

  // The SD card listing, over HTTP like the current UI (GET /upload?path=/). Read-only.
  async function listFiles() {
    try {
      const r = await fetch(`/upload?path=${encodeURIComponent("/")}`);
      const j = await r.json();
      const files = (j.files || []).filter(f => f.size !== "-1").map(f => ({ name: f.name, size: kb(Number(f.size)) }));
      set({ files });
    } catch (e) {
      log(`Couldn't list the SD card: ${e.message}`);
      emit();
    }
  }

  // Split what the socket sends into lines and hand each to Tristan's files, as socket.js does
  let partial = "";
  const decoder = new TextDecoder();
  function onData(data) {
    partial += typeof data === "string" ? data : decoder.decode(data, { stream: true });
    const lines = partial.split("\n");
    partial = lines.pop();
    for (const raw of lines) {
      const line = raw.replace(/\r$/, "").trim();
      if (!line) continue;
      globalThis.wsCommands.handleLine(line);
      m.handleLine(line);
      if (line.startsWith("<")) {
        if (!s.statusSeen) { s = { ...s, statusSeen: true }; checkRestart(); }
      } else if (line !== "ok" && line !== "PING") {
        if (line.startsWith("ALARM:") || line.startsWith("[MSG:ERR:")) s = { ...s, error: { reason: line } };
        log(line);
      }
    }
    health();
    emit();
  }

  // A restart happened if the machine comes back in an earlier setup state than it was in when the connection
  // dropped: a boot restores RETRACTED or EXTENDEDOUT at best, never tension or a running step.
  let beforeDrop = null;   // Maslow state when the socket last closed
  const TENSIONED_OR_BUSY = [1, 3, 5, 6, 7, 8, 10];
  function checkRestart() {
    const after = m.state.maslow;
    if (beforeDrop != null && TENSIONED_OR_BUSY.includes(beforeDrop) && [0, 2, 4].includes(after) && after !== beforeDrop) {
      s = { ...s, incident: restarted(beforeDrop, after), op: null };
      log(`The machine restarted: it was in state ${beforeDrop}, now ${after}`);
    }
    beforeDrop = null;
  }

  function connect() {
    if (ws) { ws.onopen = ws.onclose = ws.onmessage = ws.onerror = null; try { ws.close(); } catch (e) { /* already closed */ } }
    set({ closed: null });
    // The firmware's socket is at "/" (WebUIServer.cpp). In development Vite's own reload socket uses "/", so the
    // page asks for /ws and the dev server forwards it to the machine's "/".
    ws = new WebSocket(`ws://${host}${import.meta.env.DEV ? "/ws" : "/"}`, ["arduino"]);
    ws.binaryType = "arraybuffer";
    globalThis.ws_source = ws;   // wscommands.js sends on this
    ws.onopen = () => {
      globalThis.wsCommands.reset();   // a new socket is a new firmware channel
      s = { ...s, statusSeen: false, connectId: s.connectId + 1 };
      log("Connected");
      emit();
      // What the current UI asks for on every new socket (tablet.js, requestMachineState)
      globalThis.wsCommands.send("$Report/Interval=50", { quiet: true });
      globalThis.wsCommands.sendRealtime(0x3f);
      globalThis.wsCommands.send("$G", { quiet: true });
      readSettings();
      readMachine();
      listFiles();
    };
    ws.onmessage = (e) => onData(e.data);
    ws.onclose = (e) => {
      if (s.statusSeen && s.maslow != null) beforeDrop = s.maslow;
      log(`Disconnected (${e.code}${e.reason ? `: ${e.reason}` : ""})`);
      const notice = CLOSE_NOTICES[e.code];
      set({ connected: false, statusSeen: false, closed: notice || null });
      if (!notice) setTimeout(connect, RECONNECT_MS);
    };
  }

  async function start() {
    // Their files publish wsCommands and machine on globalThis and read a few globals; addMessage is where
    // wscommands.js reports problems such as "No reply ... carrying on".
    globalThis.addMessage = (text) => { log(text); emit(); };
    await import("../../../ESP3D-WEBUI/www/js/wscommands.js");
    await import("../../../ESP3D-WEBUI/www/js/machine.js");
    m = globalThis.machine;
    m.on("state", (ms) => set(fromMachine(ms)));
    setInterval(health, 1000);
    connect();
  }

  // Upload a file the user picks to the SD card, the way the current UI does (POST /upload). Resolves with its
  // name once it's on the card, or null if the user cancels.
  function uploadFile() {
    return new Promise((resolve) => {
      const input = Object.assign(document.createElement("input"), { type: "file", accept: ".nc,.gcode,.gc,.ngc,.tap,.txt" });
      input.onchange = async () => {
        const f = input.files[0];
        if (!f) return resolve(null);
        // Read it here, from the user's own copy, so the preview has it without fetching it back from the card
        const url = sdUrl(f.name);
        const reading = f.text().then(readGcode);
        reading.catch(() => {});
        const form = new FormData();
        form.append("path", "/");
        form.append(`/${f.name}S`, String(f.size));
        form.append("myfile[]", f, `/${f.name}`);
        log(`Uploading ${f.name}`);
        emit();
        try {
          const r = await fetch("/upload", { method: "POST", body: form });
          if (!r.ok) throw new Error(`HTTP ${r.status}`);
          log(`${f.name} uploaded`);
          readings[url] = reading;   // replaces any older file of the same name
          delete lines[`/${f.name}`];
          delete lines[`/sd/${f.name}`];
          if (chosen === f.name) chooseFile(f.name);   // the same name uploaded again: show the new one
          await listFiles();
          resolve(f.name);
        } catch (e) {
          log(`Upload of ${f.name} failed: ${e.message}`);
          emit();
          resolve(null);
        }
      };
      input.click();
    });
  }

  const setStock = (stock) => saveSetting(SETTINGS.stock, stock, "Stock thickness", { stock });

  // Run a machine.js action and follow its Operation in state.op. start() returns the Operation.
  function run(action, start) {
    s = { ...s, op: { action, phase: "starting", maslowAt: s.maslow } };
    log(`${action}: sent`);
    emit();
    const op = typeof start === "string" ? m[start]() : start();
    const mine = () => s.op?.action === action;   // a later action replaces this one
    op.accepted.then(() => { if (mine() && s.op.phase === "starting") set({ op: { ...s.op, phase: "running" } }); }, () => {});
    op.then(
      () => { log(`${action}: done`); if (mine()) set({ op: null }); else emit(); },
      (err) => {
        // Ended by our own Stop: not a failure
        if (err.reason === "reset") { if (mine()) set({ op: null }); return; }
        const detail = err.detail || err.reason;
        log(`${action}: ${detail}`);
        // A job that ended on an error is an incident to acknowledge, not just a message
        if ((action === "run" || action === "continueJob") && err.reason === "failed") {
          set({ op: null, incident: jobFailed(detail, m.state.stopped?.line) });
          return;
        }
        if (mine()) set({ op: { action, phase: "failed", detail, maslowAt: s.maslow } }); else emit();
      });
  }

  function send(action, payload = {}) {
    if (action === "connect") { if (!m) start(); return; }
    if (action === "reconnect") { connect(); return; }
    if (action === "chooseFile") { chooseFile(payload.file); return; }   // reads over HTTP; doesn't need the socket
    if (!m) return;
    if (action === "run") { run("run", () => m.runFile(`/${payload.file}`)); return; }
    if (action === "continueJob") {
      const st = m.state.stopped;
      if (st) run("continueJob", () => m.resumeFile(st.file, st.line));
      return;
    }
    if (action === "trace") { run("trace", () => traceFile(payload.file)); return; }
    if (action === "uploadFile") return uploadFile();
    if (action === "ackIncident") { set({ incident: null }); return; }
    if (action === "setStock") { setStock(payload.stock); return; }
    if (action === "setSpoilboard") { saveSetting(SETTINGS.spoil, payload.mm, "Spoil board thickness", { workArea: { ...s.workArea, spoil: payload.mm } }); return; }
    if (action === "setExtendDist") {
      if (payload.mm === 0) { log(`Not sent: extend distance 0 (${s.notYet.extendUntilDone})`); emit(); return; }
      saveSetting(SETTINGS.extend, payload.mm, "Extend distance", { extendDist: payload.mm });
      return;
    }
    if (action === "jog") {
      const delta = Object.fromEntries([["x", payload.dx], ["y", payload.dy], ["z", payload.dz]].filter(([, v]) => v));
      if (!Object.keys(delta).length) return;
      const feed = delta.x || delta.y ? JOG_FEED.xy : JOG_FEED.z;
      const op = m.jog(delta, { feed });
      op.catch(err => { if (err.reason !== "reset") { log(`Jog: ${err.detail || err.reason}`); emit(); } });
      return;
    }
    if (action === "stop") {
      s = { ...s, op: null };
      m.stop().catch(err => { log(`Stop: ${err.detail || err.reason}`); emit(); });
      return;
    }
    if (ACTIONS[action]) { run(action, ACTIONS[action]); return; }
    if (action === "doneExtending") {
      log("Done extending needs a firmware change. Today Extend ends by itself when the belts reach their length.");
      emit();
      return;
    }
    // Everything else comes in the next steps
    if (["toolCheck", "watchForTool", "setZFromStickout"].includes(action)) return;   // automatic, not a user press
    log(`Not sent: ${action} (${s.notYet[action] || "not connected yet"})`);
    emit();
  }

  return {
    getState: () => s,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    send,
    isSimulated: false
  };
}
