import { useEffect, useState } from "preact/hooks";

// The tester stands in for the machine here. Each question is something the real firmware will report once the
// adapter replaces the simulator. It's docked, not modal, so Stop and the rest of the screen stay usable.
// It folds away while there's nothing to answer, and opens again for each new question.
export function SimPanel({ d, device }) {
  const p = d.pending;
  const [manual, setManual] = useState(null);
  useEffect(() => setManual(null), [p]);
  const open = manual ?? !!p;
  const setOpen = setManual;
  return <aside class={`simpanel${open ? "" : " closed"}${p ? " asking" : ""}`} aria-label="Simulated device">
    <button type="button" class="simhead" onClick={() => setOpen(!open)} aria-expanded={open}>
      <span>Simulated device</span>{p && !open && <em>needs an answer</em>}<span aria-hidden="true">{open ? "▾" : "▴"}</span>
    </button>
    {open && <div class="simbody">
      {p ? <>
        <b>{p.title}</b>
        <p>{p.detail}</p>
        <div class="simopts">{p.options.map((o, i) =>
          <button type="button" class={`btn ${o.tone === "bad" ? "stopping" : ""}`} onClick={() => device.answer(i)}>{o.label}</button>)}</div>
      </> : <p class="note">Nothing to answer. The device is idle or waiting for you.</p>}
      <details class="simevents"><summary>Cause an event</summary>
        <div class="simopts">
          {d.connected
            ? <button type="button" class="btn ghost" onClick={() => device.event("disconnect")}>Lose the connection</button>
            : <button type="button" class="btn ghost" onClick={() => device.event("reconnect")}>Reconnect</button>}
          <button type="button" class="btn ghost" onClick={() => device.event("emergencyStop")} disabled={!d.connected}>Press the emergency button</button>
          <button type="button" class="btn ghost" onClick={() => device.event("boardReset")} disabled={!d.connected}>Board resets</button>
        </div>
      </details>
    </div>}
  </aside>;
}
