import { useState } from "preact/hooks";
import { SCREENS, STEP_HELP, isSetup, stepName } from "../flow/screens.js";
import { ask, mainActions } from "../flow/asks.jsx";
import { Btn, AppHeader, ModePill, DetailCards, progress, Segs, StepArt, Frame, LogView, JogPad, DetailsBody, FilesBody } from "./parts.jsx";

export const StopBtn = ({ send, label = "Stop" }) => <button class="btn danger" type="button" onClick={() => send("stop")}>{label}</button>;

function SetupBar({ k, ctx, onSettings }) {
  const [open, setOpen] = useState(false);
  const s = SCREENS[k], step = s.step, q = ask(k, ctx);
  const { skipped, label } = progress(step, ctx.a.ran);
  return <div class="banner gtop">
    <div class="gmeta"><span style="display:flex;align-items:center;gap:8px"><ModePill s={s} small />{label}</span><b>{stepName(k)}</b><Segs step={step} skipped={skipped} /></div>
    <div class="gthumb"><StepArt step={step} d={ctx.d} class="" /></div>
    <span class={`gtext${q.alert ? " alert" : ""}`}>{q.text}</span>
    {q.input}
    <div class="bbtns">{q.buttons.map(b => <Btn b={b} />)}
      {ctx.d.connected && !q.buttons.some(b => b.style === "stopping") && <StopBtn send={ctx.send} />}</div>
    {open && <div class="gmore"><div class="gbig"><StepArt step={step} d={ctx.d} class="" /><span>Placeholder: moving line art showing how to {stepName(k).toLowerCase()}</span></div>
      <div class="ghelp"><b>{stepName(k)}</b><ol>{STEP_HELP[step].map(x => <li>{x}</li>)}</ol></div></div>}
    <div class="gtoggle"><button type="button" aria-expanded={open} aria-label={`${open ? "Hide" : "Show"} step details`} onClick={() => setOpen(!open)}>
      <svg viewBox="0 0 16 10" width="16" height="10" aria-hidden="true"><path d="M2 2l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" /></svg></button></div>
  </div>;
}

function ModeBar({ k, ctx, onSettings }) {
  const s = SCREENS[k], q = ask(k, ctx), main = mainActions(k, ctx);
  const showStop = ctx.d.connected && k !== "wifi";
  return <div class="banner combined" style={`--tone:${s.tone}`}>
    <div class="bstate"><ModePill s={s} />{s.sub && <span class="sub">{s.sub}</span>}</div>
    <span class={`bexplain${s.alert || q.alert ? " alert" : ""}`}>{q.text || s.msg}</span>
    {q.input}
    <div class="bbtns">{[...q.buttons, ...main].map(b => <Btn b={b} />)}{showStop && <StopBtn send={ctx.send} />}</div>
  </div>;
}

export function TopBar(props) {
  return isSetup(props.k) ? <SetupBar {...props} /> : <ModeBar {...props} />;
}

// A toggle beside the visual's tabs, for an overlay
const Toggle = ({ on, set, children }) =>
  <button type="button" class={`tgl${on ? " on" : ""}`} aria-pressed={on} onClick={() => set(!on)}>{children}</button>;

export function Desktop({ k, ctx, onSettings, overlay }) {
  const s = SCREENS[k];
  const [logOpen, setLogOpen] = useState(true);
  const [showBelts, setShowBelts] = useState(false);
  const [showMotors, setShowMotors] = useState(false);
  return <div class="screen app">
    {/* One card on top: the app header, a thin line, then the setup guide or the mode bar */}
    <div class="topbox" style={`--tone:${isSetup(k) ? "var(--setup)" : s.tone}`}>
      <AppHeader d={ctx.d} onSettings={onSettings} />
      <TopBar k={k} ctx={ctx} onSettings={onSettings} />
    </div>
    <div class="board layout6">
      {/* The visuals and the device state in one panel: the drawing (with the log, and belt lengths and motor currents when toggled) on
          top, the Status, Position and File cards along the bottom */}
      <section class="panel vis merged" style="grid-area:vis">
        <div class="tabs"><span class="on">Toolpath</span><span>Frame</span><span>3D</span><span class="tabs-gap" />
          <Toggle on={showBelts} set={setShowBelts}>Belts</Toggle><Toggle on={showMotors} set={setShowMotors}>Motors</Toggle></div>
        <div class="visbody">
          <Frame k={k} d={ctx.d} showLengths={showBelts} showCurrents={showMotors} />
          <span class="viscap">{s.nopos && s.moving ? "Position isn't live while the belts move" : s.stale ? "Last known position" : ""}</span>
          <div class="inset" style={logOpen ? "" : "bottom:auto"}><div class="ph">Log<button type="button" class="linkbtn" onClick={() => setLogOpen(!logOpen)}>{logOpen ? "collapse ▾" : "expand ▸"}</button></div>
            {logOpen && <div class="pb"><LogView d={ctx.d} /></div>}</div>
        </div>
        <div class="viscards"><DetailCards k={k} d={ctx.d} a={ctx.a} /></div>
      </section>
      <section class="panel" style="grid-area:comp"><div class="ph">Jog / Home<small></small></div><div class="pb"><JogPad k={k} d={ctx.d} send={ctx.send} /></div></section>
      <section class="panel" style="grid-area:files"><div class="ph">File management<small></small></div><div class="pb"><FilesBody k={k} {...ctx} /></div></section>
    </div>
    {overlay}
  </div>;
}
