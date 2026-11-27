import { useState } from "preact/hooks";
import { SCREENS, isSetup } from "../flow/screens.js";
import { ask, mainActions, fitFor } from "../flow/asks.jsx";
import { Frame, LogView, CommandBox, SaveLogBtn, jogLock, Activity, workPos, mainButtons, adcToMilliamps, useJogStep, JogSteps } from "./parts.jsx";
import { fmt, fmtU, prose, numMode, unitName } from "../units.js";
import { available } from "./modals.jsx";
import { SettingsList } from "./advanced.jsx";
import { currentColour, MAX_CURRENT_MA } from "../art.js";
import "./legacy.css";

// The legacy UI (Settings › Display › Interface): the old page's Maslow tab, drawn over the new UI's logic. Same
// derive() screen (with the page-only answers given, as the old page never asked them), same actions through ctx.send, same state from the report. What it doesn't
// bring back: buttons that look greyed but still run (unavailable is always `disabled`), Pause, and a Stop that can
// scroll away (the Play / Stop row stays on screen: sticky on desktop, fixed at the bottom on a phone).

// A tile from the old grid. b is an ask() button ({ label, onClick, style, disabled, title }).
const Tile = ({ b, class: c = "", children }) =>
  <button type="button" class={`cl-tile ${c}`} disabled={!b || b.disabled} title={b?.title || ""} onClick={() => b?.onClick?.()}>{children ?? b?.label}</button>;

// Prompt buttons in the old pop-ups' flat style: green for the step, blue for the alternatives, red for stopping
const PBtn = ({ b }) => <button type="button" class={`cl-pbtn ${b.style === "stopping" ? "red" : b.style === "ghost" ? "blue" : "green"}`}
  disabled={b.disabled} title={b.temp ? `Temporary: ${b.temp}` : b.title} onClick={b.onClick}>{b.back ? "Back" : b.label}</button>;

// The old jog arrows: white arrows on the straight tiles, white corner triangles on the diagonal ones
const Arrow = ({ deg }) => <svg viewBox="0 0 24 24" width="30" height="30" aria-hidden="true" style={`transform:rotate(${deg}deg)`}>
  <path d="M12 3l9 9h-5v9H8v-9H3z" fill="#fff" /></svg>;
const Corner = ({ deg }) => <svg viewBox="0 0 24 24" width="30" height="30" aria-hidden="true" style={`transform:rotate(${deg}deg)`}>
  <path d="M3 3h18L3 21z" fill="#fff" /></svg>;
const House = () => <svg viewBox="0 0 24 24" width="34" height="34" aria-hidden="true"><path d="M12 3l10 9h-3v9h-5v-6h-4v6H5v-9H2z" fill="#9d88c0" /></svg>;
const ZArrow = ({ up }) => <svg viewBox="0 0 12 24" width="12" height="24" aria-hidden="true" style={up ? "" : "transform:rotate(180deg)"}>
  <path d="M6 1l5 6H7v16H5V7H1z" fill="currentColor" /></svg>;

// The tabs: Maslow (the drawing and controls) and Settings, which takes the drawing's place as the old settings tab did
function Nav({ k, d, openSettings, openMenu, settingsOpen, closeSettings }) {
  const [label, c] = !d.connected ? (d.everConnected ? ["Not responding", "#ce654c"] : ["Connecting", "#f5b129"])
    : d.hotspot ? ["Hotspot", "#f5b129"] : ["Connected", "#4aa85c"];
  return <nav class="cl-nav">
    <span class="cl-brand">Maslow{d.machine && <small>{d.machine}</small>}</span>
    <div class="cl-tabs" role="tablist">
      <button type="button" role="tab" class={settingsOpen ? "" : "on"} aria-selected={!settingsOpen} onClick={closeSettings}>Maslow</button>
      <button type="button" role="tab" class={settingsOpen ? "on" : ""} aria-selected={settingsOpen} onClick={openSettings}>Settings</button>
    </div>
    <span class="cl-gap" />
    <Activity k={k} d={d} />
    {d.spindle === "on" && <span class="cl-spin" role="status">Spindle running</span>}
    <span class="cl-dot" style={`--c:${c}`} title={label} role="status" aria-label={label} />
    <button type="button" class="cl-menu" aria-label="More settings" onClick={() => openMenu()}>
      <svg viewBox="0 0 20 16" width="20" height="16" aria-hidden="true"><path d="M1 2h18M1 6h18M1 10h18M1 14h18" stroke="currentColor" stroke-width="2" /></svg></button>
  </nav>;
}

// The old Setup pop-up: every belt action in the same places, each enabled only when the machine can do it now
const POP = [[["retract", "Retract All"], ["extend", "Extend All"], ["tension", "Apply Tension"]],
  [["findAnchors", "Find Anchor Locations"], ["stop", "Stop"], ["release", "Release Tension"]],
  [["rehomeZ", "Home Z"], ["removeTool", "Remove Tool"], ["clearError", "Clear Error"]]];
function SetupPopup({ k, ctx, close }) {
  const { d, send } = ctx, s = SCREENS[k], on = available(k, d);
  return <div class="cl-pop-bg" onClick={e => { if (e.target === e.currentTarget) close(); }}>
    <div class="cl-pop" role="dialog" aria-modal="true" aria-label="Setup">
      <h3>State: {s.sub || s.mode}</h3><hr />
      {POP.map(row => <div class="cl-poprow">{row.map(([id, label]) => {
        const ok = id === "stop" || (on.includes(id) && !d.notYet?.[id]);
        return <button type="button" class={`cl-pbtn ${id === "stop" ? "red" : "green"}`} disabled={!ok}
          title={ok ? "" : d.notYet?.[id] || "Not available in this state"} onClick={() => { close(); send(id); }}>{label}</button>;
      })}</div>)}
      <hr />
      <div class="cl-poprow">
        <button type="button" class="cl-pbtn lilac" onClick={() => { close(); ctx.openSettings("frame"); }}>Config</button>
        <button type="button" class="cl-pbtn lilac" onClick={() => { close(); ctx.openSettings("advanced"); }}>Test</button>
        <button type="button" class="cl-pbtn blue" onClick={close}>Close</button>
      </div>
    </div>
  </div>;
}

// Belt lengths and each motor's current as a small dial, as in the old status section
function Belts({ d, stale }) {
  const mc = d.motorCurrent, mA = mc ? [mc.tl, mc.tr, mc.bl, mc.br].map(adcToMilliamps) : [null, null, null, null];
  return <div class={`cl-belts${stale ? " stale" : ""}`}><span class="cl-beltlabel">Belt lengths (mm) and motor current</span>
    {["TL", "TR", "BL", "BR"].map((n, i) => {
      const v = d.belts[i], unknown = d.maslow === 0 || d.beltUnknown?.[i] || v == null;
      const pct = mA[i] == null ? 0 : Math.min(100, mA[i] / MAX_CURRENT_MA * 100);
      return <span class="cl-belt" title={mA[i] == null ? "" : `${n} motor: ${Math.round(mA[i])} mA`}>{n} {unknown ? "-" : v.toFixed(1)}
        <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><circle cx="12" cy="12" r="9" fill="none" stroke="#ddd" stroke-width="3" />
          <circle cx="12" cy="12" r="9" fill="none" stroke={currentColour(pct)} stroke-width="3" stroke-dasharray={`${pct * 0.5655} 56.55`} transform="rotate(-90 12 12)" /></svg></span>;
    })}</div>;
}

// Steps are typed in the page's units (Settings › Display, or the units button here, as the old page's Toggle Units)
// and sent as mm. Unlike the old page, toggling never sends G20 / G21 to the machine.
function Jog({ k, d, send, u, setUnits }) {
  const [step, setStep, xy] = useJogStep(u, "xy"), [zStep, setZStep, z] = useJogStep(u, "z");
  const lock = jogLock(k, d), s = SCREENS[k];
  const j = (dx, dy) => send("jog", { dx: dx * xy, dy: dy * xy, dz: 0 });
  const off = !!lock;
  const ny = (id) => off || !!d.notYet?.[id];
  const tip = (id) => lock ? lock[0] : d.notYet?.[id] || "";
  const dir = (dx, dy, deg, name, corner) => <button type="button" class={`cl-tile ${corner ? "jd" : "jc"}`} disabled={off || !xy}
    aria-label={`${name} ${step} ${u}`} title={tip()} onClick={() => j(dx, dy)}>{corner ? <Corner deg={deg} /> : <Arrow deg={deg} />}</button>;
  const w = workPos(d), m = d.pos, f = (v) => s.nopos ? "–" : fmt(v, u, "position");
  return <><JogSteps u={u} />
    <div class="cl-jog">
      <button type="button" class="cl-tile cream zb" disabled={off || !z} aria-label={`Z up ${zStep} ${u}`} title={tip()} onClick={() => send("jog", { dx: 0, dy: 0, dz: z })}><ZArrow up /> Z</button>
      {dir(-1, 1, 0, "Up left", true)}{dir(0, 1, 0, "Up")}{dir(1, 1, 90, "Up right", true)}
      <label class="cl-tile cream zb"><input value={zStep} inputmode={numMode(u)} list="jogsteps" aria-label={`Z step in ${unitName(u)}`} onInput={e => setZStep(e.currentTarget.value)} /></label>
      {dir(-1, 0, -90, "Left")}
      <label class="cl-tile cream"><input value={step} inputmode={numMode(u)} list="jogsteps" aria-label={`XY step in ${unitName(u)}`} onInput={e => setStep(e.currentTarget.value)} /></label>
      {dir(1, 0, 90, "Right")}
      <button type="button" class="cl-tile cream zb" disabled={off || !z} aria-label={`Z down ${zStep} ${u}`} title={tip()} onClick={() => send("jog", { dx: 0, dy: 0, dz: -z })}><ZArrow /> Z</button>
      {dir(-1, -1, -90, "Down left", true)}{dir(0, -1, 180, "Down")}{dir(1, -1, 180, "Down right", true)}
      <button type="button" class="cl-tile cream zb" disabled={ny("setZ")} title={tip("setZ") || "Make where the bit is now work Z 0"} onClick={() => send("setZ")}>Define Z Home</button>
      <button type="button" class="cl-tile cream" disabled={off} aria-label="Go to your home (work 0, 0)" title={tip() || "Go to your home (work 0, 0)"}
        onClick={() => send("jog", { dx: d.wco.x - d.pos.x, dy: d.wco.y - d.pos.y })}><House /></button>
      <button type="button" class="cl-tile cream" disabled={ny("rehomeZ")} title={tip("rehomeZ") || "Raise Z to the light beam to find it again"} onClick={() => send("rehomeZ")}>Home Z</button>
      <button type="button" class="cl-tile cream" disabled={ny("setHomeXY")} title={tip("setHomeXY") || "Make where the sled is now work X 0, Y 0"} onClick={() => send("setHomeXY")}>Define XY Home</button>
    </div>
    {lock && <p class="cl-lock"><b>{lock[0]}.</b> {lock[1]}</p>}
    <button type="button" class="cl-units" title="Toggle Units: what positions and steps are shown and typed in. The machine is always sent mm."
      onClick={() => setUnits(u === "in" ? "mm" : "in")}>{u === "in" ? "inch" : "mm"}</button>
    <table class={`cl-pos${s.stale ? " stale" : ""}`}><tbody>
      {["x", "y", "z"].map(a => <tr><td>{a.toUpperCase()}:</td><td>{f(w[a])}</td><td class="cl-mpos">machine {f(m[a])}</td></tr>)}
    </tbody></table>
  </>;
}

// File list, upload and delete (delete asks twice, as in the new UI)
function Files({ k, ctx }) {
  const { d, a, setA, send, mark } = ctx;
  const locked = SCREENS[k].mode === "Job";
  const [armed, setArmed] = useState(false);
  const canDel = a.file && !locked && d.job?.file !== a.file;
  return <div class="cl-files">
    <select value={a.file || ""} disabled={locked} aria-label="Job file" onChange={e => { const v = e.currentTarget.value; if (v) { mark(7); setA({ file: v }); } }}>
      <option value="" disabled>Load GCode File from SD…</option>
      {d.files.map(f => <option value={f.name}>{f.name}</option>)}</select>
    <button type="button" class="cl-tile blue" disabled={locked} title={locked ? "You can't upload during a job" : "Upload a G-code file"}
      onClick={() => Promise.resolve(send("uploadFile")).then(f => { if (f && (a.file || isSetup(k))) { mark(7); setA({ file: f }); } })}>Upload GCode</button>
    <button type="button" class="cl-tile red" disabled={!canDel} onBlur={() => setArmed(false)}
      title={armed ? "Press again to delete" : canDel ? `Delete ${a.file}` : "Choose a file first"}
      onClick={() => { if (!armed) return setArmed(true); setArmed(false); const n = a.file; setA({ file: null }); send("deleteFile", { name: n }); }}>{armed ? "Press again to delete" : "Delete GCode"}</button>
  </div>;
}

// Play, Stop and the machine's state; after a stop, Resume (with its line) and Discard take Play's and the state's places
function Playback({ k, ctx, q, main }) {
  const { d, a, setA, send } = ctx, s = SCREENS[k];
  const play = k === "ready" ? main.find(b => b.style === "go")
    : { disabled: true, title: d.job ? "The job is running" : d.maslow !== 7 ? "Apply tension first" : !a.file ? "Choose a file first" : "Not available now" };
  const stateText = !d.connected ? "Offline" : d.job && d.job.total > 0 ? `Run ${Math.min(100, Math.round(100 * d.job.line / d.job.total))}%` : d.fluid || "–";
  const bad = !d.connected || d.fluid === "Alarm" || s.mode === "Error";
  const stopBtn = <button type="button" class="cl-tile red cl-stop" disabled={!d.connected} aria-label="Stop" title="Stop: ends the job and keeps its line, spindle off"
    onClick={() => send("stop")}><svg viewBox="0 0 24 24" width="34" height="34" aria-hidden="true"><rect x="4" y="4" width="16" height="16" fill="#fff" /></svg></button>;
  if (k === "stopped") {
    const cont = main.find(b => b.style === "go"), discard = q.buttons.find(b => b.label === "Discard");
    return <div class="cl-play">
      <div class="cl-tile green cl-resume"><button type="button" class="cl-resbtn" disabled={cont.disabled} title={cont.title || cont.label} onClick={cont.onClick}>Resume from</button>
        <input value={a.contLine ?? String(d.lastJob?.line ?? 0)} inputmode="numeric" aria-label="Line to resume from" onInput={e => setA({ contLine: e.currentTarget.value })} /></div>
      {stopBtn}
      <Tile b={discard} class="cream">Discard stopped job</Tile>
    </div>;
  }
  return <div class="cl-play">
    <Tile b={play} class={play?.disabled ? "grey" : "green"}><span class="sr">{play?.label || "Run"}</span>
      <svg viewBox="0 0 24 24" width="34" height="34" aria-hidden="true"><path d="M6 3l15 9-15 9z" fill="#fff" /></svg></Tile>
    {stopBtn}
    <div class={`cl-tile cl-status${bad ? " red" : " green"}`} role="status">{stateText}</div>
  </div>;
}

function Bounds({ k, ctx, main }) {
  const { d, a } = ctx;
  if (!a.file) return null;
  const g = d.gcode?.file === a.file ? d.gcode : null, e = g?.status === "ready" && g.extent;
  const u = ctx.units, fit = fitFor(d, a, u), c = (v) => fmt(v, u, "fine");
  const trace = main.find(b => b.label === "Trace boundary") || { disabled: true, title: "Only when ready to cut" };
  return <div class="cl-bounds">
    <Tile b={trace} class="cream">Trace Boundary</Tile>
    <div class="cl-boundsinfo"><b>Job Bounds:</b><br />{g?.status === "reading" ? "Reading the file…" : g?.status === "failed" ? `Couldn't read the file: ${g.detail || ""}`
      : e ? `X ${c(e.min.x)} to ${c(e.max.x)}, Y ${c(e.min.y)} to ${c(e.max.y)} ${u}, ${fmtU(Math.max(0, -e.min.z), u, "fine")} deep` : "No file loaded"}
      {fit?.text && <span class={fit.level === "outside" || fit.level === "pastFrame" ? "cl-warn" : ""}><br />{fit.text}</span>}</div>
  </div>;
}

// The page-only answers the new flow asks for (retract checked by eye, tool and Z, work piece secure). The old page
// never asked them, so the legacy UI answers them for derive(): after tension it's Ready to cut, and Run needs only a file.
// While the spindle board moves Z (Home Z, Remove tool, a bit being pulled in) the Z answer is left out, so derive()
// shows that step as moving: jogging, Park and Run stay locked until it's done.
const Z_MOVING = ["checking", "removing", "loading"];
export const legacyAnswers = (a, d) => ({ ...a, retractChecked: true, zDone: !Z_MOVING.includes(d.tool), secured: true });

// The old page's state label: the firmware's Maslow state, with its colours (maslow.js)
const BLUE = "#cfe2ff", GREEN = "#d1e7dd", YELLOW = "#fff3cd", RED = "#f8d7da", GREY = "#e9ecef";
const OLD_STATE = { 0: ["Unknown", RED], 1: ["Retracting", BLUE], 2: ["Retracted", GREEN], 3: ["Extending", BLUE], 4: ["Extended", YELLOW],
  5: ["Taking Slack", BLUE], 6: ["Finding Anchors", BLUE], 7: ["Ready to Cut", GREEN], 8: ["Releasing Tension", BLUE],
  9: ["Find Anchors Computing", BLUE], 10: ["Tensioned", YELLOW] };
// The old tile's wording for the same actions
const OLD_LABEL = { "Retract belts": "Retract", "Extend belts": "Extend", "Extend again": "Extend", "Done extending": "Done Extending",
  "Apply tension": "Apply Tension", "Continue applying tension": "Apply Tension", "Find anchors": "Find Anchors",
  "Find anchors again": "Find Anchors", "Clear error ($X)": "Clear Alarm" };
function stateLabel(k, d) {
  const s = SCREENS[k];
  if (d.closed || k === "lost" || k === "incident" || k === "recovery" || k === "spindle") return [s.sub, RED];
  if (!d.connected) return ["Connecting", GREY];
  if (d.hotspot) return ["WiFi setup", YELLOW];
  if (d.fluid === "Alarm") return ["Alarm", RED];
  if (s.alert) return [s.sub, RED];
  if (d.maslow === 7 && (s.moving || Z_MOVING.includes(d.tool))) return [d.tool === "checking" ? "Homing Z" : s.sub, BLUE];
  return OLD_STATE[d.maslow] || ["Unknown", RED];
}

// Something the machine reported that needs acknowledging, as the old page's alert pop-up. It covers only the
// drawing, so Stop stays reachable.
function AlertPopup({ d, b }) {
  const i = d.incident;
  return <div class="cl-pop-bg"><div class="cl-pop" role="alertdialog" aria-modal="true" aria-label={i.title}>
    <h3>{i.title}</h3><hr /><p class="cl-alerttext">{i.detail}</p><hr />
    {/* OK, not the new flow's wording ("Got it, check the tool"): the legacy UI has no tool step. Stop to clear stays as it is */}
    <div class="cl-alertrow">{b && <PBtn b={b.style === "stopping" ? b : { ...b, label: "OK" }} />}</div></div></div>;
}

// The old page's settings tab: a title, Restart and Refresh, a choice of list, and the lists themselves (the same
// lists, reads and saves as Settings › Advanced). Everything else is in the full Settings, from the ☰ menu.
function SettingsTab({ k, ctx }) {
  const { d, send } = ctx;
  const [list, setList] = useState("maslow");
  const [filter, setFilter] = useState("");
  const canRestart = available(k, d).includes("reboot");
  return <div class="cl-settings">
    <h2>Settings</h2>
    <div class="cl-setbtns">
      <button type="button" class="bs danger" disabled={!canRestart} title={canRestart ? "Restart FluidNC" : "Not while the machine moves"} onClick={() => send("reboot")}>Restart</button>
      <button type="button" class="bs primary" disabled={!d.connected || d.settingsList?.status === "reading"} title="Refresh this list of settings" onClick={() => send("readSettingsList")}>Refresh</button>
    </div>
    <hr />
    <div class="cl-setfilters" role="radiogroup" aria-label="Settings list">
      {[["maslow", "Maslow Settings"], ["fluidnc", "FluidNC Settings"]].map(([id, n]) =>
        <label><input type="radio" name="cl-list" checked={list === id} onChange={() => setList(id)} /> {n}</label>)}
      <input type="search" placeholder="Filter" aria-label="Filter the settings" value={filter} onInput={e => setFilter(e.currentTarget.value)} />
    </div>
    <div class="cl-list"><SettingsList key={list} list={list} k={k} ctx={ctx} filter={filter} /></div>
  </div>;
}

export function Legacy({ k, ctx, overlay, closeOverlay, narrow, onNewUI }) {
  const { d } = ctx, q = ask(k, ctx), main = mainActions(k, ctx), s = SCREENS[k];
  const [setupOpen, setSetupOpen] = useState(false);
  const [view, setView] = useState("frame");
  const [tab, setTab] = useState("maslow");   // the navbar tab: the drawing, or the settings lists
  const btns = mainButtons(q.buttons);
  // The green action tile carries the state's one action, as the old one did (Retract, Extend, Apply Tension, Find
  // Anchors, Park when Ready to Cut and idle). The rest of a state's choices are in the Setup pop-up.
  const park = d.maslow === 7 && !d.job ? { label: "Park", onClick: () => ctx.send("park"), disabled: !!jogLock(k, d) || !!d.notYet?.park, title: d.notYet?.park || "" } : null;
  const found = k === "incident" ? null : btns.find(b => !b.style || b.style === "go") || park;
  const primary = found && { ...found, label: OLD_LABEL[found.label] || found.label };
  const release = { onClick: () => ctx.send("release"), disabled: !available(k, d).includes("release"), title: "Only while the belts are tensioned" };
  const [label, tint] = stateLabel(k, d);
  return <div class={`legacy${narrow ? " narrow" : ""}`}>
    {/* The legacy UI is for anyone finding the switch hard. It won't get new features, so it says so, and the new UI
        is one click away */}
    <div class="cl-legacybar" role="note">You are in the legacy UI. It won't get new features.
      <button type="button" onClick={onNewUI}>Switch to the new UI</button></div>
    <Nav k={k} d={d} openSettings={() => { closeOverlay(); setTab("settings"); }} openMenu={() => ctx.openSettings()} settingsOpen={tab === "settings"}
      closeSettings={() => { closeOverlay(); setTab("maslow"); }} />
    <div class="cl-main">
      <div class="cl-display">
        {tab === "settings" ? <SettingsTab k={k} ctx={ctx} /> : <>
        <div class="cl-canvas"><Frame k={k} d={d} view={view} showWorkspace={false} tolerance={ctx.tolerance} sledMass={ctx.sledMass} /></div>
        <div class="cl-views">{[["frame", "Frame"], ["toolpath", "Toolpath"]].map(([id, l]) =>
          <button type="button" aria-pressed={view === id} onClick={() => setView(id)}>{l}</button>)}</div></>}
        {overlay && <div class="cl-host">{overlay}</div>}
        {setupOpen && <SetupPopup k={k} ctx={ctx} close={() => setSetupOpen(false)} />}
        {k === "incident" && <AlertPopup d={d} b={q.buttons[0]} />}
      </div>
      <div class="cl-controls">
        <section class="cl-section">
          <div class="cl-state" style={`background:${tint}`} title={prose(s.msg, ctx.units)}>State: {label}</div>
          <div class="cl-grid4">
            <button type="button" class="cl-tile cream cl-setup" onClick={() => setSetupOpen(true)}>
              <svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><circle cx="12" cy="12" r="3.5" fill="none" stroke="currentColor" stroke-width="2" />
                <path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1" stroke="currentColor" stroke-width="2" /></svg>Setup</button>
            <button type="button" class="cl-tile green2" title="Work area, park position, scale and thickness" onClick={() => ctx.openSettings("frame")}>Options</button>
            {primary ? <Tile b={primary} class={`green cl-action${primary.temp ? " temp" : ""}`} /> : <span class="cl-tile cl-empty" aria-hidden="true" />}
            <Tile b={release} class="orange">Release Tension</Tile>
          </div>
          <Belts d={d} stale={s.stale} />
        </section>
        <section class="cl-section"><Jog k={k} d={d} send={ctx.send} u={ctx.units} setUnits={ctx.setUnits} /></section>
        <section class="cl-section cl-job">
          <Files k={k} ctx={ctx} />
          <Playback k={k} ctx={ctx} q={q} main={main} />
          <Bounds k={k} ctx={ctx} main={main} />
        </section>
        <section class="cl-section cl-serial">
          <LogView d={d} />{ctx.commandBox && <CommandBox d={d} send={ctx.send} />}
          <div class="cl-serialfoot"><span>Serial messages</span><SaveLogBtn d={d} /></div>
        </section>
      </div>
    </div>
  </div>;
}
