// The real machine, through Tristan's machine.js and wscommands.js (ESP3D-WEBUI/www/js), used as they are.
// It has the same interface as the simulator, getState(), subscribe(fn) and send(action, payload), and builds
// the same state shape from machine.state, so the screens don't change.
//
// Actions so far: Stop and the belt steps of Setup (retract, extend, apply tension, release tension, find
// anchors). Each goes through a machine.js action, and its Operation is kept in state.op so the screens can show
// "starting", then nothing (done) or why it failed. The screen itself still comes from the machine's state, so
// every connection shows the same thing. Jogging, files, jobs and the tool steps come next.

// What the firmware's close codes mean (WSChannel.h). For these the page shows a notice and doesn't reconnect.
const CLOSE_NOTICES = { 4001: "othertab", 4002: "toomany" };
const RECONNECT_MS = 3000;
const SETTINGS = {
  workW: "Maslow_Work_Area_X", workH: "Maslow_Work_Area_Y",
  spoil: "Maslow_spoilboardThickness", stock: "Maslow_workThickness"
};

// UI action → machine.js action, for the actions this page sends so far
const ACTIONS = {
  retract: "retractBelts", extend: "extendBelts", tension: "applyTension",
  release: "releaseTension", findAnchors: "calibrate"
};

const time = () => new Date().toTimeString().slice(0, 8);
const kb = (bytes) => (bytes == null ? "" : `${Math.max(1, Math.round(bytes / 1024))} KB`);

export function createMachine({ host = location.host } = {}) {
  const listeners = new Set();
  let m = null;            // Tristan's machine object, once loaded
  let ws = null;
  let s = {
    connected: false, everConnected: false, hotspot: false, connectId: 0, closed: null, readOnly: false,
    op: null,                // { action, phase: "starting" | "running" | "failed", detail, maslowAt } for this page's last action
    beltsKnown: 0,           // how many belt lengths the machine knows (BL "?" = unknown)
    toolSteps: false,        // the tool check, measure and load aren't connected yet (they need the tool field)
    maslow: null, fluid: "", statusSeen: false,
    tool: "unknown", settingZ: false, zSetCount: 0, spindle: "off", error: null,
    job: null, lastJob: null, motion: null, result: null, restored: false,
    workArea: { w: 0, h: 0, spoil: 0 }, stock: 0, anchorsSaved: true,
    belts: [0, 0, 0, 0], motorCurrent: null,
    pos: { x: 0, y: 0, z: 0 }, wco: { x: 0, y: 0, z: 0 },
    files: [], log: [], pending: null
  };

  const emit = () => listeners.forEach(fn => fn(s));
  const set = (patch) => { s = { ...s, ...patch }; emit(); };
  const log = (line) => { s = { ...s, log: [...s.log, `${time()} ${line}`].slice(-200) }; };

  // machine.state → the shape the screens read (the simulator's)
  function fromMachine(ms) {
    const b = ms.belts || {};
    const alarm = ms.grbl === "Alarm";
    // A failure message belongs to the state it happened in: once the machine moves on, drop it
    const op = s.op?.phase === "failed" && ms.maslow !== s.op.maslowAt ? null : s.op;
    return {
      op, beltsKnown: [b.tl, b.tr, b.bl, b.br].filter(v => v != null).length,
      maslow: ms.maslow, fluid: ms.grbl,
      pos: ms.mpos || s.pos, wco: ms.wco || s.wco,
      belts: [b.tl, b.tr, b.bl, b.br].map(v => v ?? 0),
      motorCurrent: ms.motorCurrent,
      job: ms.job ? { file: ms.job.file, line: ms.job.line ?? 0, total: "?", running: true } : null,
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
    const [w, h, spoil, stock] = await Promise.all(Object.values(SETTINGS).map(read));
    set({ workArea: { w: w ?? 0, h: h ?? 0, spoil: spoil ?? 0 }, stock: stock ?? 0 });
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
        if (!s.statusSeen) s = { ...s, statusSeen: true };
      } else if (line !== "ok" && line !== "PING") {
        if (line.startsWith("ALARM:") || line.startsWith("[MSG:ERR:")) s = { ...s, error: { reason: line } };
        log(line);
      }
    }
    health();
    emit();
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
      listFiles();
    };
    ws.onmessage = (e) => onData(e.data);
    ws.onclose = (e) => {
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

  // Run a machine.js action and follow its Operation in state.op
  function run(action, name) {
    s = { ...s, op: { action, phase: "starting", maslowAt: s.maslow } };
    log(`${action}: sent`);
    emit();
    const op = m[name]();
    const mine = () => s.op?.action === action;   // a later action replaces this one
    op.accepted.then(() => { if (mine() && s.op.phase === "starting") set({ op: { ...s.op, phase: "running" } }); }, () => {});
    op.then(
      () => { log(`${action}: done`); if (mine()) set({ op: null }); else emit(); },
      (err) => {
        // Ended by our own Stop: not a failure
        if (err.reason === "reset") { if (mine()) set({ op: null }); return; }
        const detail = err.detail || err.reason;
        log(`${action}: ${detail}`);
        if (mine()) set({ op: { action, phase: "failed", detail, maslowAt: s.maslow } }); else emit();
      });
  }

  function send(action) {
    if (action === "connect") { if (!m) start(); return; }
    if (action === "reconnect") { connect(); return; }
    if (!m) return;
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
    log(`Not sent: ${action} (not connected yet)`);
    emit();
  }

  return {
    getState: () => s,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    send,
    isSimulated: false
  };
}
