import { useEffect, useState } from "preact/hooks";
import { TOOL_TYPES, typeName, toolErrors, MAX_NAME, MAX_NOTES } from "../device/tools.js";
import { SEAT_TO_WORK } from "../flow/screens.js";
import { editText, editedMm, fmtU, numMode, prose, unitName } from "../units.js";

// Settings › Tool and Z › Tool library: the saved bits (device/tools.js), on the board as /tools.json so every
// connection sees the same list. Add, Edit (one at a time, below the list or in place) and Delete (asks first).
// Re-read when the section opens, so another connection's edits show.
export function ToolLibrary({ ctx, busy }) {
  const { d, send } = ctx, u = ctx.units;
  const lib = d.tools || { status: "idle", list: [] };
  const [edit, setEdit] = useState(null);        // a tool being edited, or { } for a new one
  const [del, setDel] = useState(null);          // the id asking to be deleted
  useEffect(() => { if (d.connected) send("readTools"); }, [d.connected]);
  const saving = lib.status === "saving", off = busy || saving || !d.connected;
  return <div class="tlib">
    <div class="tlhead"><b>Tool library</b><small>Saved on the board, so every browser and phone sees the same bits. Pick the loaded one at Set Z: its stick-out shows, and Set Z uses it.</small></div>
    {lib.problem && <p class="note alert">{lib.problem}</p>}
    {lib.detail && <p class="note alert" role="status">{lib.detail}</p>}
    {lib.status === "reading" && !lib.list.length && <p class="note">Reading…</p>}
    {lib.status !== "reading" && !lib.list.length && !edit && <p class="note">No bits saved yet.</p>}
    <ul class="tlist">{lib.list.map(t => edit?.id === t.id
      ? <li><ToolForm key={t.id} tool={t} list={lib.list} unit={u} off={off} onCancel={() => setEdit(null)}
          onSave={tool => Promise.resolve(send("saveTool", { tool })).then(ok => ok && setEdit(null))} /></li>
      : <li class="trow">
          <span class="tinfo"><b>{t.name}{ctx.toolId === t.id && <span class="ttag" title="The bit last picked at Set Z in this browser">last used</span>}</b>
            <span class="mono">{typeName(t.type)} · ⌀ {fmtU(t.diameter, u)} · stick-out {fmtU(t.stickout, u)}</span>
            {t.notes && <small>{t.notes}</small>}</span>
          <span class="tacts">
            <button class="btn ghost" type="button" disabled={off} onClick={() => { setDel(null); setEdit(t); }}>Edit</button>
            <button class="btn ghost fdanger" type="button" disabled={off} onClick={() => { setEdit(null); setDel(t.id); }}>Delete</button></span>
          {del === t.id && <div class="fpanel warn" role="alertdialog" aria-label={`Delete ${t.name}?`}>
            <p><b>Delete {t.name}?</b> It's removed from the board for every connection.</p>
            <span class="frename"><button class="btn danger" type="button" disabled={off}
              onClick={() => Promise.resolve(send("deleteTool", { id: t.id })).then(() => { setDel(null); if (ctx.toolId === t.id) ctx.setToolId(""); })}>Delete</button>
              <button class="btn ghost" type="button" onClick={() => setDel(null)}>Cancel</button></span></div>}
        </li>)}</ul>
    {edit && !edit.id
      ? <ToolForm tool={{}} list={lib.list} unit={u} off={off} onCancel={() => setEdit(null)}
          onSave={tool => Promise.resolve(send("saveTool", { tool })).then(ok => ok && setEdit(null))} />
      : !edit && <div><button class="btn ghost" type="button" disabled={off} title={!d.connected ? "Waiting for the device to answer." : ""}
          onClick={() => { setDel(null); setEdit({}); }}>Add a bit</button></div>}
    {saving && <p class="note" role="status">Saving to the board…</p>}
  </div>;
}

function ToolForm({ tool, list, unit: u, off, onSave, onCancel }) {
  const [name, setName] = useState(tool.name || "");
  const [type, setType] = useState(tool.type || "endmill");
  const [dia, setDia] = useState(editText(tool.diameter, u));
  const [stick, setStick] = useState(editText(tool.stickout, u));
  const [notes, setNotes] = useState(tool.notes || "");
  const [tried, setTried] = useState(false);
  const t = { id: tool.id, name, type, notes, diameter: editedMm(dia, tool.diameter, u), stickout: editedMm(stick, tool.stickout, u) };
  const err = toolErrors(t, list, SEAT_TO_WORK);
  // Field errors show once Save was tried, or as soon as that field has text
  const show = (f, text) => err?.[f] && (tried || text) ? prose(err[f], u) : null;
  const F = ({ label, f, text, children }) => <label class="tfield"><span>{label}</span>{children}
    {show(f, text) && <small class="bad">{show(f, text)}</small>}</label>;
  return <form class="fpanel tform" aria-label={tool.id ? `Edit ${tool.name}` : "Add a bit"} onSubmit={e => { e.preventDefault(); setTried(true); if (!err && !off) onSave(t); }}>
    <b>{tool.id ? `Edit ${tool.name}` : "Add a bit"}</b>
    <div class="tgrid">
      {F({ label: "Name", f: "name", text: name, children: <input value={name} maxlength={MAX_NAME} onInput={e => setName(e.currentTarget.value)} aria-invalid={!!show("name", name)} /> })}
      <label class="tfield"><span>Type</span><select value={type} onChange={e => setType(e.currentTarget.value)}>
        {TOOL_TYPES.map(([id, n]) => <option value={id}>{n}</option>)}</select></label>
      {F({ label: `Diameter (${u})`, f: "diameter", text: dia, children: <input class="mono" value={dia} inputmode={numMode(u)} onInput={e => setDia(e.currentTarget.value)} aria-label={`Diameter in ${unitName(u)}`} aria-invalid={!!show("diameter", dia)} /> })}
      {F({ label: `Stick-out (${u})`, f: "stickout", text: stick, children: <input class="mono" value={stick} inputmode={numMode(u)} onInput={e => setStick(e.currentTarget.value)} aria-label={`Stick-out in ${unitName(u)}`} aria-invalid={!!show("stickout", stick)} /> })}
    </div>
    <label class="tfield"><span>Notes (optional)</span><input value={notes} maxlength={MAX_NOTES} onInput={e => setNotes(e.currentTarget.value)} /></label>
    <small class="note">Stick-out: from the spindle socket face to the tip, as the Set Z step asks.</small>
    <span class="frename"><button class="btn" type="submit" disabled={off}>{tool.id ? "Save" : "Add"}</button>
      <button class="btn ghost" type="button" onClick={onCancel}>Cancel</button></span>
  </form>;
}
