// Firmware update, check for updates, and the board's system status: Settings › Device. Both device adapters
// (machine.js, simulator.js) keep the state in state.update and pass how they reach the board and GitHub (io).
//
// The check runs in the browser, not on the board: the board may be on its own hotspot with no internet, and it has
// too little free memory for TLS. The release list comes from GitHub's API; the files come from the copy that
// release.yml mirrors into the repository (web-assets/<tag>/ on Maslow-Main), because GitHub's own release downloads
// send no CORS headers and raw.githubusercontent.com does. Each mirrored folder has a manifest.json with every
// file's size and sha256.
//
// Installing, as the current UI's checkupdatesdlg.js does it:
//   1. the page (index.html.gz) to the flash, through /files like any flash file; the board keeps serving the page
//      it has in memory until the restart;
//   2. the firmware to POST /updatefw (WebUIServer.cpp, WebUpdateUpload): form data "/firmware.binS" = size,
//      myfile[] = the file. The board refuses a file bigger than its OTA partition, answers {"status":"3"} when the
//      image was written (UploadStatus SUCCESSFUL; "1" failed) and restarts about a second later.
// Then the page reconnects by itself (every 3 s) and [ESP800] says which version is running.
//
// There's one firmware.bin for every Maslow board: the board (Maslow 4.1 or Mini) is detected at boot from a resistor
// divider (firmware/FluidNC/src/Maslow/BoardId.cpp). So the check on a file is that it's an ESP32-S3 app image and a
// Maslow build (checkFirmware), not which board it's for.

export const REPO = "MaslowCNC/Maslow_4";
export const RELEASES_URL = `https://api.github.com/repos/${REPO}/releases?per_page=30`;
export const MIRROR_URL = `https://raw.githubusercontent.com/${REPO}/Maslow-Main/web-assets`;

// The update streams (#1142), as Preferences › Update stream had them
export const STREAMS = [
  ["release", "Release", "The newest full release."],
  ["experimental", "Experimental", "The newest pre-release (v…-exp.N), for testing before a release."],
  ["nightly", "Nightly", "The release tagged nightly, rebuilt from the latest code."]
];

// ---- Versions ------------------------------------------------------------------------------------------------

/**
 * The version in a firmware or page version string, parsed: { major, minor, patch, pre: [identifiers] }, or null.
 * The strings look like "FluidNC v1.27.0 (Maslow-Main-abc123)", "v1.28.0-exp.1-dirty" or "v1.27.0-12-g8700a9bf9". A git
 * describe tail ("-12-g8700a9bf9") or "-dirty" marks a local build of that tag, not a pre-release, so it's dropped.
 * (The current UI's parseVersion, checkupdatesdlg.js.)
 */
export function parseVersion(text) {
  if (!text) return null;
  const m = /\bv(\d+)\.(\d+)(?:\.(\d+))?((?:-[0-9A-Za-z.-]+)?)/i.exec(text) || /(\d+)\.(\d+)(?:\.(\d+))?((?:-[0-9A-Za-z.-]+)?)/.exec(text);
  if (!m) return null;
  const pre = m[4].replace(/-dirty$/i, "").replace(/-\d+-g[0-9a-f]+$/i, "").replace(/^-/, "");
  return { major: +m[1], minor: +m[2], patch: m[3] === undefined ? 0 : +m[3], pre: pre ? pre.split(".") : [] };
}

// Semver precedence for one pre-release identifier: numbers before text, numbers by value, text as ASCII
function comparePre(a, b) {
  const an = /^\d+$/.test(a), bn = /^\d+$/.test(b);
  if (an && bn) return Math.sign(+a - +b);
  if (an) return -1;
  if (bn) return 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** -1 if a is older than b, 1 if newer, 0 if the same (semver: v1.28.0-exp.2 < v1.28.0-exp.10 < v1.28.0). */
export function compareVersions(a, b) {
  for (const k of ["major", "minor", "patch"]) if (a[k] !== b[k]) return a[k] < b[k] ? -1 : 1;
  if (!a.pre.length || !b.pre.length) return Math.sign(!a.pre.length - !b.pre.length);
  for (let i = 0; i < Math.min(a.pre.length, b.pre.length); i++) {
    const c = comparePre(a.pre[i], b.pre[i]);
    if (c) return c;
  }
  return Math.sign(a.pre.length - b.pre.length);
}

/** Whether the release tag is newer than the running version. When either can't be read, only a different string counts. */
export function isNewer(current, tag) {
  const a = parseVersion(current), b = parseVersion(tag);
  if (!a || !b) return String(tag || "").trim() !== String(current || "").trim();
  return compareVersions(a, b) < 0;
}

/** Two versions name the same release (a "-dirty" build of v1.27.0 is v1.27.0). */
export function sameVersion(a, b) {
  const x = parseVersion(a), y = parseVersion(b);
  return x && y ? compareVersions(x, y) === 0 : String(a || "").trim() === String(b || "").trim();
}

/** The firmware version in [ESP800]'s answer: "FW version: FluidNC v1.28.0-exp.1 (…) # FW target:…" → "v1.28.0-exp.1". */
export function parseFwVersion(text) {
  const m = String(text || "").match(/FW version:\s*(?:FluidNC\s+)?([^\s#]+)/i);
  return m ? m[1] : null;
}

// ---- Releases -------------------------------------------------------------------------------------------------

/** GitHub's release (from the API) → the fields the page uses. */
export function toRelease(r) {
  return {
    tag: r.tag_name, name: r.name || r.tag_name, prerelease: !!r.prerelease, published: r.published_at || null,
    notes: String(r.body || "").trim(), url: r.html_url || `https://github.com/${REPO}/releases/tag/${r.tag_name}`,
    assets: (r.assets || []).map((a) => a.name)
  };
}

/**
 * The release a stream offers, from GitHub's list (newest first), or null. Drafts never count.
 *   release      — the newest that isn't a pre-release (tag vX.Y.Z)
 *   experimental — the newest pre-release that isn't the rolling nightly (vX.Y.Z-exp.N)
 *   nightly      — the release tagged "nightly"
 * Releases without a firmware.bin are skipped: there's nothing to install.
 */
export function selectRelease(list, stream) {
  const ok = (r) => !r.draft && (r.assets || []).some((a) => a.name === "firmware.bin");
  const pick = {
    nightly: (r) => r.tag_name === "nightly",
    experimental: (r) => r.prerelease && r.tag_name !== "nightly"
  }[stream] || ((r) => !r.prerelease);
  const r = (list || []).find((x) => ok(x) && pick(x));
  return r ? toRelease(r) : null;
}

/** The mirror's folder name for a tag, as release.yml makes it (SAFE_TAG: / and space → -, anything else odd → _). */
export const safeTag = (tag) => String(tag).replace(/[/ ]/g, "-").replace(/[^A-Za-z0-9._-]/g, "_");
export const mirrorUrl = (tag, file) => `${MIRROR_URL}/${encodeURIComponent(safeTag(tag))}/${file}`;

/**
 * Whether a mirrored manifest.json is the release we mean to install: its tag, and a size for each file we need.
 * Returns an error message or null. (The current UI fell back to web-assets/latest/, which can be another release.)
 */
export function manifestError(manifest, tag, files = ["firmware.bin"]) {
  if (!manifest || typeof manifest !== "object") return "The release's file list couldn't be read.";
  if (manifest.tag_name !== tag) return `The mirrored files are for ${manifest.tag_name || "another release"}, not ${tag}.`;
  const missing = files.filter((f) => !(manifest.files?.[f]?.size_bytes > 0));
  return missing.length ? `The mirror has no ${missing.join(" or ")} for ${tag}.` : null;
}

// ---- Checking files -------------------------------------------------------------------------------------------

const ESP_MAGIC = 0xe9, CHIP_S3 = 9;
const MASLOW_URL = "https://github.com/MaslowCNC/Maslow_4\0";
// The firmware's version string sits right before the project's address in the image, early on (about 75 KB in)
const SEARCH = 512 * 1024;
const MAX_FIRMWARE = 4 * 1024 * 1024;   // larger than any OTA partition this board has; the board checks exactly

function indexOf(bytes, text, limit = bytes.length) {
  const pat = Array.from(text, (c) => c.charCodeAt(0));
  outer: for (let i = 0, n = Math.min(bytes.length, limit) - pat.length; i <= n; i++) {
    for (let j = 0; j < pat.length; j++) if (bytes[i + j] !== pat[j]) continue outer;
    return i;
  }
  return -1;
}

/**
 * A firmware file, checked before it's sent: { ok, kind: "firmware", version, errors, warnings }.
 * Errors: not an ESP32 app image (a page, a filesystem image, a zip), an image for another chip, too big.
 * Warnings: no Maslow project string (another FluidNC build, or a Maslow release older than v1.27.0), version unknown.
 */
export function checkFirmware(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const errors = [], warnings = [];
  let version = null;
  if (b.length < 64 || b[0] !== ESP_MAGIC) {
    errors.push(b[0] === 0x1f && b[1] === 0x8b ? "This is a gzipped file (a web page?), not firmware." : "This isn't an ESP32 firmware image (firmware.bin).");
  } else if ((b[12] | (b[13] << 8)) !== CHIP_S3) {
    errors.push("This firmware is for another ESP32 chip, not the Maslow board's ESP32-S3.");
  } else {
    if (b.length > MAX_FIRMWARE) errors.push("This file is bigger than the board's firmware space.");
    const at = indexOf(b, MASLOW_URL, SEARCH);
    if (at < 0) warnings.push("It doesn't say it's a Maslow build: it may be plain FluidNC, or a Maslow release older than v1.27.0.");
    else {
      // Strings are 4-byte aligned: up to 4 NULs end the version before the address
      let end = at;
      while (end > 0 && at - end < 4 && b[end - 1] === 0) end--;
      let start = end;
      while (start > 0 && b[start - 1] >= 0x20 && b[start - 1] < 0x7f && end - start < 40) start--;
      const v = String.fromCharCode(...b.subarray(start, end));
      if (/^v\d+\.\d+/.test(v)) version = v;
    }
    if (!version && !errors.length) warnings.push("Its version couldn't be read from the file.");
  }
  return { ok: !errors.length, kind: "firmware", version, errors, warnings };
}

/** A web page file (index.html.gz): gzipped, and not absurdly large for the flash. */
export function checkPage(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const errors = [];
  if (!(b[0] === 0x1f && b[1] === 0x8b)) errors.push("The page must be the gzipped index.html.gz.");
  else if (b.length > 600 * 1024) errors.push("This page is too big for the board's flash.");
  return { ok: !errors.length, kind: "page", version: null, errors, warnings: [] };
}

/** Which of the two a chosen file is meant to be: by its first bytes, then its name. */
export function fileKind(name, bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (b[0] === ESP_MAGIC) return "firmware";
  if (b[0] === 0x1f && b[1] === 0x8b) return "page";
  return /\.gz$/i.test(name) ? "page" : "firmware";
}

// ---- System status --------------------------------------------------------------------------------------------

// [ESP420] and $wifi/status are "[MSG:Name: value]" lines (SysStats.cpp, WifiConfig.cpp reportStatus). Their names
// in the page's words, in the order shown; names not listed here aren't shown.
const STATUS_NAMES = [
  ["FW version", "Firmware"], ["Chip ID", "Chip ID"], ["CPU Frequency", "Processor"], ["CPU Cores", "Cores"],
  ["CPU Temperature", "Chip temperature"], ["Free memory", "Free memory"], ["Flash Size", "Flash chip"],
  ["Available Size for LocalFS", "Flash for files"], ["SDK", "ESP-IDF"],
  ["Current WiFi Mode", "WiFi mode"], ["Connected to", "WiFi network"], ["Signal", "Signal"], ["Channel", "Channel"],
  ["IP Mode", "Address from"], ["IP", "IP address"], ["Gateway", "Gateway"], ["Mask", "Mask"], ["DNS", "DNS"],
  ["Hostname", "Name"], ["Web port", "Web port"], ["Notifications", "Notifications"]
];

/** The status lines → { name: value } (the first value of each name; [MSG:…] wrappers and "ok" dropped). */
export function parseStatusLines(text) {
  const out = {};
  for (let line of String(text || "").split(/\r?\n/)) {
    line = line.trim().replace(/^\[MSG:\s*/, "").replace(/\]$/, "").trim();
    const i = line.indexOf(": ");
    if (i < 1 || /^Mode=/.test(line)) continue;
    const k = line.slice(0, i).trim(), v = line.slice(i + 2).trim();
    if (!(k in out)) out[k] = v;
  }
  return out;
}

/** [ESP420] and $wifi/status → [[label, value]] in STATUS_NAMES order. */
export function statusRows(...texts) {
  const all = Object.assign({}, ...texts.reverse().map(parseStatusLines));
  return STATUS_NAMES.filter(([k]) => all[k]).map(([k, label]) => [label, k === "CPU Frequency" ? all[k].replace(/Mhz$/i, " MHz") : all[k]]);
}

// ---- What may run now -----------------------------------------------------------------------------------------

// Maslow states where the belts are moving (retracting, extending, taking up slack, finding anchors, releasing)
const BUSY = [1, 3, 5, 6, 8];

/** Why an update can't start now, or null. Off during a job, while moving, while disconnected, in a read-only tab. */
export function updateLock(d) {
  if (!d.connected) return "Waiting for the device to answer.";
  if (d.readOnly) return "Another tab is driving the machine; this one can only look.";
  if (d.job) return "Not while a job runs.";
  if (d.motion || BUSY.includes(d.maslow) || ["Run", "Jog", "Home"].includes(d.fluid)) return "Not while the machine moves.";
  return null;
}

// ---- The actions ----------------------------------------------------------------------------------------------

// state.update:
//   check:   null | { state: "checking" | "done" | "offline" | "failed", stream, release, newer, detail }
//   files:   [{ name, size, kind, version, errors, warnings, ok }] chosen from this computer (their bytes stay here)
//   install: null | { phase: "downloading" | "uploading" | "restarting" | "done" | "failed", step, pct, detail,
//                     expect (the version installed, if known), page (the page was replaced too), slow, version, match }
//   status:  { state: "idle" | "reading" | "ready" | "failed", rows, flash: { total, free } | null, detail }
export const UPDATE_IDLE = { check: null, files: [], install: null, status: { state: "idle", rows: [] } };

// How long after the upload the page waits for the board before saying it's slow (a restart takes 10 to 30 s)
export const SLOW_MS = 90 * 1000;

const why = (e) => (e && (e.detail || e.message)) || String(e);

/**
 * The update actions, the same for both adapters. io:
 *   releases() → GitHub's list (rejects with { offline: true } when the internet can't be reached)
 *   fetchJson(url), fetchBytes(url, onProgress(loaded, total)) → from the mirror
 *   sha256(bytes) → hex, or null when the browser can't (crypto.subtle needs https or localhost)
 *   uploadPage(blob) → resolves when /index.html.gz is written
 *   uploadFirmware(blob, onProgress(loaded, total)) → resolves when the board says the image was written
 *   status() → { esp420, wifi, flash: { total, free } | null }
 * get / set: state.update; device(): the adapter's state (for updateLock); log(line).
 */
export function updateActions({ get, set, device, log, io, timer = setTimeout }) {
  // The bytes of the chosen files stay here, not in state
  let chosen = [];
  let releaseFiles = null;   // { tag, firmware, page } once downloaded

  // quiet: the check on connect (the top bar's notice), which logs only what it found, not a missing internet
  async function checkUpdates({ stream = "release", quiet = false } = {}) {
    if (get().check?.state === "checking") return;
    set({ check: { state: "checking", stream } });
    try {
      const release = selectRelease(await io.releases(), stream);
      const current = device().fwVersion;
      set({ check: { state: "done", stream, release, newer: release ? isNewer(current, release.tag) : false } });
      if (!quiet || (release && isNewer(current, release.tag))) log(release ? `Newest ${stream} release: ${release.tag}` : `No ${stream} release found`);
    } catch (e) {
      if (e && e.offline) {
        set({ check: { state: "offline", stream, detail: why(e) } });
        if (!quiet) log("Couldn't reach GitHub to check for updates (no internet on this computer?)");
      } else {
        set({ check: { state: "failed", stream, detail: why(e) } });
        if (!quiet) log(`Couldn't check for updates: ${why(e)}`);
      }
    }
  }

  // Files chosen on this computer: each is checked at once, so the page can say what it is before anything is sent.
  // At most one firmware and one page; a new pick replaces the old one of its kind.
  async function chooseFiles({ files = [] } = {}) {
    const picked = [];
    for (const f of files) {
      const bytes = new Uint8Array(await f.arrayBuffer());
      const kind = fileKind(f.name, bytes);
      const c = kind === "page" ? checkPage(bytes) : checkFirmware(bytes);
      picked.push({ file: f, info: { name: f.name, size: f.size, ...c } });
    }
    const kinds = new Set(picked.map((p) => p.info.kind));
    chosen = [...chosen.filter((c) => !kinds.has(c.info.kind)), ...picked.filter((p, i) => picked.findIndex((q) => q.info.kind === p.info.kind) === i)];
    set({ files: chosen.map((c) => c.info), install: get().install?.phase === "failed" || get().install?.phase === "done" ? null : get().install });
  }
  function clearFiles() { chosen = []; set({ files: [] }); }

  const progress = (patch) => set({ install: { ...get().install, ...patch } });
  function fail(detail, step) {
    progress({ phase: "failed", detail, step });
    log(`Update failed: ${detail}`);
  }

  // Download the release's firmware and page from the mirror and check them against its manifest
  async function download(release) {
    if (releaseFiles?.tag === release.tag) return releaseFiles;
    progress({ phase: "downloading", step: "Reading the release's file list", pct: 0 });
    const manifest = await io.fetchJson(mirrorUrl(release.tag, "manifest.json"));
    const bad = manifestError(manifest, release.tag);
    if (bad) throw new Error(bad);
    const names = ["firmware.bin", ...(manifest.files["index.html.gz"]?.size_bytes > 0 ? ["index.html.gz"] : [])];
    const total = names.reduce((n, f) => n + manifest.files[f].size_bytes, 0);
    let done = 0;
    const out = { tag: release.tag };
    for (const name of names) {
      progress({ step: `Downloading ${name}` });
      const bytes = await io.fetchBytes(mirrorUrl(release.tag, name), (loaded) => progress({ pct: Math.round(100 * (done + loaded) / total) }));
      const want = manifest.files[name];
      if (bytes.length !== want.size_bytes) throw new Error(`${name} arrived with ${bytes.length} bytes, not ${want.size_bytes}. Try again.`);
      const sum = want.sha256 ? await io.sha256(bytes) : null;
      if (sum && sum !== want.sha256) throw new Error(`${name} didn't arrive intact (its checksum is wrong). Try again.`);
      done += want.size_bytes;
      out[name === "firmware.bin" ? "firmware" : "page"] = bytes;
    }
    const c = checkFirmware(out.firmware);
    if (!c.ok) throw new Error(`The release's firmware.bin: ${c.errors[0]}`);
    releaseFiles = out;
    return out;
  }

  // Send the page, then the firmware (which restarts the board). source: "release" (the checked release) or "files".
  async function install({ source } = {}) {
    const lock = updateLock(device());
    if (lock) { log(`Not done: firmware update (${lock})`); set({ install: { phase: "failed", detail: lock } }); return; }
    if (["downloading", "uploading", "restarting"].includes(get().install?.phase)) return;
    let firmware, page, expect = null;
    set({ install: { phase: "downloading", step: "", pct: 0 } });
    try {
      if (source === "release") {
        const release = get().check?.release;
        if (!release) throw new Error("Check for updates first.");
        log(`Updating to ${release.tag}`);
        const f = await download(release);
        firmware = new Blob([f.firmware]);
        page = f.page ? new Blob([f.page]) : null;
        expect = release.tag;
      } else {
        const fw = chosen.find((c) => c.info.kind === "firmware"), pg = chosen.find((c) => c.info.kind === "page");
        const bad = chosen.find((c) => !c.info.ok);
        if (bad) throw new Error(`${bad.info.name}: ${bad.info.errors[0]}`);
        if (!fw && !pg) throw new Error("Choose a file first.");
        firmware = fw?.file || null;
        page = pg?.file || null;
        expect = fw?.info.version || null;
        log(`Updating from ${[fw?.info.name, pg?.info.name].filter(Boolean).join(" and ")}`);
      }
    } catch (e) {
      fail(why(e), "Nothing was changed on the board");
      return;
    }
    // Something may have started while the files downloaded (another tab, a phone)
    const late = updateLock(device());
    if (late) { fail(late); return; }
    let wrotePage = false;
    try {
      if (page) {
        progress({ phase: "uploading", step: "Writing the page (index.html.gz)", pct: 0 });
        await io.uploadPage(page);
        wrotePage = true;
        log("Page written: index.html.gz");
      }
      if (!firmware) {
        progress({ phase: "done", step: "Page written", pct: 100, page: !!page, version: device().fwVersion, match: null });
        return;
      }
      progress({ phase: "uploading", step: "Writing the firmware", pct: 0, page: !!page });
      await io.uploadFirmware(firmware, (loaded, total) => progress({ pct: Math.round(100 * loaded / (total || firmware.size)) }));
    } catch (e) {
      fail(why(e), wrotePage ? "The new page was written, but not the firmware" : "Nothing was changed on the board");
      return;
    }
    log("Firmware written. The board is restarting.");
    progress({ phase: "restarting", step: "Restarting", pct: 100, expect, since: Date.now(), slow: false });
    const at = get().install.since;
    timer(() => { if (get().install?.phase === "restarting" && get().install.since === at) progress({ slow: true }); }, SLOW_MS);
  }

  // The adapter calls this on each connection once [ESP800] has been read: the end of a restart after an update
  function connected(version) {
    const i = get().install;
    if (i?.phase !== "restarting") return;
    const match = i.expect ? sameVersion(version, i.expect) : null;
    progress({ phase: "done", step: "Back", version, match });
    log(match === false ? `The board came back on ${version}, not ${i.expect}` : `The board is back on ${version || "an unknown version"}`);
    // The check's "newer" was about the version that was running
    const c = get().check;
    if (c?.release) set({ check: { ...c, newer: isNewer(version, c.release.tag) } });
  }

  function dismissInstall() {
    if (["downloading", "uploading", "restarting"].includes(get().install?.phase)) return;
    set({ install: null });
  }

  async function readStatus() {
    if (get().status?.state === "reading") return;
    set({ status: { ...get().status, state: "reading", detail: null } });
    try {
      const r = await io.status();
      set({ status: { state: "ready", rows: statusRows(r.esp420, r.wifi || ""), flash: r.flash || null, at: Date.now() } });
    } catch (e) {
      log(`Couldn't read the system status: ${why(e)}`);
      set({ status: { ...get().status, state: "failed", detail: why(e) } });
    }
  }

  return {
    checkUpdates, chooseUpdateFiles: chooseFiles, clearUpdateFiles: clearFiles, installUpdate: install,
    dismissUpdate: dismissInstall, readStatus, updateConnected: connected
  };
}
