import { useState, useEffect, useRef } from "preact/hooks";
import { crumbs, formatSize, isEditable, isConfig, nameError, protectedWhy, joinPath, backupName, checkYaml, baseName } from "../device/flashfiles.js";

// Settings › Advanced › Files: the board's own files, in its flash memory (not the SD card's jobs). Lists a folder,
// and for each file: Edit (text files), Download, Rename, Delete. Upload and New folder at the top. One row's actions
// open at a time, below the row, so it works at phone width. The page, maslow.yaml and the swap between the old and
// new page (renaming index.html.gz) get their own warnings. lock (from AdvancedSection) turns every change off.
export function FilesTab({ ctx, lock }) {
  const { d, send } = ctx;
  const fl = d.flash || { status: "idle", path: "/", files: [] };
  useEffect(() => { if (d.connected && fl.status === "idle") send("listFlash"); }, [d.connected, fl.status]);
  if (fl.editor) return <Editor ctx={ctx} lock={lock} />;
  return <FileList ctx={ctx} lock={lock} />;
}

const FolderIcon = () => <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" aria-hidden="true">
  <path d="M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z" /></svg>;
const FileIcon = () => <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" aria-hidden="true">
  <path d="M6 3h8l4 4v13a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1zM14 3v4h4" /></svg>;

// Names to offer when renaming the page, for swapping the old and new UI (REWRITE.md part 3, Testing)
function renameChips(name, files) {
  const taken = new Set(files.map((f) => f.name));
  const chips = name === "index.html.gz" ? ["index-old.html.gz", "index-new.html.gz"]
    : /^index.*\.html\.gz$/.test(name) ? ["index.html.gz"] : [];
  return chips.filter((c) => !taken.has(c));
}

function FileList({ ctx, lock }) {
  const { d, send } = ctx;
  const fl = d.flash;
  const [open, setOpen] = useState(null);       // the row whose actions show
  const [mode, setMode] = useState(null);       // null | "rename" | "delete" for that row
  const [newName, setNewName] = useState("");
  const [folder, setFolder] = useState(null);   // the New folder box's text, or null when closed
  const [replace, setReplace] = useState(null); // an upload onto a taken name, waiting for Replace
  const picker = useRef(null);
  const busy = !!fl.busy || fl.status === "reading";
  const off = !!lock || busy;
  const atTop = fl.path === "/";

  // A new folder shows a fresh list
  useEffect(() => { setOpen(null); setMode(null); setReplace(null); }, [fl.path]);
  const toggle = (name) => { setOpen(open === name ? null : name); setMode(null); };
  const startRename = (name) => { setMode("rename"); setNewName(name); };
  const go = (path) => send("listFlash", { path });

  const picked = (e) => {
    const file = e.currentTarget.files[0];
    e.currentTarget.value = "";
    if (!file) return;
    if (fl.files.some((f) => f.name === file.name && !f.dir)) setReplace(file);
    else send("uploadFlash", { file });
  };
  const doRename = (f) => {
    if (nameError(newName, fl.files, { except: f.name }) || newName === f.name) return;
    send("flashAction", { action: "rename", name: f.name, newname: newName });
    setOpen(null); setMode(null);
  };
  const folderErr = folder != null && folder !== "" ? nameError(folder, fl.files) : null;

  const usage = fl.total ? `${formatSize(fl.free)} free of ${formatSize(fl.total)}` : null;
  const hasIndex = atTop && fl.files.some((f) => /^index.*\.html\.gz$/.test(f.name));

  return <>
    <p class="note">The board's own files, in its flash memory: this page (index.html.gz), the machine's configuration
      (maslow.yaml) and a few settings files. Job files are on the SD card, under Files.{usage && ` ${usage}.`}</p>
    <div class="fbar">
      <nav class="fcrumbs" aria-label="Folder">{crumbs(fl.path).map(([p, label], i, all) => <>
        {i > 0 && <span aria-hidden="true">/</span>}
        <button type="button" class="fcrumb" aria-current={i === all.length - 1 ? "location" : undefined} disabled={busy}
          onClick={() => go(p)}>{label}</button></>)}</nav>
      <span class="fbtns">
        <button class="btn ghost" type="button" disabled={busy} title="Read the list from the board again" onClick={() => go(fl.path)}>Refresh</button>
        <button class="btn ghost" type="button" disabled={off} title={lock || "Make a folder here"} onClick={() => setFolder(folder == null ? "" : null)}>New folder</button>
        <button class="btn" type="button" disabled={off} title={lock || "Upload a file from this computer to this folder"} onClick={() => picker.current.click()}>Upload</button>
        <input ref={picker} type="file" hidden onChange={picked} /></span>
    </div>
    {lock && <p class="note alert">{lock}</p>}
    <p class={`note${fl.notice?.bad ? " alert" : ""}`} aria-live="polite" hidden={!fl.busy && !fl.notice}>{fl.busy || fl.notice?.text}</p>

    {folder != null && <form class="fpanel" onSubmit={(e) => { e.preventDefault(); if (!folderErr && folder) { send("flashAction", { action: "createdir", name: folder }); setFolder(null); } }}>
      <label for="fnewdir">New folder in {fl.path === "/" ? "the top folder" : fl.path}</label>
      <span class="frename"><input id="fnewdir" class="mono" type="text" value={folder} autocomplete="off" spellcheck={false}
        aria-invalid={!!folderErr} onInput={(e) => setFolder(e.currentTarget.value)} autofocus />
        <button class="btn" type="submit" disabled={off || !folder || !!folderErr}>Create</button>
        <button class="btn ghost" type="button" onClick={() => setFolder(null)}>Cancel</button></span>
      {folderErr && <span class="rawerr">{folderErr}</span>}
    </form>}

    {replace && <div class="fpanel warn" role="alertdialog" aria-label={`Replace ${replace.name}?`}>
      <p><b>Replace {replace.name}</b> on the board with the one from this computer ({formatSize(replace.size)})? The board's copy is lost.</p>
      {protectedWhy(joinPath(fl.path, replace.name)) && <p>{replace.name} is {protectedWhy(joinPath(fl.path, replace.name))}
        {replace.name === "index.html.gz" ? " Reload the page after the upload to use the new one." : " The new file is used after the board restarts."}</p>}
      <span class="fbtns"><button class="btn danger" type="button" disabled={off} onClick={() => { send("uploadFlash", { file: replace }); setReplace(null); }}>Replace</button>
        <button class="btn ghost" type="button" onClick={() => setReplace(null)}>Cancel</button></span>
    </div>}

    {fl.status === "failed" && <div class="srow"><span>Couldn't read the board's files: {fl.detail}</span>
      <div class="val"><button class="btn ghost" type="button" onClick={() => go(fl.path)}>Try again</button></div></div>}
    {fl.status !== "ready" && fl.status !== "failed" && !fl.files.length &&
      <p class="note">{d.connected ? "Reading the board's files…" : "Connect to see the board's files."}</p>}

    <ul class="flist" aria-busy={busy}>
      {!atTop && <li><button type="button" class="frow" disabled={busy} onClick={() => go(fl.path.replace(/\/[^/]*$/, "") || "/")}>
        <span class="fname">↑ Up a folder</span></button></li>}
      {fl.status === "ready" && !fl.files.length && <li class="note fempty">This folder is empty.</li>}
      {fl.files.map((f) => {
        const path = joinPath(fl.path, f.name), why = protectedWhy(path), isOpen = open === f.name;
        const renameErr = mode === "rename" && isOpen && newName !== f.name ? nameError(newName, fl.files, { except: f.name }) : null;
        return <li class={isOpen ? "open" : ""}>
          <span class="frowwrap">
            <button type="button" class="frow" aria-expanded={f.dir ? undefined : isOpen} disabled={busy && f.dir}
              onClick={() => (f.dir ? go(path) : toggle(f.name))}>
              {f.dir ? <FolderIcon /> : <FileIcon />}<span class="fname mono">{f.name}</span>
              {why && <span class="ftag" title={`Needed: ${why}`}>{f.name === "index.html.gz" ? "page" : "config"}</span>}
              <span class="fsize">{f.dir ? "folder" : formatSize(f.size)}</span></button>
            {f.dir && <button type="button" class="fmore" aria-expanded={isOpen} aria-label={`Actions for the folder ${f.name}`}
              onClick={() => toggle(f.name)}>⋯</button>}
          </span>
          {isOpen && <div class="facts">
            {mode === null && <span class="fbtns">
              {isEditable(f) && <button class="btn" type="button" disabled={off} title={lock || "Open it as text"} onClick={() => send("openFlashFile", { name: f.name })}>Edit</button>}
              {!f.dir && <button class="btn ghost" type="button" disabled={busy} onClick={() => send("downloadFlash", { name: f.name })}>Download</button>}
              <button class="btn ghost" type="button" disabled={off} title={lock || ""} onClick={() => startRename(f.name)}>Rename</button>
              <button class="btn ghost fdanger" type="button" disabled={off} title={lock || ""} onClick={() => setMode("delete")}>Delete</button></span>}

            {mode === "rename" && <form class="fpanel" onSubmit={(e) => { e.preventDefault(); doRename(f); }}>
              <label for="frename">Rename {f.name} to</label>
              <span class="frename"><input id="frename" class="mono" type="text" value={newName} autocomplete="off" spellcheck={false}
                aria-invalid={!!renameErr} onInput={(e) => setNewName(e.currentTarget.value)} autofocus />
                <button class="btn" type="submit" disabled={off || !!renameErr || newName === f.name}>Rename</button>
                <button class="btn ghost" type="button" onClick={() => setMode(null)}>Cancel</button></span>
              {renameChips(f.name, fl.files).length > 0 && <span class="fchips">{renameChips(f.name, fl.files).map((c) =>
                <button type="button" class="fchip mono" onClick={() => setNewName(c)}>{c}</button>)}</span>}
              {renameErr && <span class="rawerr">{renameErr}</span>}
              {why && <p class="note alert">{f.name} is {why}{f.name === "index.html.gz" && " This page keeps working until you reload it."}</p>}
            </form>}

            {mode === "delete" && <div class="fpanel warn" role="alertdialog" aria-label={`Delete ${f.name}?`}>
              <p><b>Delete {f.dir ? `the folder ${f.name} and everything in it` : f.name}?</b> It can't be undone.
                {!f.dir && " Download it first to keep a copy."}</p>
              {why && <p>It's {why}</p>}
              <span class="fbtns"><button class="btn danger" type="button" disabled={off}
                onClick={() => { send("flashAction", { action: f.dir ? "deletedir" : "delete", name: f.name }); setOpen(null); setMode(null); }}>Delete</button>
                <button class="btn ghost" type="button" onClick={() => setMode(null)}>Cancel</button></span>
            </div>}
          </div>}
        </li>;
      })}
    </ul>
    {hasIndex && <p class="note">To switch between the old and new page: rename the one in use out of the way first
      (index.html.gz to index-new.html.gz, say), then rename the other to index.html.gz, and reload. A rename onto a name
      that's taken may fail, so it isn't offered.</p>}
  </>;
}

// The text editor for one file. The draft stays here until Save; the board's copy, and the original as it was when
// opened, are in state.flash.editor.
function Editor({ ctx, lock }) {
  const { d, send } = ctx;
  const ed = d.flash.editor;
  const name = baseName(ed.path);
  const [draft, setDraft] = useState(ed.text ?? "");
  const [closing, setClosing] = useState(false);
  // The board's text arrives (opened) or changes (restored): show it
  useEffect(() => { if (ed.text != null) setDraft(ed.text); }, [ed.text]);
  const config = isConfig(ed.path), yaml = /\.ya?ml$/i.test(name);
  const dirty = ed.text != null && draft !== ed.text;
  const check = yaml ? checkYaml(draft) : { errors: [], warnings: [] };
  const saving = ed.status === "saving";
  const lines = draft.split("\n").length;

  const close = () => { if (dirty && !closing) { setClosing(true); return; } send("closeFlashFile"); };

  return <>
    <div class="fbar">
      <span class="fedname"><FileIcon /><b class="mono">{ed.path}</b>{dirty && <span class="ftag">edited</span>}</span>
      <span class="fbtns"><button class={`btn ${closing ? "danger" : "ghost"}`} type="button" onClick={close}>
        {closing ? "Close without saving" : "Close"}</button>
        {closing && <button class="btn ghost" type="button" onClick={() => setClosing(false)}>Keep editing</button>}</span>
    </div>
    {config && <p class="note warn">The machine reads {name} when it starts, so changes take effect after a restart. Before
      the first save, the file as it is now is kept as {backupName(name)}. Until the restart, saving a setting in the
      Maslow or FluidNC lists (or anything else that sends $CO) writes the running settings over this file.</p>}
    {lock && <p class="note alert">{lock}</p>}
    {ed.status === "reading" && <p class="note" aria-live="polite">Reading {name}…</p>}
    {ed.text == null && ed.status === "failed" && <p class="note alert">{ed.detail}</p>}
    {ed.text != null && <>
      <label for="fedit" class="sr">Contents of {name}</label>
      <textarea id="fedit" class="fedit mono" spellcheck={false} autocapitalize="off" autocomplete="off" wrap="off"
        rows={Math.min(24, Math.max(8, lines + 1))} value={draft} readOnly={saving} onInput={(e) => setDraft(e.currentTarget.value)} />
      {check.errors.map((e) => <p class="note alert">{e}</p>)}
      {check.warnings.map((w) => <p class="note warn">{w}</p>)}
      <div class="rawfoot" aria-live="polite">
        <span>{saving ? "Saving…"
          : ed.status === "failed" ? ed.detail
          : dirty ? `${lines} lines, not saved`
          : ed.status === "saved" ? `Saved.${ed.backup ? ` The original is in ${ed.backup}.` : ""}${ed.restart ? " Restart the board to use it." : ""}`
          : ed.status === "restored" ? `Put back as it was.${ed.restart ? " Restart the board to use it." : ""}`
          : "No changes"}</span>
        <span class="fbtns">
          {(ed.status === "saved" || ed.status === "restored") && ed.restart && !dirty &&
            <button class="btn" type="button" disabled={!!lock} title={lock || "Restart the board ([ESP444]RESTART); the page reconnects"}
              onClick={() => { send("closeFlashFile"); send("reboot"); }}>Restart now</button>}
          {ed.original != null && ed.text !== ed.original && !dirty &&
            <button class="btn ghost" type="button" disabled={!!lock || saving} title={lock || `Save ${name} as it was when you opened it`}
              onClick={() => send("restoreFlashFile")}>Undo save</button>}
          {dirty && <button class="btn ghost" type="button" disabled={saving} onClick={() => setDraft(ed.text)}>Discard</button>}
          {ed.noRoom && dirty && <button class="btn ghost" type="button" disabled={!!lock || saving}
            onClick={() => send("saveFlashFile", { text: draft, withoutBackup: true })}>Save without a backup</button>}
          {dirty && <button class="btn" type="button" disabled={!!lock || saving || check.errors.length > 0}
            title={lock || (check.errors.length ? "Fix the problems first" : "")}
            onClick={() => send("saveFlashFile", { text: draft })}>Save</button>}
        </span>
      </div>
    </>}
  </>;
}
