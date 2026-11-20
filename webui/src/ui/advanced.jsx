import { useState, useEffect } from "preact/hooks";
import { SCREENS } from "../flow/screens.js";
import { LISTS, grouped, valueError, isChanged, pendingChanges } from "../device/settingslist.js";
import { FilesTab } from "./files.jsx";
import "./advanced.css";

// Settings › Advanced: tabs along the top for the support tools (the command box and belt motor test, passed in as
// children) and the raw Maslow and FluidNC settings lists. Tabs rather than more entries in the Settings nav, so it
// works the same in the desktop pop-up and the phone's, where the section nav is already a scrolling row of pills.
// Files is the board's flash files (ui/files.jsx): next to the settings lists, since editing maslow.yaml as text is
// the other way to change them.
const TABS = [["tools", "Tools"], ["maslow", "Maslow"], ["fluidnc", "FluidNC"], ["files", "Files"]];

export function AdvancedSection({ k, ctx, children }) {
  const { d, send } = ctx;
  const [tab, setTab] = useState("tools");
  const [filter, setFilter] = useState("");   // shared by both lists
  const lists = tab === "maslow" || tab === "fluidnc";
  // Enter in the filter reads every setting from the board again. Edits not saved yet are kept.
  const reread = (e) => {
    if (e.key !== "Enter") return;
    e.preventDefault();
    if (d.connected && d.settingsList?.status !== "reading") send("readSettingsList");
  };
  const pick = (e) => {   // arrow keys move between the tabs, as a tablist should
    const i = TABS.findIndex(([id]) => id === tab), n = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
    if (n) { const next = TABS[(i + n + TABS.length) % TABS.length][0]; setTab(next); e.currentTarget.querySelector(`#adv-${next}`)?.focus(); }
  };
  return <>
    <h4>Advanced</h4>
    <div class="advbar">
      <div class="subtabs" role="tablist" aria-label="Advanced" onKeyDown={pick}>
        {TABS.map(([id, label]) => <button type="button" role="tab" id={`adv-${id}`} aria-selected={id === tab} aria-controls="adv-panel"
          tabindex={id === tab ? 0 : -1} onClick={() => setTab(id)}>{label}</button>)}
      </div>
      {lists && <label class="rawfilter" title="Filter by name or value. Enter reads the settings from the board again.">
        <span class="sr">Filter {LISTS[tab].label} settings. Enter reads them from the board again.</span>
        <input type="search" placeholder="Filter · Enter to re-read" value={filter}
          onInput={(e) => setFilter(e.currentTarget.value)} onKeyDown={reread} /></label>}
    </div>
    <div id="adv-panel" role="tabpanel" aria-labelledby={`adv-${tab}`} class="advpanel">
      {tab === "tools" ? children : tab === "files" ? <FilesTab ctx={ctx} lock={lockReason(k, d)} />
        : <SettingsList key={tab} list={tab} k={k} ctx={ctx} filter={filter} />}
    </div>
  </>;
}

// Why the lists can't be saved now, or null. Settings can change while idle or in an alarm, not while anything moves.
function lockReason(k, d) {
  const s = SCREENS[k];
  if (!d.connected) return "Waiting for the device to answer.";
  if (d.readOnly) return "Another tab is driving the machine; this one can only look.";
  if (d.job || s.moving || s.mode === "Job" || d.motion) return "Not while a job runs or the machine moves.";
  if (d.fluid && !["Idle", "Alarm"].includes(d.fluid)) return "Only while the machine is idle.";
  return null;
}

function SettingsList({ list, k, ctx, filter }) {
  const { d, send } = ctx;
  const sl = d.settingsList || { status: "idle" };
  const [drafts, setDrafts] = useState({});
  // Read the list when it first opens (and once connected); after that, with Enter in the filter
  useEffect(() => { if (d.connected && sl.status === "idle") send("readSettingsList"); }, [d.connected, sl.status]);
  // A save that went through: the list now has the new values, so the drafts go
  useEffect(() => { if (sl.save?.state === "saved") setDrafts({}); }, [sl.save]);

  const lock = lockReason(k, d);
  const items = sl.items || [];
  const mine = items.filter((i) => i.list === list);
  const { changes, errors } = pendingChanges(mine, drafts);
  const saving = sl.save?.state === "saving";
  const groups = grouped(items, list, filter);
  const edit = (path, v) => setDrafts({ ...drafts, [path]: v });

  if (sl.status !== "ready" && !items.length) return <div class="srow"><span>{
    sl.status === "failed" ? <>Couldn't read the settings: {sl.detail}</> : !d.connected ? "Connect to read the settings." : "Reading the settings…"}</span>
    <div class="val">{sl.status === "failed" && <button class="btn ghost" type="button" onClick={() => send("readSettingsList")}>Try again</button>}</div></div>;

  return <>
    <p class="note">{list === "maslow"
      ? "The Maslow settings in maslow.yaml, as the board has them now. Saving sets the changed ones and writes the file ($CO); they're used at once."
      : "FluidNC's own settings. Board (flash) settings are kept as soon as they're saved; config items are written to maslow.yaml, and some only take effect after a restart."}</p>
    {sl.status === "reading" && <p class="note" aria-live="polite">Reading the settings from the board…</p>}
    {sl.status === "failed" && <p class="note alert">Couldn't read the settings again: {sl.detail}</p>}
    {lock && <p class="note alert">{lock}</p>}
    {!groups.length && <p class="note">No setting matches “{filter}”.</p>}
    {groups.map(([g, rows]) => <section class="rawgroup" aria-label={g}>
      <h5>{g}</h5>
      {rows.map((i) => <RawRow item={i} draft={drafts[i.path]} onEdit={edit} lock={lock || i.locked} />)}
    </section>)}
    <div class="rawfoot" aria-live="polite">
      <span>{saving ? "Saving…" : changes.length || errors.length
        ? `${changes.length} changed${errors.length ? `, ${errors.length} to fix` : ""}`
        : sl.save?.state === "saved" ? `Saved.${sl.save.restart ? " Some take effect after a restart (Settings › State › Reboot device)." : ""}`
        : sl.save?.state === "failed" ? `Couldn't save: ${sl.save.detail}` : "No changes"}</span>
      <span class="rawbtns">
        <button class="btn ghost" type="button" disabled={saving || !Object.keys(drafts).length} onClick={() => setDrafts({})}>Discard</button>
        <button class="btn" type="button" disabled={!!lock || saving || !changes.length || errors.length > 0}
          title={lock || (errors.length ? "Fix the marked settings first" : "")}
          onClick={() => send("saveSettingsList", { changes })}>Save {changes.length || ""}</button></span>
    </div>
  </>;
}

const RESTART_TIP = "Only applies after the board restarts";

// One setting: its name, a dot if it needs a restart, and an editor that follows its type
function RawRow({ item: i, draft, onEdit, lock }) {
  const text = draft ?? i.value;
  const changed = isChanged(i, draft);
  const error = changed ? valueError(i, text) : null;
  const id = `raw-${i.path.replace(/[^A-Za-z0-9]/g, "-")}`;
  const on = i.type === "bool" && i.options.find((o) => /^(true|on)$/i.test(o.label));
  const off = i.type === "bool" && i.options.find((o) => o.value !== on.value);
  let editor;
  if (i.type === "bool") editor = <label class="check"><input id={id} type="checkbox" disabled={!!lock} checked={text === on.value}
    onChange={(e) => onEdit(i.path, e.currentTarget.checked ? on.value : off.value)} /> {text === on.value ? on.label : off.label}</label>;
  else if (i.type === "choice") editor = <select id={id} disabled={!!lock} value={text} onChange={(e) => onEdit(i.path, e.currentTarget.value)}>
    {i.options.map((o) => <option value={o.value}>{o.label}</option>)}</select>;
  else editor = <input id={id} type={i.secret ? "password" : "text"} class={i.type === "text" ? "" : "mono"} disabled={!!lock}
    inputmode={i.type === "int" ? "numeric" : i.type === "number" || i.type === "ip" ? "decimal" : undefined}
    autocomplete={i.secret ? "new-password" : "off"} spellcheck={false} placeholder={i.secret ? "Unchanged" : ""}
    value={text} onInput={(e) => onEdit(i.path, e.currentTarget.value)} aria-invalid={!!error} />;
  const range = (i.type === "int" || i.type === "number") && i.min != null && i.max != null ? `${i.min} to ${i.max}` : null;
  return <div class={`srow rawrow${changed ? " changed" : ""}`}>
    <span><label for={id} class="mono rawname" title={i.path}>{i.name}</label>
      {i.restart && <span class="rawdot" tabindex="0" role="img" aria-label={RESTART_TIP} data-tip={RESTART_TIP} />}
      <small>{[range, i.locked].filter(Boolean).join(" · ")}</small></span>
    <div class="val">{editor}{error && <span class="rawerr">{error}</span>}</div>
  </div>;
}
