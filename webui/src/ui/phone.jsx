import { useEffect, useRef, useState } from "preact/hooks";
import { SCREENS, isSetup, stepName } from "../flow/screens.js";
import { ask, mainActions } from "../flow/asks.jsx";
import { Btn, AppHeader, ModePill, progress, Segs, StepArt, Frame, VisReadouts, LogView, CommandBox, SaveLogBtn, JogPad, DetailCards, DetailsBody, FilesBody, jogLock, BackArrow, mainButtons } from "./parts.jsx";
import "./phone.css";

// Phone layout: the mode is pinned on top and Run and Stop at the bottom (down the right edge in landscape). Between them a
// rail of tabs down the left edge, each showing one panel that fits the screen, with no long scroll.

const P = {
  next: "M9 6h11M9 12h11M9 18h11M3.5 6l1.5 1.5L7.5 5M3.5 12l1.5 1.5 2.5-2.5M3.5 18l1.5 1.5 2.5-2.5",
  frame: "M3 4h18v16H3zM3 4l9 8 9-8M3 20l9-8 9 8",
  jog: "M12 3v18M3 12h18M12 3 9 6M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3",
  files: "M14 3H6a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V8zM14 3v5h5",
  info: "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 11v6M12 7.5v.5",
  log: "M4 5h16v14H4zM7.5 9.5l3 2.5-3 2.5M12.5 15h4"
};
const Icon = ({ id }) => <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="1.8"
  stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d={P[id]} /></svg>;

// The open tab, remembered in this browser
const TAB_KEY = "maslow.phoneTab";
const TABS = ["next", "frame", "jog", "files", "info", "log"];
function useTab() {
  const [tab, setTabState] = useState(() => { try { const t = localStorage.getItem(TAB_KEY); return TABS.includes(t) ? t : "next"; } catch { return "next"; } });
  const set = (t) => { setTabState(t); try { localStorage.setItem(TAB_KEY, t); } catch { /* not remembered */ } };
  return [tab, set];
}

const Toggle = ({ on, set, children }) =>
  <button type="button" class={`tgl${on ? " on" : ""}`} aria-pressed={on} onClick={() => set(!on)}>{children}</button>;

function FrameTab({ k, ctx }) {
  const [view, setView] = useState("frame");
  const [belts, setBelts] = useState(false);
  const [motors, setMotors] = useState(false);
  const [ws, setWs] = useState(true);
  const s = SCREENS[k];
  return <div class="pt-frame">
    <div class="pt-tools">
      {[["frame", "Frame"], ["toolpath", "Toolpath"]].map(([id, label]) =>
        <Toggle on={view === id} set={() => setView(id)}>{label}</Toggle>)}<span class="tabs-gap" />
      <Toggle on={belts} set={setBelts}>Belts</Toggle><Toggle on={motors} set={setMotors}>Motors</Toggle><Toggle on={ws} set={setWs}>Area</Toggle>
    </div>
    <div class="pt-draw">
      <Frame k={k} d={ctx.d} view={view} showLengths={belts} showCurrents={motors} showWorkspace={ws} tolerance={ctx.tolerance} sledMass={ctx.sledMass} />
      {view === "toolpath" && <VisReadouts d={ctx.d} belts={belts} motors={motors} />}
    </div>
    {(s.nopos || s.stale) && <span class="note">{s.nopos && s.moving ? "Position isn't live while the belts move" : s.stale ? "Last known position" : ""}</span>}
  </div>;
}

// Setup: the step, its drawing and its question, with the answers at the bottom of the panel, by the thumb.
// Otherwise (a job running, Ready to cut): the frame drawing, the Position and File cards, and what's next, all at once.
function NextTab({ k, ctx, q }) {
  const s = SCREENS[k], setup = isSetup(k), btns = mainButtons(q.buttons);
  return <div class={`pt-next${setup ? "" : " pt-run"}`}>
    {setup && <h3 class="pt-step">{stepName(k)}</h3>}
    {setup ? <StepArt step={s.step} d={ctx.d} class="art pt-art" />
      : <><div class="pt-draw pt-small"><Frame k={k} d={ctx.d} showWorkspace tolerance={ctx.tolerance} sledMass={ctx.sledMass} /></div>
        <DetailCards k={k} d={ctx.d} a={ctx.a} /></>}
    <div class="pt-ask">
      <p class={`pt-q${q.alert || s.alert ? " alert" : ""}`}>{q.text || s.msg}</p>
      {q.input}
      {btns.length > 0 && <div class="row">{btns.map(b => <Btn b={b} />)}</div>}
    </div>
  </div>;
}

export function Phone({ k, ctx, onSettings, overlay }) {
  const s = SCREENS[k], setup = isSetup(k), q = ask(k, ctx);
  const [tab, setTab] = useTab();
  const { skipped, label } = setup ? progress(s.step, ctx.a.ran) : {};
  const lock = jogLock(k, ctx.d);
  const main = mainActions(k, ctx).map(b => ({ ...b, label: b.label.replace(/^Run .*/, "Run").replace(/^Continue from .*/, "Continue") }));
  // An incident or an error brings the Next tab forward, so it's never missed behind another tab
  const alerting = s.mode === "Error" || !!s.alert;
  const was = useRef(alerting);
  useEffect(() => { if (alerting && !was.current) setTab("next"); was.current = alerting; }, [alerting]);
  // A dot on Setup / Next when it asks something and another tab is open
  const asking = mainButtons(q.buttons).length > 0 || !!q.input || alerting;
  const tabs = [
    ["next", setup ? "Setup" : "Next", "next"],
    ["frame", "Frame", "frame"],
    ["jog", "Jog", "jog"],
    ["files", "Files", "files"],
    ["info", "Details", "info"],
    ["log", "Log", "log"]
  ];
  let body;
  if (tab === "frame") body = <FrameTab k={k} ctx={ctx} />;
  else if (tab === "jog") body = <div class="pt-jog">{lock && <p class="note">{lock[0]}</p>}<div class="jogbox"><JogPad k={k} d={ctx.d} send={ctx.send} /></div></div>;
  else if (tab === "files") body = <div class="pt-files"><FilesBody k={k} {...ctx} upload /></div>;
  else if (tab === "info") body = <div class="pt-info"><DetailsBody k={k} d={ctx.d} a={ctx.a} /></div>;
  else if (tab === "log") body = <div class="logbody pt-log"><div class="fhead"><span>The last 200 lines</span><SaveLogBtn d={ctx.d} /></div><LogView d={ctx.d} />{ctx.commandBox && <CommandBox d={ctx.d} send={ctx.send} />}</div>;
  else body = <NextTab k={k} ctx={ctx} q={q} />;
  return <div class="phoneapp tabbed">
    <header class="p-head">
      <AppHeader d={ctx.d} onSettings={onSettings} notice={ctx.notice} setNotice={ctx.setNotice} />
      <div class="p-top"><BackArrow buttons={q.buttons} /><ModePill s={s} /><span class="sub">{s.sub}</span></div>
      {setup ? <div class="p-prog"><span>{label}</span><Segs step={s.step} skipped={skipped} /></div>
        : <div class={`p-msg${s.alert || q.alert ? " alert" : ""}`}>{q.text || s.msg}</div>}
    </header>
    <main class="p-main">
      <nav class="p-rail" role="tablist" aria-label="Panels" aria-orientation="vertical">
        {tabs.map(([id, name, icon]) => <button type="button" role="tab" aria-selected={tab === id} class={id === "next" && asking && tab !== "next" ? "dot" : ""}
          onClick={() => setTab(id)}><Icon id={icon} /><span>{name}</span></button>)}
      </nav>
      <section class="p-panel" role="tabpanel">{body}</section>
    </main>
    {overlay}
    <footer class="p-act"><div class="row">
      {setup ? <button class="btn go" type="button" disabled>Run</button> : main.map(b => <Btn b={b} />)}
      {ctx.d.connected && k !== "wifi" && <button class="btn danger" type="button" onClick={() => ctx.send("stop")}>Stop</button>}
    </div></footer>
  </div>;
}
