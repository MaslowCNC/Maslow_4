// The machine's network settings: the WiFi it joins, what it does when it can't, its hotspot, and its name. These
// are FluidNC's flash settings (firmware/FluidNC/src/WebUI/WifiConfig.cpp, NetSettings.cpp), not the config file,
// so they're read, set and listed with [ESP…] commands over HTTP, as the current UI's setup and WiFi dialogs do:
//   [ESP400]  every setting, as JSON (read once when the Network section opens)
//   [ESP410]  scan for WiFi networks
//   [ESP401]P=<name> V=<value>  set one, saved to flash at once
//   [ESP420]  system status, with the network the board is on now
// A change takes effect when the board restarts.

// The firmware's settings, by the name [ESP401] takes, and their limits (WifiConfig.cpp, NetSettings.cpp)
export const NET_SETTINGS = {
  ssid:       { name: "Sta/SSID", label: "WiFi network", min: 0, max: 32 },
  password:   { name: "Sta/Password", label: "WiFi password", min: 8, max: 64, secret: true },
  mode:       { name: "WiFi/Mode", label: "Connects through" },
  apSsid:     { name: "AP/SSID", label: "Hotspot name", min: 1, max: 32 },
  apPassword: { name: "AP/Password", label: "Hotspot password", min: 8, max: 64, secret: true },
  hostname:   { name: "Hostname", label: "Name", min: 1, max: 32 }
};

// WiFi/Mode's options, as [ESP400] lists them, in the words the page uses. The board always keeps the hotspot to fall
// back on: "Join WiFi only" (STA) and Off aren't offered, since a board that can't join would be lost until it's
// reached over USB. "Hotspot only" is for a machine always used over its hotspot: it skips the WiFi setup screen.
export const WIFI_MODES = [
  ["STA>AP", "WiFi, with the hotspot as a fallback"],
  ["AP", "Hotspot only"]
];

// Modes the board can be set to elsewhere (the current UI, $WiFi/Mode) that the page doesn't offer: shown as they are,
// so the choice can be changed to one that keeps the hotspot
export const OTHER_WIFI_MODES = { STA: "WiFi only, no hotspot (not recommended)", Off: "WiFi off" };

// The network settings before they're read: read is "idle" | "reading" | "ready" | "failed"; scan and save are null
// until used ({ state: "scanning" | "done" | "failed", list, detail } and { state: "saving" | "saved" | "failed",
// detail }); restartNeeded once something was saved, since the board only uses it after a restart
export const NET_IDLE = { read: "idle", detail: null, status: null, settings: null, scan: null, save: null, restartNeeded: false };

// A password reads back as this; it isn't the password
export const HIDDEN = "********";

/** [ESP400]'s JSON → { ssid, mode, apSsid, hostname } (mode as the option's name, e.g. "STA>AP"). */
export function parseNetSettings(text) {
  const j = typeof text === "string" ? JSON.parse(text) : text;
  const items = j.EEPROM || j.data || [];
  const byName = Object.fromEntries(items.filter((i) => i && i.P).map((i) => [i.P.toLowerCase(), i]));
  const value = (key) => byName[NET_SETTINGS[key].name.toLowerCase()]?.V;
  const modeItem = byName[NET_SETTINGS.mode.name.toLowerCase()];
  let mode = null;
  if (modeItem) {
    // O is a list of { optionName: number }; V is the number
    const opt = (modeItem.O || []).map((o) => Object.entries(o)[0]).find(([, n]) => String(n) === String(modeItem.V));
    mode = opt ? opt[0] : null;
  }
  return { ssid: value("ssid") ?? "", mode, apSsid: value("apSsid") ?? "", hostname: value("hostname") ?? "" };
}

/** [ESP420]'s text → where the board is now: { sta: { ssid, connected, ip } | null, ap: { ssid, ip } | null }. */
export function parseNetStatus(text) {
  const out = { sta: null, ap: null };
  // Lines like [MSG:Mode=STA:SSID=Workshop:Status=Connected:IP=192.168.1.42:MAC=…] and [MSG:Mode=AP:SSID=maslow:IP=…]
  for (const m of String(text).matchAll(/Mode=(STA|AP):([^\]\n]*)/g)) {
    const f = {};
    // Split on ":Key=" so an SSID with a colon in it stays whole
    for (const part of (":" + m[2]).split(/:(?=(?:SSID|Status|IP|MAC)=)/).filter(Boolean)) {
      const i = part.indexOf("=");
      f[part.slice(0, i)] = part.slice(i + 1);
    }
    if (m[1] === "STA") out.sta = { ssid: f.SSID ?? "", connected: f.Status === "Connected", ip: f.IP ?? "" };
    else out.ap = { ssid: f.SSID ?? "", ip: f.IP ?? "" };
  }
  return out;
}

/**
 * Whether the board is on its hotspot because it couldn't join WiFi: a first boot with no network set, or a network
 * it can't reach. Not when it's set to "Hotspot only": then the hotspot is what was asked for. `settings` may be null
 * when only [ESP420] was read; the hotspot without a WiFi connection then counts.
 */
export function onFallbackHotspot(status, settings) {
  return !!status?.ap && !status.sta?.connected && settings?.mode !== "AP";
}

/** [ESP410]'s JSON → [{ ssid, signal, locked }], strongest first, one per name, without hidden networks. */
export function parseApList(text) {
  const j = typeof text === "string" ? JSON.parse(text) : text;
  const list = j.AP_LIST || j.data || [];
  const best = new Map();
  for (const ap of list) {
    const ssid = String(ap.SSID ?? "");
    if (!ssid) continue;
    const e = { ssid, signal: Number(ap.SIGNAL) || 0, locked: String(ap.IS_PROTECTED) === "1" };
    if (!best.has(ssid) || best.get(ssid).signal < e.signal) best.set(ssid, e);
  }
  return [...best.values()].sort((a, b) => b.signal - a.signal);
}

/** The [ESP401] line that sets one setting. The firmware reads the value up to a space and %-decodes it. */
export function setCommand(key, value) {
  return `[ESP401]P=${NET_SETTINGS[key].name} V=${encodeURIComponent(String(value))}`;
}

/** Why a value can't be saved, or null. */
export function netError(key, value) {
  const s = NET_SETTINGS[key];
  const v = String(value ?? "");
  if (key === "mode") return WIFI_MODES.some(([m]) => m === v) ? null : "Choose an option";
  if (key === "hostname" && !/^[A-Za-z0-9-]*$/.test(v)) return "Letters, numbers and dashes only";
  if (v.length < s.min) return s.min === 1 ? "Can't be empty" : `At least ${s.min} characters`;
  if (v.length > s.max) return `At most ${s.max} characters`;
  return null;
}

/**
 * The changes to send for a form, in order, or the first error. `form` holds the fields the user can edit;
 * `current` what [ESP400] read. A password reads back hidden, so it's sent only when typed, and it can't be
 * cleared: the firmware takes an empty value as a read. So a new network needs its password, unless the scan
 * showed it open (`form.open`).
 */
export function netChanges(form, current) {
  const changes = [];
  if ("ssid" in form && form.ssid !== (current.ssid ?? "") && form.ssid && !form.password && !form.open) {
    return { changes, error: `Enter the password for ${form.ssid}` };
  }
  for (const key of ["ssid", "password", "mode", "apSsid", "apPassword", "hostname"]) {
    if (!(key in form)) continue;
    const v = form[key];
    if (NET_SETTINGS[key].secret ? !v : v === (current[key] ?? "")) continue;
    const err = netError(key, v);
    if (err) return { changes: [], error: `${NET_SETTINGS[key].label}: ${err}` };
    changes.push([key, v]);
  }
  return { changes, error: null };
}

/**
 * What to tell the user before they save, so a mistake doesn't lock them out. `scan` is the last scan
 * ({ state, list }) or null; `changes` what Save will send (netChanges). A network change is checked against the
 * scan: a name the board can't see means it will fall back to its hotspot (or, with "Join WiFi only", go dark).
 */
export function netWarnings({ form, current, scan, changes }) {
  const out = [];
  const keys = new Set(changes.map(([k]) => k));
  const ssid = form.ssid ?? current.ssid ?? "";
  const mode = form.mode ?? current.mode;
  const apSsid = form.apSsid ?? current.apSsid ?? "";
  if (keys.has("ssid") && ssid) {
    if (scan?.state === "done" && !scan.list.some((ap) => ap.ssid === ssid)) {
      out.push(`The board can't see ${ssid} right now. Check the name: it has to match exactly.`);
    } else if (scan?.state !== "done") {
      out.push(`Scan first to check the board can see ${ssid}.`);
    }
  }
  const risky = ["ssid", "password", "mode", "apSsid", "apPassword"].some((k) => keys.has(k));
  if (mode === "STA") {
    out.push("This board is set to WiFi only, with no hotspot to fall back on: if it can't join, you'll need USB to reach it. Choose \"WiFi, with the hotspot as a fallback\".");
  } else if (mode === "AP" && risky) {
    // Hotspot only: the hotspot is the only way in after the restart
    out.push(`After the restart you'll reach the board only through its hotspot ${apSsid}${keys.has("apPassword")
      ? ", with the new hotspot password. Note it down." : ". Make sure you know the hotspot password (12345678 unless it was changed)."}`);
  } else if (risky) {
    out.push(keys.has("apPassword")
      ? `If the board can't join, you'll reach it through its hotspot ${apSsid} with the new hotspot password. Note it down.`
      : `If the board can't join, you'll reach it through its hotspot ${apSsid}. Make sure you know the hotspot password (12345678 unless it was changed).`);
  }
  return out;
}
