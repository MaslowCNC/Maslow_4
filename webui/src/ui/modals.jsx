import { useEffect, useState } from "preact/hooks";
import { SCREENS } from "../flow/screens.js";
import { ModePill, Conn, SpindleBtn, spindleLock } from "./parts.jsx";
import { MACHINE_SETTINGS, settingError, effectiveWorkArea } from "../device/settings.js";
import { WIFI_MODES, OTHER_WIFI_MODES, NET_SETTINGS, netError, netChanges, netWarnings } from "../device/network.js";

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

export function Settings({ k, ctx, onClose, theme, setTheme, compact, stop, section = "state" }) {
  const [sec, setSec] = useState(section);
  const { d } = ctx, s = SCREENS[k];
  const busy = s.moving || s.mode === "Job" || !d.connected;
  const nav = <nav class="mnav">{SECTIONS.map(([id, n]) => <button type="button" aria-current={id === sec} onClick={() => setSec(id)}>{n}</button>)}</nav>;
  let body;
  if (sec === "state") body = <StateSection k={k} ctx={ctx} close={onClose} />;
  else if (sec === "device") body = <><h4>Device</h4>
    <NameRow ctx={ctx} busy={busy} />
    <Row label="Firmware"><span class="mono">Simulated device</span><button class="btn ghost" type="button" disabled={busy}>Update firmware</button></Row>
    <Row label="Restart" hint="Reconnects on its own when it's back."><button class="btn ghost" type="button" disabled={busy} onClick={() => { onClose(); ctx.send("reboot"); }}>Reboot device</button></Row></>;
  // Not the socket: the network settings go over HTTP, and they're how a board whose socket won't connect is fixed.
  // The section stays off until [ESP400] answers, so a board that can't be reached at all still can't be edited.
  else if (sec === "network") body = <NetworkSection ctx={ctx} busy={s.moving || s.mode === "Job"} close={onClose} />;
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

// Network: the WiFi the board joins, what it does when it can't, and its hotspot. These are the board's own flash
// settings, read and saved over HTTP (device/network.js); the board uses them after a restart. A password reads
// back hidden, so its box starts empty and is only sent when typed.
function useNetwork(ctx) {
  const { d, send } = ctx;
  // Read each time the section opens: another connection, or the board itself, may have changed them
  useEffect(() => { send("readNetwork"); }, []);
  return d.net || {};
}

function NetStatus({ net }) {
  if (net.read === "reading" && !net.status) return <span class="note">Reading…</span>;
  if (net.read === "failed") return <span class="note" style="color:var(--danger)">Couldn't read: {net.detail}</span>;
  const st = net.status;
  if (!st) return <span class="note">–</span>;
  return <span>
    {st.sta && (st.sta.connected ? <>On <b>{st.sta.ssid}</b> <span class="mono note">{st.sta.ip}</span></> : <>Not connected to {st.sta.ssid || "a network"}</>)}
    {st.sta && st.ap && <br />}
    {st.ap && <>Hotspot <b>{st.ap.ssid}</b> <span class="mono note">{st.ap.ip}</span></>}
    {!st.sta && !st.ap && "WiFi is off"}
  </span>;
}

// After a save: the board keeps using the old settings until it restarts
function RestartNote({ ctx, close, net, busy }) {
  if (!net.restartNeeded) return null;
  return <div class="netnote"><p class="note" style="margin:0">Saved. The board uses the new settings after it restarts.</p>
    <button class="btn" type="button" disabled={busy} onClick={() => { close(); ctx.send("reboot"); }}>Restart now</button></div>;
}

// Each setting saves on its own, so changing one can't send another by accident. The WiFi network and its password
// are one group: a new network needs its password.
const NET_GROUPS = { wifi: ["ssid", "password"], mode: ["mode"], apSsid: ["apSsid"], apPassword: ["apPassword"] };

function NetworkSection({ ctx, busy, close }) {
  const { send } = ctx;
  const net = useNetwork(ctx);
  const cur = net.settings;
  const [form, setForm] = useState({});      // what the user changed; the rest shows the board's value
  const [savingGroup, setSavingGroup] = useState(null);
  const [showScan, setShowScan] = useState(false);
  // A save clears only its own group's edits, not ones still waiting in another row
  useEffect(() => {
    if (net.save?.state !== "saved" || !savingGroup) return;
    setForm((f) => Object.fromEntries(Object.entries(f).filter(([k]) => !NET_GROUPS[savingGroup].includes(k))));
    setSavingGroup(null);
  }, [net.save]);
  const val = (k) => form[k] ?? (NET_SETTINGS[k].secret ? "" : cur?.[k] ?? "");
  const edit = (k) => (e) => setForm({ ...form, [k]: e.currentTarget.value });
  const scanList = net.scan?.list || [];
  const picked = scanList.find((ap) => ap.ssid === val("ssid"));
  const ssidChanged = !!cur && val("ssid") !== cur.ssid;
  const saving = net.save?.state === "saving";
  const off = busy || !cur || saving;
  const scan = () => { setShowScan(true); send("scanNetworks"); };
  const host = cur?.hostname || "maslow";
  const payloadFor = (g) => g === "wifi" ? { ssid: val("ssid"), password: val("password"), open: !!picked && !picked.locked }
    : { [g]: val(g) };
  // One group's Save: the button, then why it can't save, what went wrong, and what to know first
  const save = (g) => {
    if (!cur) return null;
    const payload = payloadFor(g);
    const { changes, error } = netChanges(payload, cur);
    const touched = NET_GROUPS[g].some((k) => k in form);
    const warnings = !error ? netWarnings({ form: payload, current: cur, scan: net.scan, changes }) : [];
    if (changes.length && g === "wifi" && cur.mode !== "AP") {
      warnings.push(`After the restart this page loses the board until this computer is on ${val("ssid")}: then open http://${host}.local.`);
    }
    if (changes.length && g === "mode" && val("mode") === "AP") {
      warnings.push(`After the restart, join the hotspot ${val("apSsid")}, then open http://${host}.local.`);
    }
    return <>
      <button class="btn" type="button" disabled={off || !!error || !changes.length}
        onClick={() => { setSavingGroup(g); send("saveNetwork", payload); }}>
        {saving && savingGroup === g ? "Saving…" : "Save"}</button>
      {error && touched && <span class="note netmsg" style="color:var(--danger)">{error}</span>}
      {net.save?.state === "failed" && savingGroup === g && <span class="note netmsg" style="color:var(--danger)">{net.save.detail}</span>}
      {warnings.length > 0 && <ul class="netwarn">{warnings.map((w) => <li>{w}</li>)}</ul>}
    </>;
  };
  const input = (k) => <input type="text" class="mono" value={val(k)} onInput={edit(k)} aria-label={NET_SETTINGS[k].label}
    autocomplete="off" spellcheck={false} disabled={off} style="width:220px" />;
  const secret = (k, saved) => <SecretInput value={val(k)} onInput={edit(k)} label={NET_SETTINGS[k].label} disabled={off} saved={saved} />;
  const openPicked = picked && !picked.locked;
  return <><h4>Network</h4>
    <p class="note" style="margin:0">Each setting saves on its own. The board uses it after a restart.</p>
    <Row label="Now"><NetStatus net={net} /></Row>
    <Row label="WiFi network" hint="The network the board joins.">
      {input("ssid")}
      <button class="btn ghost" type="button" disabled={off || net.scan?.state === "scanning"} onClick={scan}>
        {net.scan?.state === "scanning" ? "Scanning…" : "Scan"}</button>
      {showScan && net.scan?.state === "failed" && <span class="note" style="flex-basis:100%;color:var(--danger)">Scan failed: {net.scan.detail}</span>}
      {showScan && scanList.length > 0 && <ul class="aplist">{scanList.map((ap) =>
        <li><button type="button" aria-pressed={ap.ssid === val("ssid")} disabled={off}
          onClick={() => setForm({ ...form, ssid: ap.ssid, password: "" })}>
          <span class="apname">{ap.ssid}</span><span class="note">{ap.locked ? "🔒 " : ""}{ap.signal} %</span></button></li>)}</ul>}
      {openPicked && save("wifi")}
    </Row>
    {!openPicked && <Row label="WiFi password" hint={ssidChanged ? `For ${val("ssid") || "the new network"}. Saved with the network.` : "Saved. Type a new one to change it."}>
      {secret("password", !ssidChanged)}
      {save("wifi")}</Row>}
    <Row label="Connects through" hint="If it can't join the WiFi, it starts its hotspot so you can still reach it. Hotspot only skips the WiFi setup screen.">
      <select value={val("mode")} disabled={off} onChange={edit("mode")} aria-label={NET_SETTINGS.mode.label}>
        {!WIFI_MODES.some(([m]) => m === val("mode")) && <option value={val("mode")}>{OTHER_WIFI_MODES[val("mode")] || val("mode") || "–"}</option>}
        {WIFI_MODES.map(([m, label]) => <option value={m}>{label}</option>)}</select>
      {save("mode")}</Row>
    <Row label="Hotspot name" hint="The network the board makes itself.">{input("apSsid")}
      {save("apSsid")}</Row>
    <Row label="Hotspot password" hint="Saved. Type a new one (8 characters or more) to change it.">
      {secret("apPassword", true)}
      {save("apPassword")}</Row>
    <RestartNote ctx={ctx} close={close} net={net} busy={busy} />
  </>;
}

// A password box: dots, with a button to show what's typed. A saved password can't be shown (the board only says
// one is saved), so its box starts empty with dots for a placeholder. It's a text box drawn as dots, not
// type=password: browsers and password managers fill password boxes by themselves, and a filled box here is saved
// to the board. That once changed a hotspot password nobody typed and locked a machine out.
// The eye only appears once something is typed: there's nothing else to show.
function SecretInput({ value, onInput, label, disabled, saved }) {
  const [reveal, setShown] = useState(false);
  const shown = reveal && !!value;
  return <span class="secretbox">
    <input type="text" class={shown ? "mono" : "mono secret"} value={value} onInput={onInput} aria-label={label}
      placeholder={saved ? "••••••••" : ""} autocomplete="off" spellcheck={false} autocapitalize="off" autocorrect="off"
      data-1p-ignore="" data-lpignore="true" data-bwignore="" disabled={disabled} />
    {value && <button type="button" class="iconbtn sm" aria-label={shown ? `Hide ${label.toLowerCase()}` : `Show ${label.toLowerCase()}`}
      aria-pressed={shown} title={shown ? "Hide" : "Show"} disabled={disabled} onClick={() => setShown(!shown)}>
      {shown ? <EyeOff /> : <Eye />}</button>}
  </span>;
}
const Eye = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  <path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z" /><circle cx="12" cy="12" r="3" /></svg>;
const EyeOff = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  <path d="M10.6 5.1A10.6 10.6 0 0 1 12 5c6.5 0 10 7 10 7a17 17 0 0 1-2.6 3.4M6.6 6.6A17 17 0 0 0 2 12s3.5 7 10 7a10 10 0 0 0 5.4-1.6" />
  <path d="M9.9 9.9a3 3 0 0 0 4.2 4.2M3 3l18 18" /></svg>;

// The board's name: its .local address on the network. Saved with the network settings, used after a restart.
function NameRow({ ctx, busy }) {
  const net = useNetwork(ctx);
  const cur = net.settings?.hostname;
  const [name, setName] = useState(null);
  useEffect(() => { if (net.save?.state === "saved") setName(null); }, [net.save]);
  const v = name ?? cur ?? "";
  const err = name != null ? netError("hostname", v) : null;
  const off = busy || cur == null || net.save?.state === "saving";
  return <><Row label="Name" hint="Letters, numbers and dashes. The board's address on the network is this, then .local.">
    <input type="text" class="mono" value={v} disabled={off} spellcheck={false} aria-label="Name" onInput={e => setName(e.currentTarget.value)} style="width:160px" />
    <span class="note mono">{v || "?"}.local</span>
    <button class="btn ghost" type="button" disabled={off || !!err || name == null || v === cur} onClick={() => ctx.send("saveNetwork", { hostname: v })}>Save</button>
    {err && <span class="note" style="flex-basis:100%;color:var(--danger)">{err}</span>}</Row>
    {net.restartNeeded && <p class="note" style="margin:0">Saved. The board uses the new name after it restarts (Restart, below).</p>}</>;
}
