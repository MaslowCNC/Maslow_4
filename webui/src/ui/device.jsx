import { useEffect, useRef, useState } from "preact/hooks";
import { STREAMS, updateLock, isNewer, REPO } from "../device/update.js";
import { formatSize } from "../device/flashfiles.js";
import "./device.css";

// Settings › Device: firmware version, check for updates, firmware update, and the board's system status.
// The device adapters do the work (device/update.js); this only shows state.update and sends the actions.
// Nothing installs by itself: each install is a press, then a confirmation that says the machine restarts.

const Row = ({ label, hint, children }) =>
  <div class="srow"><span>{label}{hint && <small>{hint}</small>}</span><div class="val">{children}</div></div>;

// The stream chosen here, and a release whose notice in the top bar was dismissed, are remembered in this browser
const STREAM_KEY = "maslow.updateStream", DISMISSED_KEY = "maslow.updateDismissed";
export const savedStream = () => { try { return localStorage.getItem(STREAM_KEY) || "release"; } catch { return "release"; } };
const dismissed = () => { try { return localStorage.getItem(DISMISSED_KEY); } catch { return null; } };

// The top bar's notice: a newer release in the chosen stream than the board runs. Opens Settings › Device; × hides it
// until a newer release comes out. Only a notice: nothing is downloaded or installed.
export function UpdateNotice({ d, openSettings }) {
  const [hidden, setHidden] = useState(dismissed);
  const c = d.update?.check, r = c?.release;
  if (c?.state !== "done" || !r || !c.newer || hidden === r.tag || ACTIVE.includes(d.update.install?.phase)) return null;
  const hide = () => { try { localStorage.setItem(DISMISSED_KEY, r.tag); } catch { /* not remembered */ } setHidden(r.tag); };
  return <span class="noticetag upnotice" role="status">
    <button type="button" class="uplink" title="Open Settings › Device to install it" onClick={() => openSettings("device")}>Firmware {r.tag} available</button>
    <button type="button" aria-label={`Hide the notice for ${r.tag}`} onClick={hide}>×</button></span>;
}
const date = (iso) => (iso ? new Date(iso).toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric" }) : "");
const ACTIVE = ["downloading", "uploading", "restarting"];
const RELEASES_PAGE = `https://github.com/${REPO}/releases`;

export function DeviceSection({ ctx, name, restart }) {
  const { d, send } = ctx;
  const u = d.update || {};
  const i = u.install;
  const busy = ACTIVE.includes(i?.phase);
  const lock = updateLock(d);
  return <>
    <h4>Device</h4>
    {name}
    <Row label="Firmware" hint={d.machine ? `${d.machine}. One firmware fits every Maslow board: the board is recognised when it starts.` : null}>
      <span class="mono">{d.fwVersion || (d.connected ? "Reading…" : "–")}</span></Row>
    <CheckUpdates ctx={ctx} u={u} lock={lock} busy={busy} />
    <FromFiles ctx={ctx} u={u} lock={lock} busy={busy} />
    {i && <Progress i={i} send={send} />}
    {restart}
    <SystemStatus ctx={ctx} u={u} />
    <p class="note" style="margin:0">Maslow 4 firmware: FluidNC with the Maslow layer, and this page. GPLv3.</p>
  </>;
}

// The confirmation, in the page: what will be written, and what happens next
function Confirm({ what, older, onYes, onNo }) {
  return <div class="upconfirm" role="alertdialog" aria-label="Confirm the update">
    <p><b>Install {what}?</b></p>
    {older && <p>This isn't newer than the firmware on the board.</p>}
    <p>The board restarts when the firmware is written, about 30 seconds in all. Don't turn the machine off meanwhile.
      After the restart, run setup again from Apply tension.</p>
    <div class="netnote"><button class="btn" type="button" onClick={onYes}>Install and restart</button>
      <button class="btn ghost" type="button" onClick={onNo}>Cancel</button></div>
  </div>;
}

function CheckUpdates({ ctx, u, lock, busy }) {
  const { d, send } = ctx;
  const [stream, setStream] = useState(savedStream);
  const [ask, setAsk] = useState(false);
  const pick = (v) => { setStream(v); try { localStorage.setItem(STREAM_KEY, v); } catch { /* private mode */ } };
  const c = u.check?.stream === stream ? u.check : null;
  const r = c?.release;
  useEffect(() => setAsk(false), [r?.tag, lock]);
  return <>
    <Row label="Updates" hint={STREAMS.find(([v]) => v === stream)[2]}>
      <div class="subtabs" role="radiogroup" aria-label="Update stream">
        {STREAMS.map(([v, n]) => <button type="button" role="radio" aria-checked={stream === v} aria-selected={stream === v} onClick={() => pick(v)}>{n}</button>)}
      </div>
      <button class="btn ghost" type="button" disabled={c?.state === "checking" || busy} onClick={() => send("checkUpdates", { stream })}>
        {c?.state === "checking" ? "Checking…" : "Check for updates"}</button>
    </Row>
    {c?.state === "offline" && <div class="netwarn" role="status">
      <b>This computer can't reach the internet, so it can't check GitHub.</b>
      <span>It may be on the machine's hotspot, which has no internet. Join a network with internet and check again, or
        download <span class="mono">firmware.bin</span> and <span class="mono">index.html.gz</span> from <a href={RELEASES_PAGE} target="_blank" rel="noopener">the releases page</a> on
        another computer and choose them below.</span></div>}
    {c?.state === "failed" && <p class="note bad" role="status">Couldn't check for updates: {c.detail}</p>}
    {c?.state === "done" && !r && <p class="note" role="status">No {stream} release has firmware to install.</p>}
    {c?.state === "done" && r && <div class="uprelease">
      <div class="uphead"><b class="mono">{r.tag}</b><span class="note">{date(r.published)}</span>
        <span class={`uptag${c.newer ? " new" : ""}`}>{c.newer ? `Newer than ${d.fwVersion || "this board's"}` : "This board is up to date"}</span></div>
      {r.notes && <details><summary>Release notes</summary><pre class="upnotes">{r.notes}</pre>
        <a href={r.url} target="_blank" rel="noopener">On GitHub</a></details>}
      {ask ? <Confirm what={`${r.tag} (firmware and page)`} older={!c.newer} onNo={() => setAsk(false)}
        onYes={() => { setAsk(false); send("installUpdate", { source: "release" }); }} />
        : <div class="netnote"><button class={c.newer ? "btn" : "btn ghost"} type="button" disabled={!!lock || busy} onClick={() => setAsk(true)}>
          {c.newer ? `Install ${r.tag}` : `Reinstall ${r.tag}`}</button>
          {lock && !busy && <span class="note">{lock}</span>}</div>}
    </div>}
  </>;
}

function FromFiles({ ctx, u, lock, busy }) {
  const { d, send } = ctx;
  const input = useRef(null);
  const [ask, setAsk] = useState(false);
  const files = u.files || [];
  const ok = files.length && files.every((f) => f.ok);
  const fw = files.find((f) => f.kind === "firmware");
  useEffect(() => setAsk(false), [files, lock]);
  return <>
    <Row label="From a file" hint="firmware.bin, and index.html.gz for the page, from a release on this computer. Each is checked before anything is sent.">
      <input ref={input} type="file" accept=".bin,.gz" multiple hidden onChange={(e) => {
        const list = [...e.currentTarget.files]; e.currentTarget.value = ""; if (list.length) send("chooseUpdateFiles", { files: list });
      }} />
      <button class="btn ghost" type="button" disabled={busy} onClick={() => input.current.click()}>Choose files…</button>
      {files.length > 0 && !busy && <button class="btn ghost" type="button" onClick={() => send("clearUpdateFiles")}>Clear</button>}
    </Row>
    {files.length > 0 && <ul class="upfiles">{files.map((f) => <li>
      <b class="mono">{f.name}</b> <span class="note">{formatSize(f.size)}</span>{" "}
      {f.ok ? <span class="okc">{f.kind === "page" ? "The web page" : `Firmware ${f.version || "(version unknown)"}`}</span>
        : <span class="badc">{f.errors[0]}</span>}
      {f.warnings.map((w) => <div class="note">{w}</div>)}</li>)}</ul>}
    {files.length > 0 && (ask
      ? <Confirm what={files.map((f) => f.name).join(" and ")} older={fw?.version && d.fwVersion ? !isNewer(d.fwVersion, fw.version) : false}
        onNo={() => setAsk(false)} onYes={() => { setAsk(false); send("installUpdate", { source: "files" }); }} />
      : <div class="netnote"><button class="btn" type="button" disabled={!ok || !!lock || busy} onClick={() => setAsk(true)}>
        {fw ? "Install and restart" : "Write the page"}</button>
        {lock && !busy && <span class="note">{lock}</span>}</div>)}
  </>;
}

function Progress({ i, send }) {
  const pct = Math.max(0, Math.min(100, i.pct || 0));
  if (i.phase === "failed") return <div class="netwarn bad" role="alert"><b>Update failed: {i.detail}</b>
    {i.step && <span>{i.step}.</span>}<div><button class="btn ghost" type="button" onClick={() => send("dismissUpdate")}>OK</button></div></div>;
  if (i.phase === "done") return <div class={`upresult${i.match === false ? " bad" : ""}`} role="status">
    {i.match === false ? <b>The board came back on {i.version || "an unknown version"}, not {i.expect}. The update may not have taken: try again, or update by USB.</b>
      : i.version ? <b>Updated: the board runs {i.version}.</b> : <b>Done.</b>}
    {i.page && <span>The new page is on the board. Reload to use it.</span>}
    <div class="netnote">{i.page && <button class="btn" type="button" onClick={() => location.reload()}>Reload the page</button>}
      <button class="btn ghost" type="button" onClick={() => send("dismissUpdate")}>OK</button></div></div>;
  return <div class="upprogress" role="status" aria-live="polite">
    <span>{i.phase === "restarting" ? "The board is restarting. This page reconnects by itself." : i.step || "Starting…"}</span>
    {i.phase !== "restarting" && <><progress max="100" value={pct} aria-label={i.step} /><span class="mono">{pct}%</span></>}
    {i.phase === "uploading" && <span class="note">Keep this page open, and the machine on.</span>}
    {i.slow && <span class="note bad">It isn't back yet. If it doesn't come back, check its screen or USB: the update may have failed, and the board then runs the old firmware.</span>}
  </div>;
}

function SystemStatus({ ctx, u }) {
  const { d, send } = ctx;
  const st = u.status || {};
  useEffect(() => { if (d.connected) send("readStatus"); }, [d.connected]);
  const flash = st.flash;
  return <>
    <Row label="System status" hint="What the board reports ([ESP420]).">
      <button class="btn ghost" type="button" disabled={!d.connected || st.state === "reading"} onClick={() => send("readStatus")}>
        {st.state === "reading" ? "Reading…" : "Refresh"}</button></Row>
    {st.state === "failed" && <p class="note bad">Couldn't read it: {st.detail}</p>}
    {st.rows?.length > 0 && <dl class="upstatus">
      {flash?.total != null && <><dt>Flash files</dt><dd class="mono">{formatSize(flash.free)} free of {formatSize(flash.total)}</dd></>}
      {st.rows.map(([k, v]) => <><dt>{k}</dt><dd class="mono">{v}</dd></>)}
      <dt>This page</dt><dd class="mono">{typeof __PAGE_VERSION__ === "string" ? __PAGE_VERSION__ : "dev"}</dd>
    </dl>}
  </>;
}
