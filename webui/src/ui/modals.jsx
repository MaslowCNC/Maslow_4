import { useState } from "preact/hooks";
import { SCREENS } from "../flow/screens.js";
import { ModePill, Conn, StepArt } from "./parts.jsx";

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
  ["retract", "Retract belts", "Winds all four belts in until they meet resistance. The belt lengths reset to zero."],
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
    case 7: return ["release", "toolCheck", "findAnchors", "retract", "reboot"];
  }
  return ["reboot"];
}
const NEED = { retract: "", extend: "Only once the belts are retracted.", tension: "Only once the belts are extended or tension was released.",
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

const SECTIONS = [["state", "State"], ["device", "Device"], ["network", "Network"], ["workarea", "Work area"], ["tool", "Tool and Z"], ["display", "Display"], ["advanced", "Advanced"]];

export function Settings({ k, ctx, onClose, openWorkArea, theme, setTheme, compact, stop }) {
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
  else if (sec === "workarea") body = <><h4>Work area</h4>
    <Row label="Size"><span class="mono">{d.workArea.w} × {d.workArea.h} mm</span></Row>
    <Row label="Spoil board thickness" hint="Raises the sled, which changes the belt angles."><span class="mono">{d.workArea.spoil} mm</span></Row>
    <Row label="Stock thickness" hint="Asked at Secure the work piece for each job."><span class="mono">{d.stock} mm</span></Row>
    <Row label="Anchors"><span>{d.anchorsSaved ? "Saved" : "Cleared by the new work area"}</span></Row>
    <Row label=""><button class="btn ghost" type="button" disabled={busy} onClick={() => { onClose(); openWorkArea(); }}>Change work area</button></Row></>;
  else if (sec === "tool") body = <><h4>Tool and Z</h4>
    <Row label="Last stick-out" hint="Set when you measure a tool."><span class="mono">{d.zFromStickout ? `${d.zFromStickout} mm` : "–"}</span></Row>
    <Row label="Z after Stop" hint="How far Z lifts above work Z when you press Stop."><span class="mono">+2 mm</span><span class="note">fixed in firmware</span></Row></>;
  else if (sec === "display") body = <><h4>Display</h4>
    <Row label="Panels"><select value={theme} onChange={e => setTheme(e.currentTarget.value)}><option value="dark">Dark</option><option value="light">Light</option></select></Row></>;
  else body = <><h4>Advanced</h4><p class="note">The raw FluidNC and Maslow settings. Not wired to the simulator.</p>
    <Row label="Belt retraction limit" hint="Apply tension stops if a belt winds in more than this."><span class="mono">300 mm</span></Row></>;
  return <Modal title="Settings" small="maslow" onClose={onClose} nav={nav} compact={compact} stop={stop} ctx={ctx}>{body}</Modal>;
}

export function WorkArea({ ctx, onClose, onSave, compact, stop }) {
  const { d } = ctx;
  const [w, setW] = useState(String(d.workArea.w)), [h, setH] = useState(String(d.workArea.h)), [sp, setSp] = useState(String(d.workArea.spoil));
  const save = () => { const v = [w, h, sp].map(parseFloat); if (v[0] > 0 && v[1] > 0 && v[2] >= 0) onSave({ w: v[0], h: v[1], spoil: v[2] }); };
  return <Modal title="Work area" single compact={compact} stop={stop} ctx={ctx} onClose={onClose}
    foot={<><button class="btn ghost" type="button" onClick={onClose}>Cancel</button><button class="btn" type="button" onClick={save}>Save work area</button></>}>
    <p class="note">Current work area: {d.workArea.w} × {d.workArea.h} mm</p>
    <StepArt step={0} d={d} />
    <Row label="Width"><input type="text" class="mono" value={w} onInput={e => setW(e.currentTarget.value)} style="width:90px" /> mm</Row>
    <Row label="Height"><input type="text" class="mono" value={h} onInput={e => setH(e.currentTarget.value)} style="width:90px" /> mm</Row>
    <Row label="Spoil board thickness" hint="Raises the sled, which changes the belt angles. 0 if there isn't one."><input type="text" class="mono" value={sp} onInput={e => setSp(e.currentTarget.value)} style="width:90px" /> mm</Row>
    <div class="callout" style="--tone:var(--warn)"><b>Saving clears the anchors</b><span>Setup then runs every step. Clearing them needs a firmware change.</span></div>
  </Modal>;
}
