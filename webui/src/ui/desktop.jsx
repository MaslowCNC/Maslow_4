import { useState } from "preact/hooks";
import { SCREENS, STEP_HELP, isSetup, stepName } from "../flow/screens.js";
import { ask, mainActions } from "../flow/asks.jsx";
import { ModePill, Conn, progress, Segs, StepArt, Frame, LogView, JogPad, DetailsBody, FilesBody } from "./parts.jsx";

const Btn = ({ b }) => <button class={`btn ${b.style}`} type="button" onClick={b.onClick} disabled={b.disabled}>{b.label}</button>;
export const StopBtn = ({ send, label = "Stop" }) => <button class="btn danger" type="button" onClick={() => send("stop")}>{label}</button>;
export const Gear = ({ onClick }) => <button class="gear" type="button" aria-label="Settings" onClick={onClick}>⚙</button>;

export const FINAL_CHECKS = ["The file looks right in the preview", "The material thickness matches", "The anchor points are secure", "The spoil board is in place"];

export function FinalChecks({ checks, setChecks }) {
  return <div class="bchecks" role="group" aria-label="Final check"><b>Final check</b>
    {FINAL_CHECKS.map((c, i) => <label><input type="checkbox" checked={checks[i]} onChange={e => { const on = e.currentTarget.checked; setChecks(prev => prev.map((x, j) => j === i ? on : x)); }} /> {c}</label>)}
    <span class="note">Start unlocks once all four are checked.</span></div>;
}

function SetupBar({ k, ctx, onSettings }) {
  const [open, setOpen] = useState(false);
  const s = SCREENS[k], step = s.step, q = ask(k, ctx);
  const { skipped, label } = progress(step, ctx.a.ran);
  return <div class="banner gtop">
    <div class="gthumb"><StepArt step={step} d={ctx.d} class="" /></div>
    <div class="gmeta"><span style="display:flex;align-items:center;gap:8px"><ModePill s={s} small />{label}</span><b>{stepName(k)}</b><Segs step={step} skipped={skipped} /></div>
    <span class="gtext">{q.text}</span>
    {q.input}
    <div class="bbtns">{q.buttons.map(b => <Btn b={b} />)}</div>
    <Conn d={ctx.d} /><Gear onClick={onSettings} />
    {open && <div class="gmore"><div class="gbig"><StepArt step={step} d={ctx.d} class="" /><span>Placeholder: moving line art showing how to {stepName(k).toLowerCase()}</span></div>
      <div class="ghelp"><b>{stepName(k)}</b><ol>{STEP_HELP[step].map(x => <li>{x}</li>)}</ol></div></div>}
    <div class="gtoggle"><button type="button" aria-expanded={open} aria-label={`${open ? "Hide" : "Show"} step details`} onClick={() => setOpen(!open)}>
      <svg viewBox="0 0 16 10" width="16" height="10" aria-hidden="true"><path d="M2 2l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" /></svg></button></div>
  </div>;
}

function ModeBar({ k, ctx, onSettings, checks, setChecks }) {
  const s = SCREENS[k], q = ask(k, ctx), main = mainActions(k, ctx);
  const allChecked = checks.every(Boolean);
  const final = k === "finalcheck" ? [{ label: "Back", onClick: () => ctx.setA({ finalCheck: false }), style: "ghost" },
    { label: `Start ${ctx.a.file}`, style: "go", disabled: !allChecked, onClick: () => { ctx.send("run", { file: ctx.a.file }); ctx.setA({ finalCheck: false }); } }] : [];
  const showStop = ctx.d.connected && k !== "wifi";
  return <div class="banner combined" style={`--tone:${s.tone}`}>
    <div class="bstate"><ModePill s={s} />{s.sub && <span class="sub">{s.sub}</span>}</div>
    <span class={`bexplain${s.alert ? " alert" : ""}`}>{q.text || s.msg}</span>
    {q.input}
    <div class="bbtns">{[...q.buttons, ...main, ...final].map(b => <Btn b={b} />)}{showStop && <StopBtn send={ctx.send} />}</div>
    <Conn d={ctx.d} /><Gear onClick={onSettings} />
    {k === "finalcheck" && <FinalChecks checks={checks} setChecks={setChecks} />}
  </div>;
}

export function TopBar(props) {
  return isSetup(props.k) ? <SetupBar {...props} /> : <ModeBar {...props} />;
}

export function Desktop({ k, ctx, onSettings, checks, setChecks, overlay }) {
  const s = SCREENS[k], setup = isSetup(k);
  const [logOpen, setLogOpen] = useState(true);
  return <div class="screen app">
    <TopBar k={k} ctx={ctx} onSettings={onSettings} checks={checks} setChecks={setChecks} />
    <div class="board layout6">
      <section class="panel vis" style="grid-area:vis">
        <div class="tabs"><span class="on">Toolpath</span><span>Frame</span><span>3D</span></div>
        <Frame k={k} d={ctx.d} />
        <span class="viscap">{s.nopos && s.moving ? "Position isn't live while the belts move" : s.stale ? "Last known position" : ""}</span>
        <div class="inset" style={logOpen ? "" : "bottom:auto"}><div class="ph">Log<button type="button" class="linkbtn" onClick={() => setLogOpen(!logOpen)}>{logOpen ? "collapse ▾" : "expand ▸"}</button></div>
          {logOpen && <div class="pb"><LogView d={ctx.d} /></div>}</div>
      </section>
      <section class="panel" style="grid-area:comp"><div class="ph">Jog / Home<small></small></div><div class="pb"><JogPad k={k} d={ctx.d} send={ctx.send} /></div></section>
      <section class="panel" style="grid-area:cfg">
        {setup
          ? <div class="ph pinned"><span style="display:flex;align-items:center;gap:8px;min-width:0"><ModePill s={s} small /><span class="sub" style="color:var(--text);font-family:var(--font-sans)">{s.sub}</span></span>
              <span class="hbtns"><div class="row"><button class="btn go" type="button" disabled title="Finish setup first">Run</button><StopBtn send={ctx.send} /></div></span></div>
          : <div class="ph">Device details<small>{s.stale ? "out of date" : "live"}</small></div>}
        <div class="pb"><DetailsBody k={k} d={ctx.d} a={ctx.a} /></div>
      </section>
      <section class="panel" style="grid-area:files"><div class="ph">File management<small></small></div><div class="pb"><FilesBody k={k} {...ctx} /></div></section>
    </div>
    {overlay}
  </div>;
}
