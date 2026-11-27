// The tool library: saved bits, kept on the board's flash as /tools.json, so every connection sees the same list
// (REWRITE.md part 2, "Tool library"). Both device adapters read it on connect and write it through the same /files
// upload the board file manager uses (device/flashfiles.js). Lengths are always mm here; the screen converts (units.js).
//
// The file: { "version": 1, "tools": [{ "id", "name", "type", "diameter", "stickout", "notes" }] }
// A missing file is an empty library. A file that isn't this shape is read as far as it can be: bad entries are
// dropped, and the library says once that the file had problems (no error per entry).

export const TOOLS_PATH = "/tools.json";
export const TOOL_TYPES = [["endmill", "End mill"], ["vbit", "V-bit"], ["ballnose", "Ball nose"], ["drill", "Drill"], ["other", "Other"]];
export const typeName = (t) => (TOOL_TYPES.find(([id]) => id === t) || TOOL_TYPES[4])[1];
export const MAX_NAME = 40, MAX_NOTES = 200, MAX_DIAMETER = 50, MAX_TOOLS = 100;

const num = (v) => (typeof v === "number" ? v : typeof v === "string" && v.trim() ? Number(v) : NaN);

// One entry from the file → a tool, or null if it can't be used (no name, or no usable stick-out or diameter)
function readTool(t, i) {
  if (!t || typeof t !== "object") return null;
  const name = typeof t.name === "string" ? t.name.trim().slice(0, MAX_NAME) : "";
  const diameter = num(t.diameter), stickout = num(t.stickout);
  if (!name || !(diameter > 0) || !(stickout > 0)) return null;
  return {
    id: typeof t.id === "string" && t.id ? t.id : `t${i + 1}`,
    name, type: TOOL_TYPES.some(([id]) => id === t.type) ? t.type : "other",
    diameter, stickout, notes: typeof t.notes === "string" ? t.notes.slice(0, MAX_NOTES) : ""
  };
}

/**
 * The file's text (or null when there is no file) → { tools, problem }. problem is null, or one sentence for the
 * library to show: the file isn't JSON, or some entries couldn't be read.
 */
export function parseTools(text) {
  if (text == null || !String(text).trim()) return { tools: [], problem: null };
  let j;
  try { j = JSON.parse(text); } catch { return { tools: [], problem: "tools.json on the board isn't valid JSON, so the library is empty. Saving a bit replaces it." }; }
  const list = Array.isArray(j) ? j : Array.isArray(j?.tools) ? j.tools : null;
  if (!list) return { tools: [], problem: "tools.json on the board has no list of tools, so the library is empty. Saving a bit replaces it." };
  const tools = [], ids = new Set();
  list.forEach((t, i) => {
    const r = readTool(t, i);
    if (!r) return;
    while (ids.has(r.id)) r.id += "x";
    ids.add(r.id);
    tools.push(r);
  });
  const dropped = list.length - tools.length;
  return { tools, problem: dropped ? `${dropped} ${dropped === 1 ? "entry" : "entries"} in tools.json couldn't be read and ${dropped === 1 ? "was" : "were"} left out. Saving rewrites the file without ${dropped === 1 ? "it" : "them"}.` : null };
}

export const serializeTools = (tools) => JSON.stringify({ version: 1, tools: tools.map(({ id, name, type, diameter, stickout, notes }) =>
  ({ id, name, type, diameter, stickout, ...(notes ? { notes } : {}) })) }, null, 2) + "\n";

/**
 * What's wrong with a tool being saved, per field ({ name, diameter, stickout, notes }), or null if it's fine.
 * maxStickout: the Set Z step's limit (SEAT_TO_WORK): a longer stick-out would put the tip below the work.
 */
export function toolErrors(t, tools, maxStickout) {
  const e = {};
  const name = String(t.name ?? "").trim();
  if (!name) e.name = "Type a name.";
  else if (name.length > MAX_NAME) e.name = `Up to ${MAX_NAME} characters.`;
  else if (tools.some((o) => o.id !== t.id && o.name.toLowerCase() === name.toLowerCase())) e.name = "A bit with this name is already saved.";
  if (!(t.diameter > 0 && t.diameter <= MAX_DIAMETER)) e.diameter = `From 0 to ${MAX_DIAMETER} mm.`;
  if (!(t.stickout > 0 && t.stickout < maxStickout)) e.stickout = `From 0 to ${maxStickout} mm (the Set Z step's limit).`;
  if (String(t.notes ?? "").length > MAX_NOTES) e.notes = `Up to ${MAX_NOTES} characters.`;
  return Object.keys(e).length ? e : null;
}

// An id not used yet: short, so the file stays small
export function newId(tools) {
  const ids = new Set(tools.map((t) => t.id));
  let n = tools.length + 1;
  while (ids.has(`t${n}`)) n++;
  return `t${n}`;
}

// Saving or deleting applies to the list as the board has it now (read again first), so another connection's edit
// made since this page read it isn't lost
export function applyEdit(tools, edit) {
  if (edit.remove) return tools.filter((t) => t.id !== edit.remove);
  const t = { ...edit.tool, name: String(edit.tool.name).trim(), notes: String(edit.tool.notes ?? "").trim() };
  if (!t.id) return [...tools, { ...t, id: newId(tools) }];
  return tools.some((o) => o.id === t.id) ? tools.map((o) => (o.id === t.id ? t : o)) : [...tools, t];
}

// The stick-outs typed at the Set Z step, newest first, at most `max`, each once (to a hundredth of a mm)
export const RECENT_STICKOUTS = 5;
export function addRecent(list, mm, max = RECENT_STICKOUTS) {
  if (!(mm > 0)) return list || [];
  const same = (a) => Math.abs(a - mm) < 0.005;
  return [mm, ...(list || []).filter((v) => v > 0 && !same(v))].slice(0, max);
}
// Stored list → numbers, whatever was stored
export function readRecent(text) {
  try { const j = JSON.parse(text); return Array.isArray(j) ? j.filter((v) => typeof v === "number" && v > 0).slice(0, RECENT_STICKOUTS) : []; }
  catch { return []; }
}

/**
 * The Set Z step's suggestions: the saved bits' stick-outs with their names, then the last values typed (newest first)
 * that aren't already a bit's. Each is { mm, label, toolId? }; fmt(mm) shows it in the chosen unit.
 */
export function stickoutSuggestions(tools, recent, fmt) {
  const out = (tools || []).map((t) => ({ mm: t.stickout, label: t.name, toolId: t.id }));
  for (const mm of recent || []) {
    if (!out.some((s) => Math.abs(s.mm - mm) < 0.005)) out.push({ mm, label: "Typed before" });
  }
  return out.map((s) => ({ ...s, text: fmt(s.mm) }));
}

// The suggestion a box's text matches, so filling one in (shown rounded, in inches) keeps its exact mm
export const matchSuggestion = (text, suggestions) => suggestions.find((s) => s.text === String(text).trim()) || null;

// state.tools: { status: "idle" | "reading" | "ready" | "saving" | "failed", list, problem, detail }
export const TOOLS_IDLE = { status: "idle", list: [], problem: null, detail: null };

// readTools and saveTool / deleteTool for both device adapters. io: read(path) → text (throws on a missing file),
// upload(path, text) → the /files answer. lock() says why the board's files can't be changed now, or null.
export function toolActions({ get, set, log, io, lock = () => null, failed = () => null }) {
  const why = (e) => (e && e.message) || String(e);
  async function fetchList() {
    let text = null;
    try { text = await io.read(TOOLS_PATH); }
    catch (e) { if (!/404|not found/i.test(why(e))) throw e; }   // no file yet: an empty library
    return parseTools(text);
  }
  async function readTools() {
    set({ status: get().status === "ready" ? "ready" : "reading", detail: null });
    try {
      const r = await fetchList();
      set({ status: "ready", list: r.tools, problem: r.problem, detail: null });
    } catch (e) {
      // Quietly: the library is optional, and the page goes on without it
      set({ status: "failed", detail: `Couldn't read the tool library: ${why(e)}` });
    }
  }
  async function write(edit, what) {
    const r = lock();
    if (r) { set({ detail: r }); return false; }
    set({ status: "saving", detail: null });
    try {
      const cur = await fetchList().catch(() => ({ tools: get().list }));
      const list = applyEdit(cur.tools, edit);
      if (list.length > MAX_TOOLS) throw new Error(`the library holds up to ${MAX_TOOLS} bits`);
      const answer = await io.upload(TOOLS_PATH, serializeTools(list));
      const bad = failed(answer);
      if (bad) throw new Error(bad);
      set({ status: "ready", list, problem: null, detail: null });
      log(what);
      return true;
    } catch (e) {
      set({ status: "ready", detail: `Not saved: ${why(e)}` });
      log(`Tool library not saved: ${why(e)}`);
      return false;
    }
  }
  return {
    readTools,
    saveTool: ({ tool }) => write({ tool }, `Tool library: ${tool.id ? "saved" : "added"} ${String(tool.name).trim()}`),
    deleteTool: ({ id }) => write({ remove: id }, `Tool library: deleted ${get().list.find((t) => t.id === id)?.name || id}`)
  };
}
