// The board's own files: its flash filesystem (LittleFS, 2 MB), separate from the SD card's job files. It holds the
// page (index.html.gz), the machine's configuration (maslow.yaml) and a few small files. Settings › Advanced › Files
// (ui/files.jsx) lists them; both device adapters (machine.js, simulator.js) keep the listing in state.flash.
//
// The firmware's /files endpoint (firmware/FluidNC/src/WebUI/WebUIServer.cpp, handleFileOps), as the current UI's
// SPIFFSdlg.js uses it:
//   GET  /files?path=/dir                                           list a folder
//   GET  /files?path=/dir&action=delete|deletedir|createdir&filename=x
//   GET  /files?path=/dir&action=rename&filename=x&newname=y        both names inside /dir
//   POST /files  form data: path=/dir/, "/dir/xS"=size, myfile[]=the file named "/dir/x"   upload (replaces x)
// Every answer is the folder's listing: {"files":[{name, size}], "path", "total", "used", "occupation", "status"}.
// size is -1 for a folder; total and used are formatted ("1.88 MB"); status is "Ok", "x deleted", "Cannot …" or
// "Upload failed". A file is read with a plain GET of its path (/maslow.yaml), which the board sends as a download.

// Files the board needs, at the top of the flash, and what breaks without them
export const PROTECTED = {
  "/index.html.gz": "the page you're using now. Without it the board serves no page: open /?forcefallback=yes for FluidNC's basic file manager to upload one again.",
  "/maslow.yaml": "the machine's configuration (frame, belts, motors, spindle board). Without a good copy the machine starts unconfigured after its next restart, and won't move until one is uploaded."
};
export const protectedWhy = (path) => PROTECTED[path] || null;

// Files the editor opens as text, and the largest it opens (the board's config is about 7 KB)
const TEXT = /\.(ya?ml|json|txt|cfg|ini|md|bak)$/i;
export const MAX_EDIT = 64 * 1024;
export const isEditable = (f) => !f.dir && TEXT.test(f.name) && !/\.gz\.bak$/i.test(f.name) && f.size <= MAX_EDIT;
// The machine's config: saving it offers a restart
export const isConfig = (path) => /^\/[^/]+\.ya?ml$/i.test(path) && !/\.bak$/i.test(path);

// Paths: always absolute; a folder's path ends without a slash, except the top ("/")
export const joinPath = (dir, name) => `${dir === "/" ? "" : dir}/${name}`;
export const parentPath = (path) => path.replace(/\/[^/]*$/, "") || "/";
export const baseName = (path) => path.split("/").pop();
// The breadcrumbs for a folder: [["/", "/"], ["/a", "a"], ["/a/b", "b"]]
export function crumbs(dir) {
  const out = [["/", "Flash"]];
  let p = "";
  for (const part of dir.split("/").filter(Boolean)) { p += `/${part}`; out.push([p, part]); }
  return out;
}

// "1.88 MB" → bytes; null if it isn't one of formatBytes()'s forms (NutsBolts.cpp)
export function parseBytes(text) {
  const m = String(text || "").trim().match(/^([\d.]+)\s*(B|KB|MB|GB)$/i);
  if (!m) return null;
  return Math.round(Number(m[1]) * 1024 ** ["B", "KB", "MB", "GB"].indexOf(m[2].toUpperCase()));
}
export function formatSize(bytes) {
  if (bytes == null || bytes < 0) return "";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(bytes < 10240 ? 1 : 0)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
}

// The firmware's answer (text or parsed) → { path, files, total, used, free, status, failed }. Folders first, then
// files, each by name.
export function parseFileList(answer) {
  const j = typeof answer === "string" ? JSON.parse(answer) : answer || {};
  const files = (j.files || []).map((f) => {
    const size = Number(f.size);
    return size < 0 ? { name: f.name, dir: true, size: null } : { name: f.name, dir: false, size };
  }).sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name, undefined, { sensitivity: "base" }));
  const total = parseBytes(j.total), used = parseBytes(j.used);
  const path = `/${String(j.path || "").replace(/^\/+|\/+$/g, "")}`;
  const status = String(j.status || "");
  return {
    path, files, total, used, free: total != null && used != null ? Math.max(0, total - used) : null,
    status, failed: statusFailed(status) ? status : null
  };
}
// The status of an action that didn't work: "Cannot delete x …", "Upload failed", "Missing new filename", "No SD card"
export const statusFailed = (status) => /cannot|fail|missing|error|no sd/i.test(String(status || ""));

// The query for a /files request in a folder; the firmware wants the folder with a trailing slash
export function filesQuery(dir, action, filename, newname) {
  const q = { path: dir === "/" ? "/" : `${dir}/` };
  if (action) Object.assign(q, { action, filename });
  if (newname != null) q.newname = newname;
  return new URLSearchParams(q).toString();
}
// A flash file's address for reading or downloading: each part of the path encoded
export const fileUrl = (path, prefix = "") => `${prefix}${path.split("/").map(encodeURIComponent).join("/")}`;

// Why a new name (a rename, a new folder, an upload) can't be used in this folder, or null
export function nameError(name, files, { except } = {}) {
  const n = String(name ?? "");
  if (!n.trim()) return "Type a name.";
  if (n !== n.trim()) return "No spaces at the start or end.";
  if (/[\/\\]/.test(n)) return "A name can't contain / or \\.";
  if (n === "." || n === "..") return "Not a usable name.";
  if (/[\x00-\x1f"*:<>?|]/.test(n)) return "Use letters, numbers, - _ . and spaces.";
  if (n.length > 60) return "Up to 60 characters.";
  if (n !== except && files.some((f) => f.name === n))
    return `${n} already exists here. Rename or delete it first: a rename onto a name that's taken may fail, or replace that file.`;
  return null;
}

// Room for a file of this size, replacing one of the same name if there is one. LittleFS writes whole 4 KB
// blocks and keeps some for itself, so a little more is asked for than the file's size.
const SLACK = 2 * 4096;
export function hasRoom(listing, bytes, replacing = 0) {
  if (!listing || listing.free == null) return true;   // unknown: let the board refuse it
  return listing.free + (replacing || 0) >= bytes + SLACK;
}

// The copy kept before the first save of a file, so a bad save can be undone from the board too
export const backupName = (name) => `${name}.bak`;

// Problems in a YAML config before it's saved: errors block the save, warnings don't. FluidNC's config is plain
// "name: value" lines nested by two spaces; it doesn't accept tabs.
export function checkYaml(text) {
  const errors = [], warnings = [];
  if (!String(text).trim()) errors.push("The file is empty. The machine would start unconfigured.");
  const lines = String(text).split(/\r?\n/);
  const tabs = lines.map((l, i) => (/^\s*\t/.test(l) ? i + 1 : 0)).filter(Boolean);
  if (tabs.length) errors.push(`Tabs used for indenting on line${tabs.length > 1 ? "s" : ""} ${tabs.slice(0, 5).join(", ")}${tabs.length > 5 ? "…" : ""}. Use spaces.`);
  const odd = [];
  lines.forEach((l, i) => {
    if (!l.trim() || /^\s*#/.test(l) || /^\s*\t/.test(l)) return;
    if (!/^\s*(- )?[^\s:#][^:]*:(\s|$)/.test(l) && !/^\s*- /.test(l)) odd.push(i + 1);
  });
  if (odd.length) warnings.push(`Line${odd.length > 1 ? "s" : ""} ${odd.slice(0, 5).join(", ")}${odd.length > 5 ? "…" : ""} ${odd.length > 1 ? "don't" : "doesn't"} look like "name: value".`);
  return { errors, warnings };
}


// state.flash: the folder shown and its listing, what's being done (busy), the last result (notice), and the file
// open in the editor: { path, status: "reading" | "ready" | "saving" | "saved" | "restored" | "failed", text (as the
// board has it), original (as it was when opened), backup (the .bak name, once saved), restart, detail, noRoom }
export const FLASH_IDLE = { status: "idle", path: "/", files: [], free: null, total: null, busy: null, notice: null, detail: null, editor: null };

// The file actions, the same for both device adapters. Each adapter passes how it reaches the files (io):
//   list(dir) and act(query) → the firmware's answer; upload(path, blob | text) → the answer;
//   read(path) → the text; download(path, name) saves the file on this computer.
// lock() says why changes can't be made now (a job, motion, no connection), or null; it's checked again here, not
// only by the disabled buttons.
export function flashActions({ get, set, log, io, lock = () => null }) {
  const why = (e) => (e && e.message) || String(e);
  const show = (l, notice) => set({
    status: "ready", path: l.path, files: l.files, free: l.free, total: l.total, busy: null, detail: null,
    notice: notice === undefined ? null : notice
  });
  const refused = (what) => {
    const r = lock();
    if (r) { log(`Not done: ${what} (${r})`); set({ notice: { bad: true, text: r } }); }
    return r;
  };

  async function list(dir = get().path) {
    set({ status: "reading", detail: null });
    try {
      const l = parseFileList(await io.list(dir));
      if (l.failed) throw new Error(l.failed);
      show(l, get().notice);
    } catch (e) {
      // A folder that has gone (deleted from another tab): go back to the top
      if (dir !== "/") return list("/");
      log(`Couldn't list the board's files: ${why(e)}`);
      set({ status: "failed", detail: why(e), busy: null });
    }
  }

  // delete, deletedir, createdir, rename, in the folder shown
  async function act({ action, name, newname }) {
    const dir = get().path;
    const what = { delete: `delete ${name}`, deletedir: `delete the folder ${name}`, createdir: `create the folder ${name}`, rename: `rename ${name} to ${newname}` }[action];
    if (!what || refused(what)) return;
    if (action === "createdir" || action === "rename") {
      const bad = nameError(action === "rename" ? newname : name, get().files);
      if (bad) { set({ notice: { bad: true, text: bad } }); return; }
    }
    set({ busy: what.replace(/^\w/, (c) => c.toUpperCase()) + "…", notice: null });
    try {
      const l = parseFileList(await io.act(filesQuery(dir, action, name, action === "rename" ? newname : undefined)));
      if (l.failed) throw new Error(l.failed);
      log(`Board files: ${l.status}`);
      show(l, { text: `${l.status}.` });
    } catch (e) {
      log(`Couldn't ${what}: ${why(e)}`);
      set({ busy: null, notice: { bad: true, text: `Couldn't ${what}: ${why(e)}` } });
      await list(dir);
    }
  }

  // Send a file (a File or Blob, or text) to path, checking the room first. Returns the new listing; throws if it failed.
  async function put(path, content) {
    const size = typeof content === "string" ? new TextEncoder().encode(content).length : content.size;
    const old = get().path === parentPath(path) ? get().files.find((f) => f.name === baseName(path)) : null;
    if (!hasRoom(get(), size, old?.size)) throw new Error(`Not enough room on the board for ${baseName(path)} (${formatSize(size)}, ${formatSize(get().free)} free)`);
    const l = parseFileList(await io.upload(path, content));
    if (l.failed) throw new Error(l.failed);
    return l;
  }

  async function upload({ file, name = file?.name }) {
    if (!file || refused(`upload ${name}`)) return;
    const bad = nameError(name, [], {});
    if (bad) { set({ notice: { bad: true, text: bad } }); return; }
    set({ busy: `Uploading ${name}…`, notice: null });
    try {
      const l = await put(joinPath(get().path, name), file);
      log(`Board files: ${name} uploaded`);
      show(l, { text: `${name} uploaded.` });
    } catch (e) {
      log(`Upload of ${name} to the board failed: ${why(e)}`);
      set({ busy: null, notice: { bad: true, text: `Upload of ${name} failed: ${why(e)}` } });
      await list();
    }
  }

  function download({ name }) {
    const path = joinPath(get().path, name);
    try { io.download(path, name); log(`Board files: downloading ${name}`); } catch (e) { set({ notice: { bad: true, text: `Couldn't download ${name}: ${why(e)}` } }); }
  }

  const setEditor = (patch) => set({ editor: { ...get().editor, ...patch } });

  async function open({ name }) {
    const path = joinPath(get().path, name);
    set({ editor: { path, status: "reading" }, notice: null });
    try {
      const text = await io.read(path);
      if (typeof text !== "string") throw new Error("not a text file");
      set({ editor: { path, status: "ready", text, original: text, backup: null } });
    } catch (e) {
      log(`Couldn't open ${name}: ${why(e)}`);
      set({ editor: { path, status: "failed", detail: `Couldn't open ${name}: ${why(e)}`, text: null } });
    }
  }

  // Save the editor's text. Before the first save, the file as it was goes to <name>.bak (if there's room; without
  // room, only when the user says to save without it). The original also stays in the page, for Undo.
  async function save({ text, withoutBackup = false }) {
    const ed = get().editor;
    if (!ed || ed.text == null) return;
    const name = baseName(ed.path);
    if (refused(`save ${name}`)) return;
    if (/\.ya?ml$/i.test(name) && checkYaml(text).errors.length) { setEditor({ status: "failed", detail: checkYaml(text).errors[0] }); return; }
    setEditor({ status: "saving", detail: null, noRoom: false });
    let backup = ed.backup;
    try {
      if (!backup && !/\.bak$/i.test(name) && ed.original !== text) {
        const bak = joinPath(parentPath(ed.path), backupName(name));
        const old = get().files.find((f) => f.name === backupName(name));
        const size = new TextEncoder().encode(ed.original).length;
        if (hasRoom(get(), size, old?.size)) {
          await put(bak, ed.original);
          backup = backupName(name);
          log(`Board files: kept the original as ${backup}`);
        } else if (!withoutBackup) {
          setEditor({ status: "failed", noRoom: true, detail: `There's no room on the board for a backup copy (${backupName(name)}). The original stays in this page until you close the file, so Undo still works.` });
          return;
        }
      }
      const l = await put(ed.path, text);
      log(`Board files: ${name} saved`);
      show(l, get().notice);
      setEditor({ status: "saved", text, backup, restart: isConfig(ed.path) });
    } catch (e) {
      log(`Couldn't save ${name}: ${why(e)}`);
      setEditor({ status: "failed", backup, detail: `Couldn't save ${name}: ${why(e)}` });
    }
  }

  // Undo: put the file back as it was when it was opened
  async function restore() {
    const ed = get().editor;
    if (!ed || ed.original == null || refused(`restore ${baseName(ed.path)}`)) return;
    setEditor({ status: "saving", detail: null });
    try {
      const l = await put(ed.path, ed.original);
      log(`Board files: ${baseName(ed.path)} put back as it was`);
      show(l, get().notice);
      setEditor({ status: "restored", text: ed.original, restart: isConfig(ed.path) });
    } catch (e) {
      setEditor({ status: "failed", detail: `Couldn't put the original back: ${why(e)}` });
    }
  }

  return {
    listFlash: ({ path } = {}) => list(path),
    flashAction: act, uploadFlash: upload, downloadFlash: download,
    openFlashFile: open, saveFlashFile: save, restoreFlashFile: restore,
    closeFlashFile: () => set({ editor: null })
  };
}
