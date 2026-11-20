// The raw settings lists in Settings › Advanced: every FluidNC and Maslow setting the board has, as [ESP400]
// lists them (firmware/FluidNC/src/WebUI/WebCommands.cpp, listSettings). The current UI's Settings tab reads the
// same list (ESP3D-WEBUI/www/js/settings.js).
//
// [ESP400] answers {"EEPROM": [...]} (or {"data": [...]} with json=yes). Each entry:
//   F  where it lives: "nvs" (flash settings: WiFi, web, $ settings) or "tree" (the running config, maslow.yaml)
//   P  its name; a config item is a path, "/Maslow_Scale_X", "/kinematics/MaslowKinematics/tlX"
//   T  type: I integer, R real, S text, B choice (O lists the options), A IP address
//   V  value; M and S the minimum and maximum (a text's length), when the firmware gives them
//
// Saving (as the current UI does; machine.js sends them): a flash setting with [ESP401]P=<name> T=<type> V=<value> over HTTP, kept in flash
// at once; a config item with machine.js setSetting() ($/<path>=<value>), then saveConfig() ($CO) once, which writes
// maslow.yaml.

import { MACHINE_SETTINGS } from "./settings.js";

// Maslow settings that also have an editor of their own, by the Settings section it's in
const FRIENDLY = Object.fromEntries([
  ...Object.values(MACHINE_SETTINGS).map((s) => s.path),
  "Maslow_spoilboardThickness", "Maslow_workThickness", "Maslow_Extend_Dist"
].map((p) => [p, "Frame and belts"]));
// Real-number ranges the JSON doesn't carry (it gives M and S for integers and text only), from MachineConfig.cpp
const RANGES = {
  ...Object.fromEntries(Object.values(MACHINE_SETTINGS).map((s) => [s.path, [s.min, s.max]])),
  Maslow_spoilboardThickness: [0, 50], Maslow_workThickness: [0, 50], Maslow_Extend_Dist: [0, 4250],
  Maslow_Acceptable_Calibration_Threshold: [0, 1], Maslow_calibration_grid_width_mm_X: [0, 3000],
  Maslow_calibration_grid_height_mm_Y: [0, 3000], arc_tolerance_mm: [0.001, 1], junction_deviation_mm: [0.01, 1]
};
// Flash settings the board only uses after a restart: its network and servers (WifiConfig.cpp, NetSettings.cpp)
const RESTART = /^(Hostname|Sta\/|AP\/|WiFi\/|HTTP\/|Telnet\/|Bluetooth\/|BT\/|network\/|Ethernet\/|NTP\/|MDNS\/|Notification\/)/i;
const ANCHORS = "kinematics/MaslowKinematics/";
// Config items the firmware lists but can't change while running (RuntimeSetting.cpp), with why
const LOCKED = { "stepping/engine": "The firmware can't change this while running. Edit maslow.yaml instead." };
const IP = /^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}$/;
const TYPES = { I: "int", R: "number", S: "text", A: "ip", B: "choice" };

/** The two lists and their groups, in the order the page shows them */
export const LISTS = {
  // The ones Settings › Frame and belts also edits come last, so the list opens on what's only here
  maslow: { label: "Maslow", groups: ["Maslow", "Frame anchors and arms", "Also in Frame and belts"] },
  fluidnc: { label: "FluidNC", groups: ["Board (flash)"] }   // then the config file's sections, as they come
};

function options(O) {
  return (O || []).flatMap((o) => Object.entries(o).map(([label, value]) => ({ label: label.trim(), value: String(value).trim() })));
}

/** One [ESP400] entry → the page's item, or null if it isn't a setting */
export function toItem(e) {
  if (!e || e.P == null || e.T == null || e.V == null) return null;
  const config = String(e.F).toLowerCase() !== "nvs" && !/flash/i.test(e.F);
  const path = config ? String(e.P).replace(/^\//, "") : String(e.P);
  const opts = options(e.O);
  let type = TYPES[e.T] || "text", value = String(e.V).trim();
  if (opts.length) {
    // Most choices give the option's value; a few (the step engine) give its name
    const byLabel = opts.find((o) => o.label === value);
    if (!opts.some((o) => o.value === value) && byLabel) value = byLabel.value;
    const labels = opts.map((o) => o.label.toLowerCase()).sort().join();
    type = labels === "false,true" || labels === "off,on" ? "bool" : "choice";
  }
  const item = { path, store: config ? "config" : "flash", espType: String(e.T), type, value };
  if (opts.length) item.options = opts;
  if (e.M != null) item.min = Number(e.M);
  if (e.S != null) item.max = Number(e.S);
  if (type === "number" && RANGES[path]) [item.min, item.max] = RANGES[path];
  if (value.startsWith("****")) { item.secret = true; item.value = ""; }
  // Which list and group, and the short name shown in it
  if (config && /^Maslow/.test(path)) Object.assign(item, { list: "maslow", group: "Maslow", name: path });
  else if (config && path.startsWith(ANCHORS)) Object.assign(item, { list: "maslow", group: "Frame anchors and arms", name: path.slice(ANCHORS.length) });
  else if (config) Object.assign(item, { list: "fluidnc", group: path.includes("/") ? `Config: ${path.split("/")[0]}` : "Config: general", name: path });
  else Object.assign(item, { list: "fluidnc", group: "Board (flash)", name: path });
  // A restart: known for the flash network settings; the Maslow ones are used at once; other config items vary
  item.restart = !config ? RESTART.test(path) : item.list === "maslow" ? false : null;
  if (FRIENDLY[path]) Object.assign(item, { friendly: FRIENDLY[path], group: `Also in ${FRIENDLY[path]}` });
  if (LOCKED[path] && config) item.locked = LOCKED[path];
  return item;
}

/** [ESP400]'s answer (text or parsed JSON) → the items, in the board's order. Throws if it isn't a settings list. */
export function parseSettingsList(answer) {
  const j = typeof answer === "string" ? JSON.parse(answer) : answer;
  const list = j?.EEPROM || j?.data;
  if (!Array.isArray(list)) throw new Error("No settings in the answer");
  return list.map(toItem).filter(Boolean);
}

/** The items of one list, grouped: [[group, items]], the list's own groups first */
export function grouped(items, list, filter = "") {
  const q = filter.trim().toLowerCase();
  const hit = (i) => !q || i.path.toLowerCase().includes(q) || i.value.toLowerCase().includes(q)
    || (i.options || []).some((o) => o.value === i.value && o.label.toLowerCase().includes(q));
  const groups = new Map(LISTS[list].groups.map((g) => [g, []]));
  for (const i of items) if (i.list === list && hit(i)) {
    if (!groups.has(i.group)) groups.set(i.group, []);
    groups.get(i.group).push(i);
  }
  return [...groups].filter(([, g]) => g.length);
}

/** Why a typed value can't be saved, or null */
export function valueError(item, text) {
  const v = String(text ?? "").trim();
  switch (item.type) {
    case "int":
      if (!/^-?\d+$/.test(v)) return "Enter a whole number";
      break;
    case "number":
      if (v === "" || !Number.isFinite(Number(v))) return "Enter a number";
      break;
    case "ip":
      return IP.test(v) ? null : "Enter an IP address, like 192.168.0.1";
    case "bool": case "choice":
      return item.options.some((o) => o.value === v) ? null : "Choose an option";
    default: {
      const n = String(text ?? "").length;
      if (item.min != null && n < item.min) return item.min === 1 ? "Can't be empty" : `At least ${item.min} characters`;
      if (item.max != null && n > item.max) return `At most ${item.max} characters`;
      return null;
    }
  }
  const n = Number(v);
  if (item.min != null && item.max != null && (n < item.min || n > item.max)) return `Between ${item.min} and ${item.max}`;
  return null;
}

/** Whether a typed value differs from the board's (numbers compare as numbers: 1 is 1.000). A secret's empty box is
 * "leave it as it is". */
export function isChanged(item, text) {
  if (text == null) return false;
  if (item.secret) return text !== "";
  if (item.type === "int" || item.type === "number") {
    const a = Number(String(text).trim()), b = Number(item.value);
    return String(text).trim() === "" || !Number.isFinite(a) || a !== b;
  }
  return item.type === "text" ? text !== item.value : String(text).trim() !== item.value;
}

/** The edits to send, from drafts (path → typed text): { changes: [{ path, store, value }], errors: [{ path, error }] } */
export function pendingChanges(items, drafts) {
  const changes = [], errors = [];
  for (const i of items) {
    const t = drafts[i.path];
    if (i.locked || !isChanged(i, t)) continue;
    const error = valueError(i, t);
    if (error) errors.push({ path: i.path, error });
    else changes.push({ path: i.path, store: i.store, value: i.type === "text" ? t : String(t).trim() });
  }
  return { changes, errors };
}

/** The [ESP401] line that sets a flash setting. The firmware reads the value up to a space and %-decodes it. */
export function espSetCommand(item, value) {
  return `[ESP401]P=${item.path} T=${item.espType} V=${encodeURIComponent(String(value))}`;
}


/** The items with the saved values put in (a secret stays hidden) */
export function withValues(items, saved) {
  const by = new Map(saved.map((c) => [c.path, c.value]));
  return items.map((i) => (by.has(i.path) && !i.secret ? { ...i, value: by.get(i.path) } : i));
}

