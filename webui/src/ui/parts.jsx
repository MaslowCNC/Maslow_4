import { useState } from "preact/hooks";
import { STEPS, SCREENS, isSetup } from "../flow/screens.js";
import { stepArt, visSvg } from "../art.js";

export const Svg = ({ html, class: c }) => <span class={c} dangerouslySetInnerHTML={{ __html: html }} />;

export const ModePill = ({ s, small }) =>
  <span class="mode" style={`--tone:${s.tone}${small ? ";font-size:13px" : ""}`}>{s.mode}</span>;

export function Conn({ d }) {
  const [label, c] = !d.connected ? (d.everConnected ? ["Not responding", "var(--danger)"] : ["Connecting", "var(--warn)"])
    : d.hotspot ? ["Hotspot", "var(--warn)"] : ["Connected", "var(--ready)"];
  return <span class="conn" style={`--c:${c}`}><i></i>{label}</span>;
}

// Setup progress: steps before the current one are done, or skipped if the device was already past them
export function progress(step, ran) {
  const skipped = STEPS.map((_, i) => i < step && i > 0 && !ran.includes(i));
  const n = skipped.filter(Boolean).length;
  return { skipped, label: `${step}/${STEPS.length} done${n ? `, ${n} skipped` : ""}` };
}
export const Segs = ({ step, skipped }) =>
  <div class="segs">{STEPS.map((_, i) => <i class={skipped[i] ? "skip" : i < step ? "done" : i === step ? "now" : ""} />)}</div>;

export const StepArt = ({ step, d, class: c = "art" }) =>
  <div class={c}><Svg html={stepArt(step, `${d.workArea.w} × ${d.workArea.h} mm`)} /></div>;

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
    <div class="row"><button class="btn ghost" type="button" onClick={() => send("setHomeXY")}>Set home XY</button>
      <button class="btn ghost" type="button" onClick={() => send("setZ")}>Set Z</button></div>
    <div class="row"><button class="btn ghost" type="button" onClick={() => send("park")}>Park</button></div>
    {lock && <div class="locked"><div><b>{lock[0]}</b><span>{lock[1]}</span></div></div>}
  </div>;
}

const BELT_NAMES = ["TL", "TR", "BL", "BR"];
export function DetailsBody({ k, d, a }) {
  const [tab, setTab] = useState("job");
  const s = SCREENS[k];
  const nopos = s.nopos;
  const file = d.files.find(f => f.name === (d.job?.file || a.file));
  let body;
  if (tab === "job") {
    body = d.job ? <><div class="bar"><i style="width:38%"></i></div>
      <dl class="kv"><dt>File</dt><dd>{d.job.file}</dd><dt>Line</dt><dd>{d.job.line} / {d.job.total}</dd><dt>Progress</dt><dd>needs a firmware report (issue 19)</dd></dl></>
      : file ? <dl class="kv"><dt>File</dt><dd>{file.name}</dd><dt>Size</dt><dd>{file.w} × {file.h} mm</dd><dt>Est. time</dt><dd>{file.minutes} min</dd><dt>Depth</dt><dd>{file.depth} mm</dd></dl>
      : <p class="note">No job yet. Stats show here once a file is chosen and running.</p>;
  } else if (tab === "belts") {
    body = d.maslow === 0 ? <p class="note">Belt lengths aren't known yet.</p> :
      <><div class={`belts${s.stale ? " stale" : ""}`}>{d.belts.map((v, i) => <><span>{BELT_NAMES[i]}</span><span class="bar"><i style={`width:${Math.min(100, v / 15)}%`}></i></span><span>{v.toFixed(1)} mm</span></>)}</div>
        <p class="note">Live lengths need the planned firmware report. Today they come from $MINFO on request.</p></>;
  } else {
    body = <p class="note">Motor currents need a firmware report. Today they only go to the debug log.</p>;
  }
  return <>
    <div class="minis">
      <div class="mini"><span>Status</span><p class={s.alert ? "alert" : ""}>{s.msg}</p></div>
      <div class="mini"><span>Position</span><PosReadout d={d} nopos={nopos} stale={s.stale} /></div>
      <div class="mini"><span>Work area</span><b>{d.workArea.w} × {d.workArea.h} mm</b><small>spoil board {d.workArea.spoil} mm · stock {d.stock} mm</small></div>
    </div>
    <div class="dstats">
      <div class="dtabs" role="tablist">{[["job", "Job stats"], ["belts", "Belts"], ["motors", "Motors"]].map(([id, n]) =>
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
      onClick={() => { const f = send("uploadFile"); if (a.file || isSetup(k)) { mark(8); setA({ file: f }); } }}>Upload file</button></div>
    <ul class="files">{d.files.map(f =>
      <li class={f.name === a.file ? "sel" : ""}><button type="button" class="fbtn" disabled={locked} onClick={() => { mark(8); setA({ file: f.name }); }}>
        <span>{f.name}</span><span>{f.size}</span></button></li>)}</ul>
    {locked && <p class="note">You can't change the file during a job.</p>}
  </>;
}
