import { useState } from "preact/hooks";
import { SCREENS } from "../flow/screens.js";
import { ModePill, Conn } from "./parts.jsx";

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

const SECTIONS = [["state", "State"], ["device", "Device"], ["network", "Network"], ["frame", "Frame and belts"], ["tool", "Tool and Z"], ["display", "Display"], ["advanced", "Advanced"]];

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
  else if (sec === "tool") body = <><h4>Tool and Z</h4>
    <Row label="Last stick-out" hint="Set when you measure a tool."><span class="mono">{d.zFromStickout ? `${d.zFromStickout} mm` : "–"}</span></Row>
    <Row label="Z after Stop" hint="How far Z lifts above work Z when you press Stop."><span class="mono">+2 mm</span><span class="note">fixed in firmware</span></Row></>;
  else if (sec === "display") body = <><h4>Display</h4>
    <Row label="Panels"><select value={theme} onChange={e => setTheme(e.currentTarget.value)}><option value="dark">Dark</option><option value="light">Light</option></select></Row></>;
  else body = <><h4>Advanced</h4><p class="note">The raw FluidNC and Maslow settings. Not wired to the simulator.</p>
    <Row label="Belt retraction limit" hint="Apply tension stops if a belt winds in more than this."><span class="mono">300 mm</span></Row></>;
  return <Modal title="Settings" small="maslow" onClose={onClose} nav={nav} compact={compact} stop={stop} ctx={ctx}>{body}</Modal>;
}

// Frame and belts. There's no work area to confirm any more: the machine works it out from how far the belts
// extend. Extend distance 0 means "extend until I say the belts are attached" (Done extending); a set distance is
// for a machine standing vertically, where the belts can't be pulled out by hand.
function FrameSection({ ctx, busy }) {
  const { d, send } = ctx;
  const [ext, setExt] = useState(String(d.extendDist ?? 0));
  const [spoil, setSpoil] = useState(String(d.workArea.spoil));
  const extN = parseFloat(ext), spoilN = parseFloat(spoil);
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
    <Row label="Stock thickness" hint="Asked at Secure the work piece for each job."><span class="mono">{d.stock} mm</span></Row>
    <Row label="Work area" hint="Worked out from how far the belts extend."><span>Automatic</span></Row>
    <Row label="Anchors"><span>{d.anchorsSaved ? "Saved" : "Not measured yet"}</span></Row></>;
}
