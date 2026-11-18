import { useState } from "preact/hooks";
import { STEPS, SCREENS, isSetup } from "../flow/screens.js";
import { stepArt, visSvg, frameMap, toolpathSvg, toolpathView, workspaceCaption, currentColour, MAX_CURRENT_MA } from "../art.js";
import { makeFrame, accuracyMap, safeRect, checkJob, startRing, TOLERANCE, SLED_MASS } from "../accuracy.js";
import logo from "../assets/maslow-logo.webp";

// A top-bar button. b.temp marks one that exists only until the firmware rewrite is finished: it shows orange,
// tagged "Temporary", and its tooltip says what it's waiting for. Every stopgap like this should use it.
export const Btn = ({ b }) => <button class={`btn ${b.style}${b.temp ? " temp" : ""}`} type="button" onClick={b.onClick}
  disabled={b.disabled} title={b.temp ? `Temporary: ${b.temp}` : b.title}>{b.label}{b.temp && <span class="temptag">Temporary</span>}</button>;

export const Svg = ({ html, class: c }) => <span class={c} dangerouslySetInnerHTML={{ __html: html }} />;

export const ModePill = ({ s, small }) =>
  <span class="mode" style={`--tone:${s.tone}${small ? ";font-size:13px" : ""}`}>{s.mode}</span>;

export function Conn({ d }) {
  const [label, c] = !d.connected ? (d.everConnected ? ["Not responding", "var(--danger)"] : ["Connecting", "var(--warn)"])
    : d.hotspot ? ["Hotspot", "var(--warn)"] : [d.readOnly ? "Connected, read-only" : "Connected", "var(--ready)"];
  return <span class="conn" style={`--c:${c}`}><i></i>{label}</span>;
}

// Setup progress: steps before the current one are done, or skipped if the device was already past them
export function progress(step, ran) {
  const skipped = STEPS.map((_, i) => i < step && !ran.includes(i));
  const n = skipped.filter(Boolean).length;
  return { skipped, label: `${step}/${STEPS.length} done${n ? `, ${n} skipped` : ""}` };
}
export const Segs = ({ step, skipped }) =>
  <div class="segs">{STEPS.map((_, i) => <i class={skipped[i] ? "skip" : i < step ? "done" : i === step ? "now" : ""} />)}</div>;

export const StepArt = ({ step, d, class: c = "art" }) =>
  <div class={c}><Svg html={stepArt(step)} /></div>;

// view: "frame" (the frame, the belts and the sled, with the file's toolpath on it) or "toolpath" (the chosen file
// alone, fitted to the drawing, with the bit as a small circle).
// showLengths / showCurrents (the Belts and Motors toggles), on the frame view: each belt labelled with its live
// length, and a ring at each anchor for its motor's current. The toolpath view has no belts or anchors to draw them
// on, so there they show as text instead (VisReadouts).
// showWorkspace: the area where cuts land within the tolerance (mm, Settings → Frame and belts) and whether the
// chosen job fits it (the Workspace toggle), from the anchors. The outline is drawn on the frame view; the caption
// shows on both.
export const Frame = ({ k, d, view = "frame", showLengths = false, showCurrents = false, showWorkspace = false, tolerance = TOLERANCE, sledMass = SLED_MASS }) => {
  const s = SCREENS[k];
  const acc = accuracyFor(d, tolerance, sledMass);
  const cap = showWorkspace ? workspaceCaption(acc.overlay) : null;
  const caption = cap && <span class="wscap" style={{ color: cap.colour }}>{cap.text}</span>;
  if (view === "toolpath") {
    const st = d.gcode?.status;
    const html = st === "ready" && d.gcode.extent ? toolpathViewFor(d, s.nopos ? null : workPos(d)) : null;
    return <>{html ? <Svg class="framesvg" html={html} />
      : <div class="visempty">{st === "reading" ? "Reading the file…" : st === "failed" ? `Couldn't read the file: ${d.gcode.detail || "unknown error"}`
        : st === "ready" ? "This file has no cutting moves to show" : "Choose a file to see its toolpath"}</div>}{caption}</>;
  }
  const lengths = showLengths ? d.belts.map((v, i) => d.maslow === 0 || d.beltUnknown?.[i] ? null : v) : null;
  const mc = d.motorCurrent;
  const currents = showCurrents ? (mc ? [mc.tl, mc.tr, mc.bl, mc.br].map(adcToMilliamps) : [null, null, null, null]) : null;
  return <><Svg class="framesvg" html={visSvg(k, s, d.pos, d.wco, lengths, currents, showWorkspace ? acc.overlay : null, acc.size,
    toolpathFor(d, acc.size))} />{caption}</>;
};

// Belt lengths and motor currents as text, under the Belts and Motors toggles, on the Toolpath tab. A length is unknown before the machine
// knows it (Maslow state 0, or "?" in the report's BL); currents are in mA from the report's MC.
const CORNERS = ["TL", "TR", "BL", "BR"];
export function VisReadouts({ d, belts, motors }) {
  if (!belts && !motors) return null;
  const mc = d.motorCurrent;
  const mA = mc ? [mc.tl, mc.tr, mc.bl, mc.br].map(adcToMilliamps) : [null, null, null, null];
  return <div class="readouts">
    {belts && <div class="ro"><span>Belts</span><dl>{d.belts.map((v, i) => {
      const unknown = d.maslow === 0 || d.beltUnknown?.[i] || v == null;
      return <><dt>{CORNERS[i]}</dt><dd>{unknown ? "?" : `${v.toFixed(1)} mm`}</dd></>;
    })}</dl></div>}
    {motors && <div class="ro"><span>Motors</span><dl>{mA.map((v, i) => <><dt>{CORNERS[i]}</dt>
      <dd>{v == null ? "–" : <><i style={{ background: currentColour(Math.min(100, v / MAX_CURRENT_MA * 100)) }} />{Math.round(v)} mA</>}</dd></>)}</dl></div>}
  </div>;
}

// The Toolpath view, kept until the file, the bit's position (to a tenth of a mm) or the running line change; the
// running line redraws at most once a second, as a big file's path is large.
let viewCache = { g: null, key: null, done: null, at: 0, value: "" };
function toolpathViewFor(d, work) {
  const g = d.gcode;
  const base = (f) => String(f || "").split("/").pop();
  const done = d.job && base(d.job.file) === base(g.file) ? d.job.line : null;
  const key = JSON.stringify([work && work.x.toFixed(1), work && work.y.toFixed(1), done == null]);
  const now = Date.now();
  if (g !== viewCache.g || key !== viewCache.key || (done !== viewCache.done && now - viewCache.at >= 1000)) {
    viewCache = { g, key, done, at: now, value: toolpathView(g, work, done) };
  }
  return viewCache.value;
}

// The chosen file's toolpath, kept until the file, the work offset or the scale change. While its job runs, the moves
// up to the running line show as cut; that part is redrawn at most once a second, as a big file's path is large.
let pathCache = { key: null, g: null, done: null, at: 0, value: "" };
function toolpathFor(d, size) {
  const g = d.gcode?.status === "ready" ? d.gcode : null;
  if (!g) return "";
  const base = (f) => String(f || "").split("/").pop();   // the job's file is a bare name; the chosen file may be a path
  const done = d.job && base(d.job.file) === base(g.file) ? d.job.line : null;
  const key = JSON.stringify([d.wco.x, d.wco.y, size, done == null]);
  const now = Date.now();
  if (g !== pathCache.g || key !== pathCache.key || (done !== pathCache.done && now - pathCache.at >= 1000)) {
    const { mapX, mapY } = frameMap(size);
    pathCache = { key, g, done, at: now, value: toolpathSvg(g.moves, d.wco, mapX, mapY, done) };
  }
  return pathCache.value;
}

// The accuracy map takes a few ms, so it's kept until the anchors, the spoil board, the tolerance, the board or (on a
// vertical frame) the sled's weight change
let accCache = { key: null, value: null };
function accuracyFor(d, tol, mass) {
  const f = d.frame;
  // The real machine reports the anchors a moment after connecting: until then there's nothing to work out
  if (!f) return { size: null, overlay: { note: "The workspace needs the anchors" } };
  const ring = startRing(d.machine), key = JSON.stringify([f, d.workArea?.spoil, tol, ring, f.vertical && mass]);
  if (key !== accCache.key) {
    const frame = f && makeFrame({ ...f, spoil: d.workArea?.spoil ?? 0, ring, vertical: !!f.vertical, mass });
    const map = frame && accuracyMap(frame);
    accCache = { key, value: { frame, map, rect: map && safeRect(map, tol) } };
  }
  const { frame, map, rect } = accCache.value;
  const size = frame ? { w: frame.width, h: frame.height } : null;
  if (!frame) return { size, overlay: { note: "The workspace needs the anchors" } };
  // On a vertical frame even the centre can need more than the motors hold
  if (!map) return { size, overlay: { note: `A ${mass} kg sled is more than the motors hold, even at the centre` } };
  const hanging = f.vertical ? mass : null;
  // The job's extent is in work coordinates; machine = work + WCO
  const e = d.gcode?.status === "ready" ? d.gcode.extent : null, w = d.wco;
  const job = e && e.max.x > e.min.x && e.max.y > e.min.y
    ? checkJob(map, { min: { x: e.min.x + w.x, y: e.min.y + w.y }, max: { x: e.max.x + w.x, y: e.max.y + w.y } }, tol) : null;
  return { size, overlay: { tol, rect, job, hanging } };
}

// Work position (from the user's home) and machine position (from the anchors). WPos = MPos − WCO.
export const workPos = (d) => ({ x: d.pos.x - d.wco.x, y: d.pos.y - d.wco.y, z: d.pos.z - d.wco.z });
const f3 = (v) => (Math.abs(v) < 0.0005 ? 0 : v).toFixed(3);
export function PosReadout({ d, nopos, stale }) {
  const w = workPos(d), m = d.pos;
  const row = (label, p) => <div class="posrow"><span>{label}</span><b><i>X</i>{nopos ? "–" : f3(p.x)}</b><b><i>Y</i>{nopos ? "–" : f3(p.y)}</b><b><i>Z</i>{nopos ? "–" : f3(p.z)}</b></div>;
  return <div class={`posgrid${stale ? " stale" : ""}`}>{row("Work", w)}{row("Machine", m)}</div>;
}

export function LogView({ d }) {
  return <pre class="log">{d.log.slice(-60).join("\n") || "No messages yet"}</pre>;
}

// Jogging needs anchors and a confirmed position, and nothing else moving
export function jogLock(k, d) {
  const s = SCREENS[k];
  if (!d.connected) return ["Not connected", "Jogging is off until the device answers."];
  if (k === "wifi") return ["Not connected", "Jogging unlocks once the device is on your network."];
  if (k === "cutting") return ["Job running", "Stop the job to jog."];
  if (s.mode === "Error") return ["Motion is disabled", "Clear the error first."];
  if (s.moving) return ["Moving", `${s.sub || s.mode}. Wait for it to finish, or press Stop.`];
  if (s.nopos) return ["Position not known", "Jogging needs anchors and a confirmed position."];
  return null;
}

// A button whose action the device may not support yet (d.notYet): then it's disabled and says why
const NotYetBtn = ({ d, id, label, send }) => <button class="btn ghost" type="button" disabled={!!d.notYet?.[id]}
  title={d.notYet?.[id]} onClick={() => send(id)}>{label}</button>;

// Chunky jog arrows: one shape, turned for each direction (0° points up)
const Arrow = ({ deg }) => <svg class="jarrow" viewBox="0 0 24 24" width="22" height="22" aria-hidden="true" style={`transform:rotate(${deg}deg)`}>
  <path d="M12 3 L21 13 H15.5 V21 H8.5 V13 H3 Z" fill="currentColor" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round" /></svg>;
const House = () => <svg viewBox="0 0 24 24" width="24" height="24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M3 11.5 12 4l9 7.5" /><path d="M5.5 10v9.5h4.5V14h4v5.5h4.5V10" /></svg>;

// The XY jog directions: [dx, dy, arrow angle, axis label]. The straight ones say which way the machine moves.
const JOG_DIRS = [[-1, 1, -45, ""], [0, 1, 0, "+Y"], [1, 1, 45, ""], [-1, 0, -90, "−X"], null, [1, 0, 90, "+X"],
  [-1, -1, -135, ""], [0, -1, 180, "−Y"], [1, -1, 135, ""]];
const DIR_NAMES = ["Up left", "Up", "Up right", "Left", "", "Right", "Down left", "Down", "Down right"];

export function JogPad({ k, d, send }) {
  // One XY step: the user's own value (10 mm to start). Z has its own step, typed between Z+ and Z−.
  const [step, setStep] = useState("10");
  const size = parseFloat(step) > 0 ? parseFloat(step) : 0;
  const [zStep, setZStep] = useState("5");
  const zMm = parseFloat(zStep) > 0 ? parseFloat(zStep) : 0;
  const lock = jogLock(k, d);
  const j = (dx, dy) => send("jog", { dx: dx * size, dy: dy * size, dz: 0 });
  const jz = dir => send("jog", { dx: 0, dy: 0, dz: dir * zMm });
  return <div class="jogwrap">
    <div class="jogtop">
      <label class="chipin on"><span>Step</span><input type="text" inputmode="decimal" value={step} aria-label="XY step size in mm"
        onInput={e => setStep(e.currentTarget.value)} /><span>mm</span></label>
      <NotYetBtn d={d} id="rehomeZ" label="Home Z" send={send} />
      <NotYetBtn d={d} id="park" label="Park" send={send} />
    </div>
    <div class="compass">
      <div class="xy">{JOG_DIRS.map((v, i) => v
        ? <button type="button" aria-label={`${DIR_NAMES[i]} ${size} mm`} disabled={!size} onClick={() => j(v[0], v[1])}>
            <Arrow deg={v[2]} />{v[3] && <span class="jlabel">{v[3]}</span>}</button>
        : <button type="button" class="home" aria-label="Go to your home (work 0, 0)" title="Go to your home (work 0, 0)"
            onClick={() => send("jog", { dx: d.wco.x - d.pos.x, dy: d.wco.y - d.pos.y })}><House /></button>)}
      </div>
      <div class="zcol">
        <button type="button" class="z" aria-label={`Z up ${zMm} mm`} disabled={!zMm} onClick={() => jz(1)}><Arrow deg={0} /><span class="jlabel">+Z</span></button>
        <label class="zstep"><input type="text" inputmode="decimal" value={zStep} aria-label="Z step size in mm"
          onInput={e => setZStep(e.currentTarget.value)} /><span>mm</span></label>
        <button type="button" class="z" aria-label={`Z down ${zMm} mm`} disabled={!zMm} onClick={() => jz(-1)}><Arrow deg={180} /><span class="jlabel">−Z</span></button>
      </div>
    </div>
    <PosReadout d={d} nopos={!!lock && SCREENS[k].nopos} />
    {/* Home Z finds the top of travel (the spindle's light beam); Set Z makes where the bit is now work Z 0 */}
    <div class="row"><NotYetBtn d={d} id="setHomeXY" label="Set home XY" send={send} />
      <NotYetBtn d={d} id="setZ" label="Set Z" send={send} /></div>
    {lock && <div class="locked"><div><b>{lock[0]}</b><span>{lock[1]}</span></div></div>}
  </div>;
}

const BELT_NAMES = ["TL", "TR", "BL", "BR"];
// The app header, above the setup or mode bar on every screen: the Maslow logo, the machine this page is
// connected to, the connection, and Settings. These live only here.
export function AppHeader({ d, onSettings }) {
  return <header class="apphead">
    <img class="logo" src={logo} alt="Maslow" />
    <MachineTag d={d} />
    <span class="apphead-gap" />
    <Conn d={d} />
    <button class="gear" type="button" aria-label="Settings" onClick={onSettings}>⚙</button>
  </header>;
}

// The machine this page is connected to, as a small tag ("Maslow Mini")
export const MachineTag = ({ d }) => d.machine ? <span class="machinetag" title="The machine this page is connected to">{d.machine}</span> : null;

// The File card: the running job's progress, or what the chosen file will cut
function FileCard({ d, a }) {
  if (d.job) {
    const pct = Number(d.job.total) > 0 ? Math.min(100, Math.round(100 * d.job.line / d.job.total)) : null;
    return <div class="mini"><span>File</span><b class="fname">{d.job.file}</b>
      <div class="bar"><i style={`width:${pct ?? 0}%`}></i></div>
      <small>line {d.job.line} / {d.job.total} · {pct == null ? "counting the file's lines…" : `${pct}% of the lines`}</small></div>;
  }
  const name = a.file;
  if (!name) return <div class="mini"><span>File</span><p class="note" style="margin:0">No file chosen yet</p></div>;
  const f = d.files.find(x => x.name === name) || {};
  const g = d.gcode?.file === name ? d.gcode : null;
  const e = g?.status === "ready" && g.extent;
  const size = e ? `${(e.max.x - e.min.x).toFixed(0)} × ${(e.max.y - e.min.y).toFixed(0)} mm` : f.w ? `${f.w} × ${f.h} mm` : null;
  const depth = e ? `${Math.max(0, -e.min.z).toFixed(1)} mm deep` : f.depth != null ? `${f.depth} mm deep` : null;
  const extra = [size, depth, f.minutes ? `about ${f.minutes} min` : null, g?.lineCount ? `${g.lineCount.toLocaleString()} lines` : null].filter(Boolean);
  return <div class="mini"><span>File</span><b class="fname">{name}</b>
    <small>{g?.status === "reading" ? "reading the file…" : g?.status === "failed" ? `couldn't read it: ${g.detail || ""}` : extra.join(" · ") || f.size}</small></div>;
}

// Belt lengths and motor currents, live from the status report. Desktop shows them as toggled overlays on the
// visuals (they'll become a graphic overlay later); the phone shows them as tabs in Details.
export function BeltsView({ k, d }) {
  const s = SCREENS[k];
  if (d.maslow === 0) return <p class="note">Belt lengths aren't known yet.</p>;
  return <div class={`belts${s.stale ? " stale" : ""}`}>{d.belts.map((v, i) => <><span>{BELT_NAMES[i]}</span><span class="bar"><i style={`width:${Math.min(100, v / 15)}%`}></i></span><span>{v.toFixed(1)} mm</span></>)}</div>;
}
export function MotorsView({ k, d }) {
  const s = SCREENS[k];
  if (!d.motorCurrent) return <p class="note">Motor currents show here when the machine reports them.</p>;
  // MC is raw ADC 0–4095; the current UI converts it to mA this way (docs/protocol.md)
  const mA = (adc) => Math.round(1000 * 3.3 * adc / 4095 / 1.5);
  const mc = d.motorCurrent;
  return <div class={`belts${s.stale ? " stale" : ""}`}>{[mc.tl, mc.tr, mc.bl, mc.br].map((v, i) => <><span>{BELT_NAMES[i]}</span><span class="bar"><i style={`width:${Math.min(100, mA(v) / 20)}%`}></i></span><span>{mA(v)} mA</span></>)}</div>;
}

// MC is raw ADC 0–4095; the current UI converts it to mA this way (docs/protocol.md)
export const adcToMilliamps = (adc) => (adc == null ? null : 1000 * 3.3 * adc / 4095 / 1.5);

// The Status, Position and File cards
export function DetailCards({ k, d, a }) {
  const s = SCREENS[k];
  return <div class="minis">
    <div class="mini"><span>Status</span><p class={s.alert ? "alert" : ""}>{s.msg}</p></div>
    <div class="mini"><span>Position</span><PosReadout d={d} nopos={s.nopos} stale={s.stale} /></div>
    <FileCard d={d} a={a} />
  </div>;
}

// Details on a phone: the cards, then Belts and Motors as tabs
export function DetailsBody({ k, d, a }) {
  const [tab, setTab] = useState("belts");
  const s = SCREENS[k];
  return <>
    <DetailCards k={k} d={d} a={a} />
    <div class="dstats">
      <div class="dtabs" role="tablist">{[["belts", "Belts"], ["motors", "Motors"]].map(([id, n]) =>
        <button type="button" role="tab" aria-selected={id === tab} onClick={() => setTab(id)}>{n}</button>)}
        <span class={`dlive${s.stale ? " off" : ""}`}>{s.stale ? "out of date" : ""}</span></div>
      <div class="dbody">{tab === "belts" ? <BeltsView k={k} d={d} /> : <MotorsView k={k} d={d} />}</div>
    </div>
  </>;
}

const UploadIcon = () => <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  <path d="M12 15V4M7 9l5-5 5 5M4 15v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4" /></svg>;
const TrashIcon = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  <path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-12M9 7V4h6v3" /></svg>;

// The upload icon, for the File management header (desktop) or the top of the Files section (phone)
export function UploadBtn({ k, a, setA, send, mark }) {
  const locked = SCREENS[k].mode === "Job";
  return <button type="button" class="iconbtn" aria-label="Upload a file" title={locked ? "You can't upload during a job" : "Upload a file"} disabled={locked}
    onClick={() => Promise.resolve(send("uploadFile")).then(f => { if (f && (a.file || isSetup(k))) { mark(7); setA({ file: f }); } })}><UploadIcon /></button>;
}

export function FilesBody({ k, d, a, setA, send, mark, upload = false }) {
  const locked = SCREENS[k].mode === "Job";
  // Deleting can't be undone, so the trash asks once: the first press arms it ("Delete"), the second deletes
  const [armed, setArmed] = useState(null);
  const del = name => {
    if (armed !== name) { setArmed(name); return; }
    setArmed(null);
    if (a.file === name) setA({ file: null });
    send("deleteFile", { name });
  };
  return <>
    {upload && <div class="fhead"><span>{d.files.length} file{d.files.length === 1 ? "" : "s"} on the SD card</span>
      <UploadBtn k={k} a={a} setA={setA} send={send} mark={mark} /></div>}
    {!d.files.length && <p class="note">No files yet. Upload one with the upload icon.</p>}
    <ul class="files">{d.files.map(f => {
      const running = d.job?.file === f.name;
      return <li class={`${f.name === a.file ? "sel" : ""}${armed === f.name ? " armed" : ""}`} onMouseLeave={() => armed === f.name && setArmed(null)}>
        <button type="button" class="fbtn" disabled={locked} onClick={() => { mark(7); setA({ file: f.name }); }}>
          <span>{f.name}</span><span>{f.size}</span></button>
        <button type="button" class="ftrash" disabled={locked || running} onBlur={() => armed === f.name && setArmed(null)}
          aria-label={armed === f.name ? `Confirm: delete ${f.name}` : `Delete ${f.name}`} title={armed === f.name ? "Press again to delete" : "Delete this file"}
          onClick={() => del(f.name)}><TrashIcon />{armed === f.name && <span>Delete</span>}</button>
      </li>;
    })}</ul>
    {locked && <p class="note">You can't change or delete files during a job.</p>}
  </>;
}
