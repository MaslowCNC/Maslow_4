import { useState } from "preact/hooks";
import { STEPS, SCREENS, isSetup } from "../flow/screens.js";
import { stepArt, visSvg } from "../art.js";

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

export const Frame = ({ k, d }) => {
  const s = SCREENS[k];
  return <Svg class="framesvg" html={visSvg(k, s, d.pos, d.wco)} />;
};

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

const STEP_SIZES = [0.1, 1, 10];
export function JogPad({ k, d, send }) {
  // A step size is one of the presets, or "custom": the user's own value from the box at the end
  const [choice, setChoice] = useState(1);
  const [custom, setCustom] = useState("50");
  const customMm = parseFloat(custom);
  const size = choice === "custom" ? (customMm > 0 ? customMm : 0) : choice;
  const lock = jogLock(k, d);
  const j = (dx, dy, dz = 0) => send("jog", { dx: dx * size, dy: dy * size, dz: dz * size });
  return <div class="jogwrap">
    <div class="chips">{STEP_SIZES.map(v => <button type="button" class={v === choice ? "on" : ""} onClick={() => setChoice(v)}>{v}</button>)}
      <label class={`chipin${choice === "custom" ? " on" : ""}`}><input type="text" inputmode="decimal" value={custom} aria-label="Your own step size in mm"
        onFocus={() => setChoice("custom")} onClick={() => setChoice("custom")} onInput={e => { setCustom(e.currentTarget.value); setChoice("custom"); }} /><span>mm</span></label></div>
    <div class="compass">
      <button type="button" aria-label="Up left" onClick={() => j(-1, 1)}>↖</button><button type="button" aria-label="Up" onClick={() => j(0, 1)}>↑</button>
      <button type="button" aria-label="Up right" onClick={() => j(1, 1)}>↗</button><span></span><button type="button" class="z" aria-label="Z up" onClick={() => j(0, 0, 1)}>Z+</button>
      <button type="button" aria-label="Left" onClick={() => j(-1, 0)}>←</button><button type="button" class="home" title="Go to your home (work 0, 0)" onClick={() => send("jog", { dx: d.wco.x - d.pos.x, dy: d.wco.y - d.pos.y })}>HOME</button>
      <button type="button" aria-label="Right" onClick={() => j(1, 0)}>→</button><span></span><button type="button" class="z" aria-label="Z down" onClick={() => j(0, 0, -1)}>Z−</button>
      <button type="button" aria-label="Down left" onClick={() => j(-1, -1)}>↙</button><button type="button" aria-label="Down" onClick={() => j(0, -1)}>↓</button>
      <button type="button" aria-label="Down right" onClick={() => j(1, -1)}>↘</button>
    </div>
    <PosReadout d={d} nopos={!!lock && SCREENS[k].nopos} />
    <div class="row"><NotYetBtn d={d} id="setHomeXY" label="Set home XY" send={send} />
      <NotYetBtn d={d} id="setZ" label="Set Z" send={send} /></div>
    <div class="row"><NotYetBtn d={d} id="park" label="Park" send={send} /></div>
    {lock && <div class="locked"><div><b>{lock[0]}</b><span>{lock[1]}</span></div></div>}
  </div>;
}

const BELT_NAMES = ["TL", "TR", "BL", "BR"];
// The machine this page is connected to, as a small tag beside a panel title ("Maslow Mini")
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

export function DetailsBody({ k, d, a }) {
  const [tab, setTab] = useState("belts");
  const s = SCREENS[k];
  const nopos = s.nopos;
  let body;
  if (tab === "belts") {
    body = d.maslow === 0 ? <p class="note">Belt lengths aren't known yet.</p> :
      <><div class={`belts${s.stale ? " stale" : ""}`}>{d.belts.map((v, i) => <><span>{BELT_NAMES[i]}</span><span class="bar"><i style={`width:${Math.min(100, v / 15)}%`}></i></span><span>{v.toFixed(1)} mm</span></>)}</div>
        <p class="note">Live, from the status report.</p></>;
  } else if (d.motorCurrent) {
    // MC is raw ADC 0–4095; the current UI converts it to mA this way (docs/protocol.md)
    const mA = (adc) => Math.round(1000 * 3.3 * adc / 4095 / 1.5);
    const mc = d.motorCurrent;
    body = <><div class={`belts${s.stale ? " stale" : ""}`}>{[mc.tl, mc.tr, mc.bl, mc.br].map((v, i) => <><span>{BELT_NAMES[i]}</span><span class="bar"><i style={`width:${Math.min(100, mA(v) / 20)}%`}></i></span><span>{mA(v)} mA</span></>)}</div>
      <p class="note">Belt motor currents, live from the status report.</p></>;
  } else {
    body = <p class="note">Motor currents show here when the machine reports them.</p>;
  }
  return <>
    <div class="minis">
      <div class="mini"><span>Status</span><p class={s.alert ? "alert" : ""}>{s.msg}</p></div>
      <div class="mini"><span>Position</span><PosReadout d={d} nopos={nopos} stale={s.stale} /></div>
      <FileCard d={d} a={a} />
    </div>
    <div class="dstats">
      <div class="dtabs" role="tablist">{[["belts", "Belts"], ["motors", "Motors"]].map(([id, n]) =>
        <button type="button" role="tab" aria-selected={id === tab} onClick={() => setTab(id)}>{n}</button>)}
        <span class={`dlive${s.stale ? " off" : ""}`}>{s.stale ? "out of date" : ""}</span></div>
      <div class="dbody">{body}</div>
    </div>
  </>;
}

export function FilesBody({ k, d, a, setA, send, mark }) {
  const locked = SCREENS[k].mode === "Job";
  return <>
    <div class="row"><button class="btn ghost" type="button" disabled={locked}
      onClick={() => Promise.resolve(send("uploadFile")).then(f => { if (f && (a.file || isSetup(k))) { mark(7); setA({ file: f }); } })}>Upload file</button></div>
    <ul class="files">{d.files.map(f =>
      <li class={f.name === a.file ? "sel" : ""}><button type="button" class="fbtn" disabled={locked} onClick={() => { mark(7); setA({ file: f.name }); }}>
        <span>{f.name}</span><span>{f.size}</span></button></li>)}</ul>
    {locked && <p class="note">You can't change the file during a job.</p>}
  </>;
}
