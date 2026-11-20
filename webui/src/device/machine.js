// The real machine, through Tristan's machine.js and wscommands.js (ESP3D-WEBUI/www/js), used as they are.
// It has the same interface as the simulator, getState(), subscribe(fn) and send(action, payload), and builds
// the same state shape from machine.state, so the screens don't change.
//
// Actions so far: Stop, the belt steps of Setup (retract, extend, apply tension, release tension, find
// anchors), running a file, Trace boundary, jogging, Set home XY, Set Z, Park, Clear error, the belt motor test, and
// on the Mini Home Z, Remove tool and Spindle on / off. Each goes through a machine.js action, and its Operation is kept in state.op so the screens can
// show "starting", then nothing (done) or why it failed. The screen itself still comes from the machine's state, so
// every connection shows the same thing. The tool steps come next: they need the spindle board's status field.

import { restarted, jobFailed, holding } from "../flow/incidents.js";
import { readGcode, boundaryPath } from "../gcode/reader.js";
import { MACHINE_SETTINGS, settingError } from "./settings.js";
import { parseSettingsList, valueError, espSetCommand, withValues } from "./settingslist.js";
import { FLASH_IDLE, flashActions, filesQuery, fileUrl, parentPath } from "./flashfiles.js";
import { NET_IDLE, parseNetSettings, parseNetStatus, parseApList, onFallbackHotspot, setCommand, netChanges } from "./network.js";

// What the firmware's close codes mean (WSChannel.h). For these the page shows a notice and doesn't reconnect.
const CLOSE_NOTICES = { 4001: "othertab", 4002: "toomany" };
const RECONNECT_MS = 3000;
const SETTINGS = {
  spoil: "Maslow_spoilboardThickness", stock: "Maslow_workThickness", extend: "Maslow_Extend_Dist"
};
// The frame's geometry under kinematics/MaslowKinematics, in this order (readSettings)
const FRAME_KEYS = ["tlX", "tlY", "trX", "trY", "blX", "blY", "brX", "brY", "tlZ", "trZ", "blZ", "brZ", "beltEndExtension", "armLength"];
// The board type [ESP800] reports (firmware BoardId), as the machine's name. The Unified Control Board is the Mini's.
const MACHINES = { maslow41: "Maslow 4.1", unified: "Maslow Mini", unified36v: "Maslow Mini 36 V" };

// UI action → machine.js action, for the actions this page sends so far
const ACTIONS = {
  retract: "retractBelts", extend: "extendBelts", tension: "applyTension",
  release: "releaseTension", findAnchors: "calibrate",
  park: "park", clearError: "clearAlarm",
  rehomeZ: "homeSpindleZ", removeTool: "removeTool",
  spindleOn: "spindleOn", spindleOff: "spindleOff"
};
// The spindle board's actions, Mini only. Their Operation ends when the firmware answers ok, which only means the
// board was sent the command: it finishes 1 to 40 s later, and can fail (a tool removal that can't clear the beam).
// Until the status report has a spindle board field, the page says "sent", never "done", and the board's own log
// lines tell the rest. When the field exists, machine.js waits for it and these become ordinary actions.
const SPINDLE_BOARD = ["rehomeZ", "removeTool"];
const NO_SPINDLE_BOARD = "This machine has no spindle board (the Maslow 4.1's Z is set by eye)";
// Mini-only actions: the spindle board's, and switching the spindle (the 4.1's config has no spindle to drive)
const MINI_ONLY = [...SPINDLE_BOARD, "spindleOn", "spindleOff"];
const NO_SPINDLE = "This machine's spindle isn't controlled by the board (the Maslow 4.1 has no spindle output)";
const isMini = (name) => /^Maslow Mini/.test(name || "");

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
      // Until [ESP800] says which machine this is; readMachine() then enables them on a Mini
      rehomeZ: "Waiting to learn which machine this is",
      removeTool: "Waiting to learn which machine this is",
      spindleOn: "Waiting to learn which machine this is",
      spindleOff: "Waiting to learn which machine this is",
      // Extend distance 0 means "until Done extending", which today's firmware doesn't support: it would extend 0 mm
      extendUntilDone: "0 needs the Done extending firmware change. Until then, Extend stops at the distance set here."
    },
    machine: null,           // the machine's name from [ESP800], e.g. "Maslow Mini"; null until known
    extendDist: null,        // Maslow_Extend_Dist, mm
    maslow: null, fluid: "", statusSeen: false,
    tool: "unknown", settingZ: false, zSetCount: 0, spindle: "off", error: null,
    motorTest: null,         // the last belt motor test: { passed: [arm], failed: [{ arm, problem }] }
    job: null, lastJob: null, motion: null, result: null, restored: false,
    gcode: null,             // the chosen file, read: { file, status: "reading" | "ready" | "failed", detail?, ...readGcode() }
    workArea: { spoil: 0 }, stock: 0, anchorsSaved: true,
    settings: {},            // work area, park, scale, belt limits (device/settings.js); a key is missing until read
    settingsList: { status: "idle" },   // Settings › Advanced's raw lists: { status, items, detail, save } (device/settingslist.js)
    frame: null,             // { tl, tr, bl, br: [x, y] mm, z: [tl, tr, bl, br], arm: beltEndExtension + armLength, vertical }
    belts: [0, 0, 0, 0], motorCurrent: null,
    pos: { x: 0, y: 0, z: 0 }, wco: { x: 0, y: 0, z: 0 },
    files: [], log: [], pending: null,
    net: NET_IDLE,           // the network settings (device/network.js), read when Settings › Network or Device opens
    flash: FLASH_IDLE        // the board's own files (device/flashfiles.js), read when Settings › Advanced › Files opens
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
      // On while the report's spindle speed (FS) is above 0: a job, Stop or another connection changes it too
      spindle: ms.spindleSpeed > 0 ? "on" : "off",
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
    const [spoil, stock, extend] = await Promise.all(Object.values(SETTINGS).map(read));
    const keys = Object.keys(MACHINE_SETTINGS);
    const values = await Promise.all(keys.map((k) => read(MACHINE_SETTINGS[k].path)));
    const settings = Object.fromEntries(keys.flatMap((k, i) => (values[i] == null ? [] : [[k, values[i]]])));
    set({ workArea: { spoil: spoil ?? 0 }, stock: stock ?? 0, extendDist: extend, settings });
    await readFrame();
  }

  // The anchors (the calibrated ones once Find anchors has run) and orientation, for the accuracy overlay. Read on
  // connect and again when a Find anchors run ends.
  async function readFrame() {
    const read = (path) => m.getSetting(path).then(Number).catch(() => null);
    const K = (n) => read(`kinematics/MaslowKinematics/${n}`);
    const v = await Promise.all(FRAME_KEYS.map(K));
    const vertical = await m.getSetting("Maslow_vertical").then((x) => /^(true|1)$/i.test(x.trim())).catch(() => false);
    if (v.some((x) => x == null)) { set({ frame: null }); return; }
    const [tlX, tlY, trX, trY, blX, blY, brX, brY, tlZ, trZ, blZ, brZ, ext, arm] = v;
    set({ frame: { tl: [tlX, tlY], tr: [trX, trY], bl: [blX, blY], br: [brX, brY], z: [tlZ, trZ, blZ, brZ], arm: ext + arm, vertical } });
  }

  // Which machine this is: [ESP800] reports the board ("board:unified"). Over HTTP, like the current UI.
  async function readMachine() {
    try {
      const r = await fetch(`/command?plain=${encodeURIComponent("[ESP800]")}`);
      const tag = (await r.text()).match(/board:\s*([a-z0-9]+)/i)?.[1];
      const machine = MACHINES[tag] || (tag ? `Unknown board (${tag})` : "Unknown machine");
      const notYet = { ...s.notYet };
      for (const a of MINI_ONLY) {
        if (isMini(machine)) delete notYet[a]; else notYet[a] = SPINDLE_BOARD.includes(a) ? NO_SPINDLE_BOARD : NO_SPINDLE;
      }
      set({ machine, notYet });
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

  // Save several machine settings (device/settings.js) and write the config file once, as the current UI's popups
  // do. Only the values that changed are sent; one that fails stops the rest.
  async function saveSettings(values, label) {
    const changed = Object.entries(values).filter(([k, v]) => s.settings[k] !== v);
    const bad = changed.map(([k, v]) => [k, settingError(k, v)]).find(([, e]) => e);
    if (bad) { log(`Not sent: ${label} (${bad[0]}: ${bad[1]})`); emit(); return; }
    if (!changed.length) return;
    const saved = {};
    try {
      for (const [k, v] of changed) {
        await m.setSetting(MACHINE_SETTINGS[k].path, v);
        saved[k] = v;
      }
      await m.saveConfig();
      log(`${label} saved: ${changed.map(([k, v]) => `${MACHINE_SETTINGS[k].path}=${v}`).join(", ")}`);
    } catch (err) {
      log(`Couldn't save the ${label.toLowerCase()}: ${err.detail || err.reason}`);
    }
    // What was set is in the machine's memory even if writing the file failed, so show it
    set({ settings: { ...s.settings, ...saved } });
  }

  // Settings › Advanced: every setting, from [ESP400] (about 40 KB, so only when the lists open or after a save)
  async function readSettingsList() {
    if (s.settingsList.status === "reading") return;
    set({ settingsList: { ...s.settingsList, status: "reading", detail: null } });
    try {
      set({ settingsList: { status: "ready", items: parseSettingsList(await esp("[ESP400]")), save: s.settingsList.save } });
    } catch (e) {
      log(`Couldn't read the settings list: ${e.message}`);
      set({ settingsList: { ...s.settingsList, status: "failed", detail: e.message } });
    }
  }

  // Save what changed, in order; one that fails stops the rest. A flash setting goes with [ESP401] (kept at once); a
  // config item with setSetting(), then one saveConfig() ($CO) writes maslow.yaml. Passwords aren't logged.
  async function saveSettingsList(changes) {
    const items = s.settingsList.items || [];
    const byPath = new Map(items.map((i) => [i.path, i]));
    const bad = changes.map((c) => [c.path, byPath.get(c.path) ? valueError(byPath.get(c.path), c.value) : "Unknown setting"]).find(([, e]) => e);
    const setSave = (save, saved = []) => set({ settingsList: { ...s.settingsList, items: withValues(s.settingsList.items || [], saved), save } });
    if (bad) { log(`Not sent: settings (${bad[0]}: ${bad[1]})`); setSave({ state: "failed", detail: `${bad[0]}: ${bad[1]}` }); return; }
    if (!changes.length) return;
    setSave({ state: "saving" });
    const saved = [];
    try {
      for (const c of changes) {
        const i = byPath.get(c.path);
        if (i.store === "flash") await esp(espSetCommand(i, c.value));
        else await m.setSetting(c.path, c.value);
        saved.push(c);
      }
      if (saved.some((c) => c.store === "config")) await m.saveConfig();
      log(`Settings saved: ${saved.map((c) => (byPath.get(c.path).secret ? c.path : `${c.path}=${c.value}`)).join(", ")}`);
      setSave({ state: "saved", restart: saved.some((c) => byPath.get(c.path).restart) }, saved);
    } catch (err) {
      const why = err.detail || err.reason || err.message;
      log(`Couldn't save ${changes[saved.length].path}: ${why}`);
      setSave({ state: "failed", detail: `${changes[saved.length].path}: ${why}` }, saved);
    }
    // Frame and belts shows some of the same settings: read them again if one of them changed
    if (saved.some((c) => byPath.get(c.path).friendly)) readSettings();
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

  // Delete a file from the SD card, over HTTP like the listing (GET /upload?path=/&action=delete&filename=…).
  // machine.files.delete() can't be used here: it calls the current UI's HTTP helpers (SendGetHttp), which this
  // page doesn't load. The answer is the new listing; its status says whether the delete worked.
  async function deleteFile(name) {
    try {
      const q = new URLSearchParams({ path: "/", action: "delete", filename: name });
      const r = await fetch(`/upload?${q}`);
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const j = await r.json().catch(() => ({}));
      if (/fail|error|cannot/i.test(j.status || "")) throw new Error(j.status);
      log(`${name} deleted`);
    } catch (e) {
      log(`Couldn't delete ${name}: ${e.message}`);
    }
    await listFiles();
  }

  // The board's flash files (LittleFS), over the firmware's /files endpoint like the current UI's SPIFFSdlg.js. A file
  // is read with a plain GET of its path; in development the dev server forwards those under /flashfs, because the
  // page's own paths are Vite's.
  const FLASH_URL = import.meta.env.DEV ? "/flashfs" : "";
  async function filesAnswer(r) {
    if (!r.ok) throw new Error((await r.text().catch(() => "")).trim().slice(0, 120) || `HTTP ${r.status}`);
    return r.json();
  }
  const flash = flashActions({
    get: () => s.flash,
    set: (patch) => set({ flash: { ...s.flash, ...patch } }),
    log: (line) => { log(line); emit(); },
    lock: () => (!s.connected ? "Waiting for the device to answer." : s.readOnly ? "Another tab is driving the machine; this one can only look."
      : s.job ? "Not while a job runs." : s.motion || s.fluid === "Run" || s.fluid === "Jog" ? "Not while the machine moves." : null),
    io: {
      list: async (dir) => filesAnswer(await fetch(`/files?${filesQuery(dir)}`, { cache: "no-store" })),
      act: async (query) => filesAnswer(await fetch(`/files?${query}`, { cache: "no-store" })),
      // As BuildFileUploadFormData (files.js): the size first, so the board can check the whole file arrived
      async upload(path, content) {
        const blob = typeof content === "string" ? new Blob([content], { type: "text/plain" }) : content;
        const dir = parentPath(path);
        const form = new FormData();
        form.append("path", dir === "/" ? "/" : `${dir}/`);
        form.append(`${path}S`, String(blob.size));
        form.append("myfile[]", blob, path);
        return filesAnswer(await fetch(`/files?${filesQuery(dir)}`, { method: "POST", body: form }));
      },
      async read(path) {
        const r = await fetch(fileUrl(path, FLASH_URL), { cache: "no-store" });
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        return r.text();
      },
      download(path, name) {
        Object.assign(document.createElement("a"), { href: fileUrl(path, FLASH_URL), download: name }).click();
      }
    }
  });

  // An [ESP…] command over HTTP, as the current UI sends them. The answer is the body; the board answers 200 even
  // when the command fails, with error:N or a message in the body.
  async function esp(text) {
    const r = await fetch(`/command?plain=${encodeURIComponent(text)}`);
    const body = await r.text();
    if (!r.ok) throw new Error(body.trim() || `HTTP ${r.status}`);
    const bad = body.match(/error:\s*\d+|Parameter must[^\n]*|Invalid[^\n]*/i);
    if (bad) throw new Error(bad[0].trim());
    return body;
  }
  const setNet = (patch) => set({ net: { ...s.net, ...patch } });

  // The network settings and where the board is now: [ESP400] (flash settings, JSON) and [ESP420] (status)
  async function readNetwork() {
    if (s.net.read === "reading") return;
    setNet({ read: "reading", detail: null });
    try {
      const [list, text] = await Promise.all([esp("[ESP400]"), esp("[ESP420]")]);
      const settings = parseNetSettings(list), status = parseNetStatus(text);
      set({ net: { ...s.net, read: "ready", settings, status }, hotspot: onFallbackHotspot(status, settings) });
    } catch (e) {
      log(`Couldn't read the network settings: ${e.message}`);
      setNet({ read: "failed", detail: e.message });
    }
  }

  // On each connect: is the board on its hotspot because it couldn't join WiFi (first boot, or a network it can't
  // reach)? Then the page asks for a network first. [ESP420] is small; the settings ([ESP400], about 40 KB) are only
  // read when the hotspot is up, to tell a fallback from "Hotspot only".
  async function checkHotspot() {
    try {
      const status = parseNetStatus(await esp("[ESP420]"));
      if (!onFallbackHotspot(status, null)) { set({ net: { ...s.net, status }, hotspot: false }); return; }
      await readNetwork();
    } catch (e) {
      log(`Couldn't read the network status: ${e.message}`);
      emit();
    }
  }

  // The WiFi networks the board can see ([ESP410]). The scan takes a few seconds; the board stays connected.
  async function scanNetworks() {
    if (s.net.scan?.state === "scanning") return;
    setNet({ scan: { state: "scanning", list: s.net.scan?.list || [] } });
    try {
      setNet({ scan: { state: "done", list: parseApList(await esp("[ESP410]")) } });
    } catch (e) {
      log(`Couldn't scan for WiFi networks: ${e.message}`);
      setNet({ scan: { state: "failed", list: [], detail: e.message } });
    }
  }

  // Save what changed, one [ESP401] each, in order; one that fails stops the rest. Saved to flash at once, used
  // after a restart. Passwords aren't logged.
  async function saveNetwork(form) {
    const current = s.net.settings || {};
    const { changes, error } = netChanges(form, current);
    if (error) { log(`Not sent: network settings (${error})`); setNet({ save: { state: "failed", detail: error } }); return; }
    if (!changes.length) return;
    setNet({ save: { state: "saving" } });
    const saved = {};
    try {
      for (const [k, v] of changes) {
        await esp(setCommand(k, v));
        saved[k] = v;
      }
      log(`Network settings saved: ${changes.map(([k]) => k).join(", ")}. They take effect after a restart.`);
      setNet({ save: { state: "saved" } });
    } catch (e) {
      log(`Couldn't save the network settings: ${e.message}`);
      setNet({ save: { state: "failed", detail: e.message } });
    }
    const { password, apPassword, ...shown } = saved;
    if (Object.keys(saved).length) {
      // Hotspot only, saved while on the hotspot, ends the WiFi setup screen now: nothing changes at the restart
      const settings = { ...current, ...shown };
      set({ net: { ...s.net, settings, restartNeeded: true }, hotspot: onFallbackHotspot(s.net.status, settings) });
    }
  }

  // Restart the board ([ESP444]RESTART). The socket drops; the page reconnects on its own when the board is back.
  async function reboot() {
    log("Restarting the board");
    set({ net: { ...s.net, restartNeeded: false, save: null } });
    try {
      await esp("[ESP444]RESTART");
    } catch (e) {
      // The board may drop the request as it restarts; only a clear refusal is worth reporting
      if (/error|Parameter/i.test(e.message)) { log(`Couldn't restart: ${e.message}`); emit(); }
    }
  }

  // A line the user typed in the command box, sent as typed: the current UI's Commands panel (commands.js). It's
  // the user's own command, so there's no machine.js action for it; it goes through wsCommands like every line,
  // one at a time. The machine's output lines show in the log as they arrive (onData), then ok or error:N.
  // [ESP…] commands go over HTTP, as everywhere else; ?, ! and ~ are realtime bytes that get no ok.
  const REALTIME = { "?": 0x3f, "!": 0x21, "~": 0x7e };
  async function command(line) {
    const text = String(line || "").trim();
    if (!text) return;
    log(`> ${text}`);
    emit();
    if (/^\[ESP\d+\]/i.test(text)) {
      try {
        const r = await fetch(`/command?plain=${encodeURIComponent(text)}`);
        const body = (await r.text()).trim();
        for (const l of body.split(/\r?\n/).filter(Boolean)) log(l);
        if (!r.ok) log(`HTTP ${r.status}`);
      } catch (e) {
        log(`Couldn't send ${text}: ${e.message}`);
      }
      emit();
      return;
    }
    const ws = globalThis.wsCommands;
    const sent = REALTIME[text] != null ? ws.sendRealtime(REALTIME[text]) !== false
      : ws.send(text, {
        quiet: true,   // its error is logged here, once
        onOk: () => { log("ok"); emit(); },
        onError: (why) => { log(/^\d+$/.test(why) ? `error:${why}` : `No answer to ${text} (${why})`); emit(); }
      });
    if (!sent) { log(`Not sent: ${text} (not connected)`); emit(); }
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
      checkHotspot();
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
    m.on("state", (ms) => {
      const wasFinding = s.maslow === 6;   // CALIBRATION_IN_PROGRESS
      set(fromMachine(ms));
      if (wasFinding && s.maslow !== 6) readFrame();
    });
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
      () => {
        log(SPINDLE_BOARD.includes(action) ? `${action}: sent to the spindle board` : `${action}: done`);
        if (mine()) set({ op: null }); else emit();
      },
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
    // The network settings and restarting are HTTP too
    if (action === "readNetwork") { readNetwork(); return; }
    if (action === "scanNetworks") { scanNetworks(); return; }
    if (action === "saveNetwork") { saveNetwork(payload); return; }
    if (action === "reboot") { reboot(); return; }
    // The board's files are HTTP too; changes are refused in flashActions while a job runs or nothing answers
    if (flash[action]) return flash[action](payload);
    if (!m) return;
    if (action === "command") { command(payload.line); return; }
    if (action === "run") { run("run", () => m.runFile(`/${payload.file}`)); return; }
    if (action === "continueJob") {
      const st = m.state.stopped;
      if (st) run("continueJob", () => m.resumeFile(st.file, payload.line ?? st.line));
      return;
    }
    if (action === "discardStopped") { run("discardStopped", () => m.discardStopped()); return; }
    if (action === "trace") { run("trace", () => traceFile(payload.file)); return; }
    if (action === "uploadFile") return uploadFile();
    if (action === "deleteFile") { deleteFile(payload.name); return; }
    if (action === "ackIncident") { set({ incident: null }); return; }
    if (action === "setStock") { setStock(payload.stock); return; }
    if (action === "setSpoilboard") { saveSetting(SETTINGS.spoil, payload.mm, "Spoil board thickness", { workArea: { ...s.workArea, spoil: payload.mm } }); return; }
    if (action === "setSettings") { saveSettings(payload.values || {}, payload.label || "Settings"); return; }
    if (action === "readSettingsList") { readSettingsList(); return; }
    if (action === "saveSettingsList") { saveSettingsList(payload.changes || []); return; }
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
    if (s.notYet[action]) { log(`Not sent: ${action} (${s.notYet[action]})`); emit(); return; }
    // Set home XY / Set Z: the work origin where the machine is now, through Tristan's setWorkZero
    if (action === "resetHome") { run(action, () => m.setWorkZero({ x: 0, y: 0, z: 0 })); return; }
    if (action === "setHomeXY" || action === "setZ") {
      const p = m.state.mpos;
      if (!p) { log(`Not sent: ${action} (the machine's position isn't known yet)`); emit(); return; }
      // payload: the new home in machine coordinates ({x, y} or {z}); without one, here
      const q = payload || {};
      run(action, () => m.setWorkZero(action === "setZ" ? { z: q.z ?? p.z } : { x: q.x ?? p.x, y: q.y ?? p.y }));
      return;
    }
    // Belt motor test ($TEST): the result comes back in the firmware's log lines, kept in state.motorTest
    if (action === "motorTest") {
      set({ motorTest: null });
      run("motorTest", () => {
        const op = m.testMotors();
        op.then((r) => {
          const bad = r.failed.map((f) => `${f.problem} on ${f.arm}`);
          log(bad.length ? `Motor test: ${bad.join("; ")}` : `Motor test: all ${r.passed.length} arms passed`);
          set({ motorTest: { passed: r.passed, failed: r.failed } });
        }, () => {});
        return op;
      });
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
