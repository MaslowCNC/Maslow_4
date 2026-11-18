import { useState } from "preact/hooks";
import { SCREENS } from "../flow/screens.js";
import { ModePill, Conn, SpindleBtn, spindleLock } from "./parts.jsx";
import { MACHINE_SETTINGS, settingError, effectiveWorkArea } from "../device/settings.js";

// Pop-ups cover the panels only, so the top bar and Stop stay visible and clickable
function Modal({ title, small, onClose, children, foot, nav, single, compact, stop, ctx }) {
  return <div class={`modal-bg${compact ? " compact" : ""}`} onClick={e => { if (e.target === e.currentTarget) onClose(); }}>
    <div class={`modal${single ? " single" : ""}${compact ? " compact" : ""}`} role="dialog" aria-modal="true" aria-label={title}>
      <div class="mhead"><h3>{title}{small && <small>{small}</small>}</h3>
        <span style="display:flex;gap:8px;align-items:center">
          {stop && <button class="btn danger" type="button" style="min-height:36px" onClick={() => ctx.send("stop")}>Stop</button>}
          <button class="gear" type="button" aria-label={`Close ${title}`} onClick={onClose}>✕</button></span></div>
      {nav}
      <div class="mbody">{children}</div>
      {foot && <div class="mfoot">{foot}</div>}
    </div>
  </div>;
}

const Row = ({ label, hint, children }) =>
  <div class="srow"><span>{label}{hint && <small>{hint}</small>}</span><div class="val">{children}</div></div>;

// Every device action, always in the same order. The ones the device can't do right now are greyed out,
// and the ? says what each does and why it might be off.
const ACT = [
  ["retract", "Retract belts", "Winds all four belts in until they meet resistance. Unhook the belts from the anchors first. The belt lengths reset to zero."],
  ["extend", "Extend belts", "Lets the belts pay out so you can pull them to the anchors and attach them."],
  ["tension", "Apply tension", "Takes up the slack and checks the position against the saved anchors."],
  ["release", "Release tension", "Slackens the belts a little so you can reach the work piece. The belt lengths are kept."],
  ["findAnchors", "Find anchors", "Measures the frame to find each anchor point. About 11 minutes."],
  ["toolCheck", "Check the tool", "Raises the spindle through the light beam to see if a bit is loaded."],
  ["clearError", "Clear error ($X)", "Clears the error so the device can move again."],
  ["reboot", "Reboot device", "Restarts the board. It reconnects on its own."],
  ["stop", "Stop", "Stops the belts at once, turns the spindle off and lifts Z. The job's progress is saved (needs firmware)."]
];
function available(k, d) {
  const s = SCREENS[k];
  if (!d.connected || d.hotspot) return k === "lost" ? ["reboot"] : [];
  if (d.fluid === "Alarm" || k === "recovery") return ["clearError", "reboot"];
  if (s.moving || k === "cutting") return ["stop"];
  switch (d.maslow) {
    case 0: return ["retract", "reboot"];
    case 2: return ["extend", "retract", "reboot"];
    case 4: return ["tension", "findAnchors", "retract", "reboot"];
    // No Retract while the belts are tight: retracting under tension gives a false zero (ISSUES-LOG). Release first.
    case 7: return ["release", "toolCheck", "findAnchors", "reboot"];
    case 10: return ["findAnchors", "tension", "release", "reboot"];
  }
  return ["reboot"];
}
const NEED = { retract: "Not while the belts are tight: release tension first.", extend: "Only once the belts are retracted.", tension: "Only once the belts are extended or tension was released.",
  release: "Only while the belts are tensioned.", findAnchors: "Only once the belts are extended or tensioned.", toolCheck: "Only once the position is confirmed.",
  clearError: "Only after an error.", reboot: "", stop: "Nothing is moving." };

function StateSection({ k, ctx, close }) {
  const { d } = ctx, s = SCREENS[k], on = available(k, d);
  const why = (id) => !d.connected ? "Waiting for the device to answer." : s.moving || k === "cutting" ? "Not while the device is moving. Press Stop first." : NEED[id] || "Not available in this state.";
  return <>
    <h4>State</h4>
    <div class="minis"><div class="mini"><span>Mode</span><b><ModePill s={s} small /> {s.sub}</b></div>
      <div class="mini"><span>Connection</span><b><Conn d={d} /></b></div>
      <div class="mini"><span>Firmware reports</span><b>Maslow {d.maslow} · {d.fluid}</b></div></div>
    <p class={s.alert ? "alert" : ""} style="margin:0">{s.msg}</p>
    <div class="actgrid">{ACT.map(([id, label, what]) => {
      const ok = on.includes(id), tip = what + (ok ? "" : " " + why(id));
      return <span class="actpair"><button class={`btn ${id === "stop" ? "danger" : "ghost"}`} type="button" disabled={!ok}
        onClick={() => { close(); ctx.send(id); }}>{label}</button><span class="qhelp" tabindex="0" data-tip={tip} aria-label={tip}>?</span></span>;
    })}</div>
  </>;
}

const SECTIONS = [["state", "State"], ["device", "Device"], ["network", "Network"], ["frame", "Frame and belts"], ["home", "Home"], ["tool", "Tool and Z"], ["display", "Display"], ["advanced", "Advanced"]];

export function Settings({ k, ctx, onClose, theme, setTheme, compact, stop }) {
  const [sec, setSec] = useState("state");
  const { d } = ctx, s = SCREENS[k];
  const busy = s.moving || s.mode === "Job" || !d.connected;
  const nav = <nav class="mnav">{SECTIONS.map(([id, n]) => <button type="button" aria-current={id === sec} onClick={() => setSec(id)}>{n}</button>)}</nav>;
  let body;
  if (sec === "state") body = <StateSection k={k} ctx={ctx} close={onClose} />;
  else if (sec === "device") body = <><h4>Device</h4>
    <Row label="Name" hint="Letters, numbers and dashes. Sets the .local address and the hotspot name."><input type="text" class="mono" value="maslow" /> <span class="note">maslow.local</span></Row>
    <Row label="Firmware"><span class="mono">Simulated device</span><button class="btn ghost" type="button" disabled={busy}>Update firmware</button></Row>
    <Row label="Restart" hint="Reconnects on its own when it's back."><button class="btn ghost" type="button" disabled={busy} onClick={() => { onClose(); ctx.send("reboot"); }}>Reboot device</button></Row></>;
  else if (sec === "network") body = <><h4>Network</h4>
    <Row label="WiFi network"><span>Workshop</span><button class="btn ghost" type="button">Change network</button></Row>
    <Row label="Hotspot"><select><option>Only when WiFi isn't connected</option><option>Always on</option><option>Off</option></select></Row>
    <p class="note">Placeholder: these depend on what the current WiFi dialog offers.</p></>;
  else if (sec === "frame") body = <FrameSection ctx={ctx} busy={busy} />;
  else if (sec === "home") body = <HomeSection ctx={ctx} busy={busy} />;
  else if (sec === "tool") body = <><h4>Tool and Z</h4>
    {/* The Maslow 4.1 has no spindle output, so this only shows on a Mini (or before the machine is known) */}
    {!(d.machine && !/^Maslow Mini/.test(d.machine)) && <>
      <Row label="Spindle" hint="Runs the spindle at 12,000 RPM outside a job: to check the bit runs true, or to cut by hand while you jog. Stop also turns it off.">
        <span class="mono">{d.spindle === "on" ? "Running" : "Off"}</span><SpindleBtn k={k} d={d} send={ctx.send} /></Row>
      {spindleLock(k, d) && <p class="note" style="margin:0">{spindleLock(k, d)}.</p>}</>}
    <Row label="Last stick-out" hint="Set when you measure a tool."><span class="mono">{d.zFromStickout ? `${d.zFromStickout} mm` : "–"}</span></Row>
    <Row label="Z after Stop" hint="Stop leaves Z where it is, so the bit may stay in the work. Continue lifts it to at least 2 mm above work Z 0 before moving."><span>Stays put</span><span class="note">set in firmware</span></Row></>;
  else if (sec === "display") body = <><h4>Display</h4>
    <Row label="Panels"><select value={theme} onChange={e => setTheme(e.currentTarget.value)}><option value="dark">Dark</option><option value="light">Light</option></select></Row></>;
  else body = <><h4>Advanced</h4><p class="note">The raw FluidNC and Maslow settings. Not designed yet.</p>
    <Row label="Command box" hint="A box under the log that sends a line to the machine as typed, for support. No checks: motion and calibration commands run too. Remembered in this browser.">
      <label class="check"><input type="checkbox" checked={ctx.commandBox} onChange={e => ctx.setCommandBox(e.currentTarget.checked)} /> Show</label></Row>
    <MotorTest k={k} ctx={ctx} /></>;
  return <Modal title="Settings" small="maslow" onClose={onClose} nav={nav} compact={compact} stop={stop} ctx={ctx}>{body}</Modal>;
}

// The belt motor test ($TEST), a diagnostic. Each arm's motor pays its belt out at full power until it draws current
// (at most 100 ms), then the encoder and magnet are checked. Only while idle with the belts slack: with the belts tight
// the pull would jerk the sled. A missing motor or encoder stops motion until the error is cleared.
const SLACK = [0, 2, 4];   // Maslow states UNKNOWN, RETRACTED, EXTENDEDOUT
function MotorTest({ k, ctx }) {
  const { d } = ctx, s = SCREENS[k];
  const why = !d.connected ? "Waiting for the device to answer."
    : d.fluid === "Alarm" ? "Clear the error first (Settings › State › Clear error)."
    : d.job || s.moving || d.fluid !== "Idle" ? "Not while the machine is busy."
    : !SLACK.includes(d.maslow) ? "Only while the belts are slack: retracted, extended, or before setup. Release tension first."
    : null;
  const running = d.op?.action === "motorTest" && d.op.phase !== "failed";
  const r = d.motorTest;
  return <>
    <Row label="Belt motor test" hint="Runs each belt motor outward for a moment (under 0.1 s) and checks its encoder and magnet. For finding a loose cable or a dead motor. The belts must be slack.">
      <button class="btn ghost" type="button" disabled={!!why || running} title={why || "Send $TEST"} onClick={() => ctx.send("motorTest")}>
        {running ? "Testing…" : "Test motors"}</button></Row>
    {why && <p class="note" style="margin:0">{why}</p>}
    {r && <ul class="mtest">{r.passed.map(a => <li class="ok">{a}: passed</li>)}
      {r.failed.map(f => <li class="bad">{f.arm}: {f.problem.toLowerCase()}</li>)}</ul>}
    {r?.failed.some(f => f.problem !== "Magnet not detected") &&
      <p class="note alert" style="margin:0">Motion is disabled until you fix the cause and clear the error ($X).</p>}
  </>;
}

// Frame and belts. There's no work area to confirm any more: the machine works it out from how far the belts
// extend. Extend distance 0 means "extend until I say the belts are attached" (Done extending); a set distance is
// for a machine standing vertically, where the belts can't be pulled out by hand.
function FrameSection({ ctx, busy }) {
  const { d, send } = ctx;
  const [ext, setExt] = useState(String(d.extendDist ?? 0));
  const [spoil, setSpoil] = useState(String(d.workArea.spoil));
  const [stock, setStock] = useState(String(d.stock ?? 0));
  const extN = parseFloat(ext), spoilN = parseFloat(spoil), stockN = parseFloat(stock);
  const extOk = extN >= 0 && !(extN === 0 && d.notYet?.extendUntilDone);
  return <><h4>Frame and belts</h4>
    <Row label="Extend distance" hint="0: the belts pay out until you press Done extending. Set a length if the machine stands vertically.">
      <input type="text" inputmode="decimal" class="mono" value={ext} onInput={e => setExt(e.currentTarget.value)} style="width:90px" /> mm
      <button class="btn ghost" type="button" disabled={busy || !extOk || extN === d.extendDist} onClick={() => send("setExtendDist", { mm: extN })}>Save</button></Row>
    {extN === 0 && d.notYet?.extendUntilDone &&
      <p class="note" style="color:var(--temp)"><b>Temporary:</b> {d.notYet.extendUntilDone}</p>}
    <Row label="Spoil board thickness" hint="Raises the sled, which changes the belt angles. 0 if there isn't one.">
      <input type="text" inputmode="decimal" class="mono" value={spoil} onInput={e => setSpoil(e.currentTarget.value)} style="width:90px" /> mm
      <button class="btn ghost" type="button" disabled={busy || !(spoilN >= 0) || spoilN === d.workArea.spoil} onClick={() => send("setSpoilboard", { mm: spoilN })}>Save</button></Row>
    <Row label="Stock thickness" hint="Optional. Calibration doesn't use it yet.">
      <input type="text" inputmode="decimal" class="mono" value={stock} onInput={e => setStock(e.currentTarget.value)} style="width:90px" /> mm
      <button class="btn ghost" type="button" disabled={busy || !(stockN >= 0) || stockN === d.stock} onClick={() => send("setStock", { stock: stockN })}>Save</button></Row>
    <WorkAreaRow ctx={ctx} busy={busy} />
    <SettingGroup ctx={ctx} busy={busy} label="Work area centre offset" name="Work area centre offset" keys={[["offX", "X"], ["offY", "Y"]]}
      hint="Moves the work area off the frame's centre. Usually 0." />
    <SettingGroup ctx={ctx} busy={busy} label="Park position" name="Park position" keys={[["parkX", "X"], ["parkY", "Y"], ["parkZ", "Z"]]}
      hint="Where Park moves the sled. Z first, to this height above work Z 0; then X and Y, as machine position (0, 0 is where Apply tension leaves the sled)." />
    <SettingGroup ctx={ctx} busy={busy} label="Belt retraction limit" name="Belt retraction limit" keys={[["retractLimit"]]}
      hint="Apply tension stops if a belt winds in more than this. Default 300 mm." />
    <SettingGroup ctx={ctx} busy={busy} label="Retraction force" name="Retraction force" keys={[["retractForce"]]}
      hint="How hard the motors pull before a belt counts as tight, when retracting and measuring. Default 1300." />
    <SettingGroup ctx={ctx} busy={busy} label="Scale" name="Scale" keys={[["scaleX", "X"], ["scaleY", "Y"]]}
      hint="Corrects cuts that come out a little too big or small. 1 is no change; 1.01 cuts 1 % larger. 0.8 to 1.2." />
    <Row label="Workspace tolerance" hint="The Workspace overlay shows where cuts should land within this, and whether the job stays inside. Remembered in this browser.">
      <select value={String(ctx.tolerance)} onChange={e => ctx.setTolerance(parseFloat(e.currentTarget.value))}>
        {[0.5, 1, 2, 3, 5].map(v => <option value={String(v)}>±{v} mm</option>)}</select></Row>
    {d.frame?.vertical && <Row label="Sled weight" hint="The sled with the router fitted. On a vertical frame its weight stretches the belts, so the Workspace overlay needs it. Remembered in this browser.">
      <select value={String(ctx.sledMass)} onChange={e => ctx.setSledMass(parseFloat(e.currentTarget.value))}>
        {[4, 6, 8, 10, 12, 15].map(v => <option value={String(v)}>{v} kg</option>)}</select></Row>}
    <Row label="Anchors"><span>{d.anchorsSaved ? "Saved" : "Not measured yet"}</span></Row></>;
}

// The work area: 0 on an axis means automatic, which the firmware works out from the anchors the belts measured
// (80 % of the frame's width, 60 % of its height). Shows what that comes to on this frame.
function WorkAreaRow({ ctx, busy }) {
  const { d } = ctx;
  const a = effectiveWorkArea({ workW: d.settings?.workW ?? 0, workH: d.settings?.workH ?? 0 }, d.frame);
  const mm = (v, auto) => (v == null ? "?" : `${Math.round(v)}${auto ? " (auto)" : ""}`);
  return <SettingGroup ctx={ctx} busy={busy} label="Work area" name="Work area" keys={[["workW", "X"], ["workH", "Y"]]}
    hint={<>0 = automatic: worked out from how far the belts extend to the anchors (80 % of the frame's width, 60 % of its height).
      {" "}Now {mm(a.w, a.auto.w)} × {mm(a.h, a.auto.h)} mm.</>} />;
}

// One or more machine settings (device/settings.js) with one Save. Edited fields are kept as drafts; the rest show
// the device's value, so a value read after the dialog opened still appears. Save sends only what changed.
function SettingGroup({ ctx, busy, label, name, hint, keys }) {
  const { d, send } = ctx;
  const [draft, setDraft] = useState({});
  const stored = (k) => d.settings?.[k];
  const text = (k) => draft[k] ?? (stored(k) == null ? "" : String(stored(k)));
  const values = Object.fromEntries(keys.map(([k]) => [k, text(k).trim() === "" ? NaN : Number(text(k))]));
  const errors = keys.map(([k, sub]) => [sub, settingError(k, values[k])]).filter(([, e]) => e);
  const changed = keys.some(([k]) => values[k] !== stored(k));
  const unit = MACHINE_SETTINGS[keys[0][0]].unit;
  const save = () => { send("setSettings", { values, label: name }); setDraft({}); };
  return <Row label={label} hint={hint}>
    {keys.map(([k, sub]) => <label class="setfield">{sub && <span>{sub}</span>}
      <input type="text" inputmode="decimal" class="mono" value={text(k)} aria-label={`${label}${sub ? ` ${sub}` : ""}`}
        placeholder={stored(k) == null ? "–" : ""} onInput={e => setDraft({ ...draft, [k]: e.currentTarget.value })} style="width:80px" /></label>)}
    {unit && <span>{unit}</span>}
    <button class="btn ghost" type="button" disabled={busy || errors.length > 0 || !changed} onClick={save}>Save</button>
    {changed && errors.length > 0 && <span class="note" style="flex-basis:100%;color:var(--danger)">
      {errors.map(([sub, e]) => (sub ? `${sub}: ${e}` : e)).join(" · ")}</span>}
  </Row>;
}

// Home: the job's 0, 0, 0 typed as machine coordinates (the jog pad's Set home XY / Set Z use where the machine is
// now), and Reset home, which puts it back on the machine's own 0, 0, 0 so work and machine positions match.
// On a 4.1 the Z home must be 0–72 mm above the z-stop; the Mini's spindle board has its own Z range.
const Z_HOME_MAX_MM = 72;
function HomeSection({ ctx, busy }) {
  const { d, send } = ctx;
  const f = v => (Math.abs(v) < 0.0005 ? 0 : v).toFixed(1);
  const [x, setX] = useState(f(d.wco.x)), [y, setY] = useState(f(d.wco.y)), [z, setZ] = useState(f(d.wco.z));
  const xN = parseFloat(x), yN = parseFloat(y), zN = parseFloat(z);
  const mini = /^Maslow Mini/.test(d.machine || "");
  const xyErr = !Number.isFinite(xN) || !Number.isFinite(yN) ? "Enter X and Y in mm" : "";
  const zErr = !Number.isFinite(zN) ? "Enter Z in mm"
    : !mini && (zN < 0 || zN > Z_HOME_MAX_MM) ? `Z home must be 0–${Z_HOME_MAX_MM} mm above the z-stop` : "";
  const off = busy || !!d.notYet?.setHomeXY;
  const field = (label, v, set) => <label class="field"><span>{label}</span>
    <input type="text" inputmode="decimal" class="mono" value={v} onInput={e => set(e.currentTarget.value)} aria-label={`Home ${label} in mm`} style="width:80px" /></label>;
  const isReset = d.wco.x === 0 && d.wco.y === 0 && d.wco.z === 0;
  return <><h4>Home</h4>
    <p class="note">The jog pad's Set home XY and Set Z put home where the machine is now. Here you can type it instead, in
      machine coordinates.</p>
    <Row label="Home X, Y" hint="Machine position of the job's 0, 0.">
      {field("X", x, setX)}{field("Y", y, setY)} mm
      <button class="btn ghost" type="button" disabled={off || !!xyErr || (xN === d.wco.x && yN === d.wco.y)} onClick={() => send("setHomeXY", { x: xN, y: yN })}>Set</button></Row>
    {xyErr && <p class="note" style="color:var(--danger)">{xyErr}</p>}
    <Row label="Home Z" hint={mini ? "Machine Z of the job's Z 0." : "Machine Z of the job's Z 0: how far above the z-stop."}>
      {field("Z", z, setZ)} mm
      <button class="btn ghost" type="button" disabled={off || !!zErr || zN === d.wco.z} onClick={() => send("setZ", { z: zN })}>Set</button></Row>
    {zErr && <p class="note" style="color:var(--danger)">{zErr}</p>}
    <Row label="Reset home" hint="Puts home back on the machine's own 0, 0, 0, so work and machine positions read the same.">
      <button class="btn ghost" type="button" disabled={off || isReset} onClick={() => { send("resetHome"); setX("0.0"); setY("0.0"); setZ("0.0"); }}>{isReset ? "Already reset" : "Reset home"}</button></Row></>;
}
