import { useState } from "preact/hooks";
import { jobFit } from "./fit.js";
import { SEAT_TO_WORK, helperZ } from "./screens.js";

// What each screen asks for in the top bar, and what its buttons do. The rule from REWRITE.md: whatever the
// top bar asks for can be completed in the top bar (small inputs here, bigger tasks in a pop-up opened from here).
//
// ctx: { d: device state, a: answers, setA(patch), send(action, payload), mark(step) }
// Returns { text, buttons: [{ label, onClick, style }], input }

// The stick-out must leave the tip above the work surface (under SEAT_TO_WORK), or the helper would set Z below it
function StickoutInput({ onSave, value }) {
  const [v, setV] = useState(value != null ? String(value) : "");
  const n = parseFloat(v), ok = n > 0 && n < SEAT_TO_WORK;
  return <span class="binput">
    <label class="field"><span>Stick-out</span><input value={v} onInput={e => setV(e.currentTarget.value)} aria-label="Stick-out in mm" style="width:70px" /> mm</label>
    <button class="btn" type="button" disabled={!ok} title={ok || !v ? "" : `From 0 to ${SEAT_TO_WORK} mm`} onClick={() => ok && onSave(n)}>Set Z</button>
  </span>;
}

function FilePicker({ files, onPick, onUpload }) {
  const [v, setV] = useState(files[0]?.name || "");
  return <span class="binput">
    <select class="fsel" value={v} onChange={e => setV(e.currentTarget.value)} aria-label="Job file">
      {files.map(f => <option value={f.name}>{f.name}</option>)}
    </select>
    <button class="btn" type="button" onClick={() => v && onPick(v)}>Use this file</button>
    <button class="btn ghost" type="button" onClick={onUpload}>Upload file</button>
  </span>;
}

// What this page's last action is doing (d.op, from the real machine; the simulator has none). While it starts,
// its buttons are disabled so it can't be sent twice; Stop stays live. If it failed, the reason leads the text.
export function ask(k, ctx) {
  const q = askFor(k, ctx);
  const op = ctx.d.op;
  if (!op) return q;
  if (op.phase === "starting") {
    return { ...q, buttons: q.buttons.map(b => b.style === "stopping" ? b : { ...b, disabled: true, label: b.label }),
      text: `Starting… ${q.text}` };
  }
  if (op.phase === "failed") return { ...q, alert: true, text: `${op.detail}. ${q.text}` };
  return q;
}

// The line a stopped job continues from: the stopped line to start, changeable (as the current UI's ▲▼ box)
function LineInput({ value, onChange }) {
  return <span class="binput"><label class="field"><span>Line</span>
    <input type="text" inputmode="numeric" value={value} onInput={e => onChange(e.currentTarget.value)} aria-label="Line to continue from" style="width:90px" /></label></span>;
}

// The chosen job against the work area and the frame (fit.js), once the file has been read
export function fitFor(d, a) {
  const g = d.gcode?.status === "ready" && d.gcode.file === a.file ? d.gcode : null;
  return g ? jobFit(g.extent, d.wco, d.settings, d.frame) : null;
}

function askFor(k, ctx) {
  const { d, a, setA, send, mark } = ctx;
  const B = (label, onClick, style = "") => ({ label, onClick, style });
  const stop = (label = "Stop") => B(label, () => send("stop"), "stopping");
  // Back to the previous step this page asked about: it forgets that answer, so the step can be checked or redone.
  // Steps the machine reports (belts, anchors) can't be undone by an answer: redo them with their own action.
  // It shows as a small arrow at the left of the bar (BackArrow), not with the other buttons.
  const back = (patch) => ({ ...B("Back to the previous step", () => setA(patch), "ghost"), back: true });
  switch (k) {
    case "wifi": return { text: "Choose a WiFi network for the machine to join, or keep using its hotspot. If you always use the hotspot, Hotspot only stops this screen from coming back.",
      buttons: [B("Join network", () => ctx.openSettings?.("network")), B("Use the hotspot only", () => send("saveNetwork", { mode: "AP" }), "ghost")] };
    case "recover": return { text: `${d.beltsKnown} of 4 belt lengths are still known. Apply tension holds those and pulls the others tight to recover them. Check every belt is attached first.`,
      buttons: [B("Apply tension", () => { mark(2); send("tension"); }), B("Retract belts", () => { mark(0); send("retract"); }, "ghost")] };
    case "unknown": return { text: "Clear the area around the sled so the belts can wind in freely.", buttons: [B("Retract belts", () => { mark(0); send("retract"); })] };
    case "retracting": return { text: "Winding the belts in. Each one stops when it meets resistance. Stopping loses the belt lengths.", buttons: [stop("Stop retracting")] };
    case "retracted": return { text: "Check each arm by eye: are all four belts fully wound in?",
      buttons: [B("Yes, all retracted", () => setA({ retractChecked: true })), B("No, retract again", () => send("retract"), "ghost")] };
    case "toextend": return { text: "Belts retracted and checked. Press Extend belts, then pull each one out toward its anchor.",
      buttons: [B("Extend belts", () => { mark(1); send("extend"); }), B("Retract again", () => { setA({ retractChecked: false }); send("retract"); }, "ghost"), back({ retractChecked: false })] };
    case "extending": return { text: "Pull each belt out and attach it to its anchor. Press Done when all four are attached.",
      buttons: [B("Done extending", () => send("doneExtending")), stop("Stop extending")] };
    case "extstopped": return { text: "Extending stopped. Extend again, or retract from where the belts are.",
      buttons: [B("Extend again", () => send("extend")), B("Retract belts", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    case "extended": return { text: "Belts attached to the anchors. Apply tension next.", buttons: [B("Apply tension", () => { mark(2); send("tension"); })] };
    case "restored": return { text: "Belt lengths restored from the encoders. A reboot always releases tension, so apply it again.",
      buttons: [B("Apply tension", () => { mark(2); send("tension"); })] };
    case "tensioning": return { text: "Taking up the slack, then checking the position against the anchors.", buttons: [stop()] };
    case "notattached": return { text: "A belt wound in more than 300 mm. Check every belt is hooked to its anchor, then carry on.",
      buttons: [B("Continue applying tension", () => send("tension")), B("Retract belts", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    case "tensionfail": return { text: "The belt lengths don't match the saved anchors. Find anchors measures them again; the belts stay attached.",
      buttons: [B("Find anchors", () => { mark(3); send("findAnchors"); }), B("Apply tension again", () => send("tension"), "ghost")] };
    case "anchors": return { text: "Measuring the frame to find each anchor point. Keep clear of the sled and belts.", buttons: [stop("Stop finding anchors")] };
    case "noanchors": return { text: "Check each belt is hooked to its anchor, then try again.",
      buttons: [B("Find anchors again", () => send("findAnchors")), B("Retract belts", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    // The machine reports no tool state (TS): a 4.1, or older firmware on a Mini. The tool check can only be skipped
    case "tcheck": return d.toolSteps === false
      ? { text: "This machine doesn't report whether a bit is loaded (a Maslow 4.1, or older firmware on a Mini). Skip the tool check, and set Z the way you do today.",
          buttons: [{ ...B("Skip the tool check", () => { mark(4); setA({ zDone: true }); }),
            temp: "only for machines that don't report the tool state" }, stop()] }
      : { text: "Checking for a tool. The spindle rises through the light beam.", buttons: [stop()] };
    // A bit is loaded: Set Z where the bit is now (jogged down to the work), or let the helper work it out from the
    // stick-out, or take the bit out. The step is done once the report's work offset (WCO) shows the new Z.
    case "setz": return { text: "A bit is loaded. Jog it down to the work surface and press Set Z, or use the Set Z helper.",
      buttons: [B("Set Z", () => { setA({ zTarget: d.pos.z }); send("setZ"); }),
        B("Set Z helper", () => setA({ helper: true, zTarget: null }), "ghost"),
        B("Remove tool", () => { setA({ zTarget: null }); send("removeTool"); }, "ghost")] };
    case "zhelper": return { text: `Measure how far the bit sticks out of the spindle socket, from the socket face to the tip. The bit is lowered to the work and Z is set there (assuming the seated socket face is ${SEAT_TO_WORK} mm above the work).`,
      buttons: [back({ helper: false, zTarget: null })],
      input: <StickoutInput value={a.stickout} onSave={n => { const z = helperZ(n); setA({ stickout: n, zTarget: z }); send("lowerToWork", { z }); }} /> };
    case "toolfreeing": return { text: "Raising the spindle so the bit can come out. Take it out once the spindle stops.", buttons: [stop()] };
    case "toolload": return { text: "No bit loaded. Put a bit in and push it up into the light beam: the device pulls it in and seats it. Or continue with no tool.",
      buttons: [B("Continue with no tool", () => { mark(4); setA({ zDone: true }); }, "ghost")] };
    case "toolseating": return { text: "Bit detected. Let go once the spindle starts pulling it in; it stops when the bit is seated.", buttons: [stop()] };
    // No stock thickness here: calibration doesn't use it yet, so it lives in Settings for anyone who wants to set it
    case "secure": return { text: "Is your work piece set up and secure?",
      buttons: [B("Release tension", () => { setA({ securing: true }); send("release"); }, "ghost"),
        B("It's secure", () => { mark(6); setA({ secured: true }); }), back({ zDone: false, helper: false, zTarget: null })] };
    case "releasing": return { text: "Belts are slack. Secure the work piece, then apply tension again.", buttons: [B("Apply tension", () => send("tension"))] };
    case "choosefile": return { text: "Choose a job file, or upload one.",
      input: <FilePicker files={d.files} onPick={f => { mark(7); setA({ file: f }); }}
        onUpload={() => Promise.resolve(send("uploadFile")).then(f => { if (f) { mark(7); setA({ file: f }); } })} />,
      buttons: [back({ secured: false })] };
    case "error": return { text: d.error?.reason || "", buttons: [B("Clear error ($X)", () => send("clearError"))] };
    case "recovery": return { text: "Z lost its position (the spindle board needs homing). Re-home Z, then the tool check runs again.",
      buttons: [B("Re-home Z", () => { setA({ zDone: false, helper: false, zTarget: null }); send("rehomeZ"); })] };
    case "incident": {
      const i = d.incident;
      return { text: `${i.title}. ${i.detail} ${i.next}`,
        // After a spindle fault the tool check runs again, so the page forgets the tool and Z answers
        buttons: i.clearWithStop ? [stop("Stop to clear")] : [B(i.ack || "Got it", () => {
          if (i.kind === "spindleFault") setA({ faultSeen: true, zDone: false, helper: false, zTarget: null });
          send("ackIncident");
        })] };
    }
    case "othertab": return { text: "Only one tab per browser can be connected.", buttons: [B("Use this tab", () => send("reconnect"))] };
    case "toomany": return { text: "Close tabs or devices you aren't using.", buttons: [B("Reconnect", () => send("reconnect"))] };
    case "lost": return { text: "Reconnecting automatically. If it doesn't come back, reboot the device or turn it off and on.", buttons: [B("Reboot device", () => send("reboot"), "ghost")] };
    // Ready to cut says when the job doesn't fit: outside the work area is a warning, past the frame blocks Run
    case "ready": {
      const fit = fitFor(d, a);
      // Back to setup: the last setup step (the job file) asks again, and its Back goes further
      const toSetup = { ...back({ file: null }), label: "Back to setup" };
      return fit && (fit.level === "outside" || fit.level === "pastFrame") ? { text: fit.text, alert: true, buttons: [toSetup] } : { text: "", buttons: [toSetup] };
    }
    // Continue from the stopped line, or one the user types; Discard forgets the stopped job ($SD/Discard)
    case "stopped": {
      const stoppedLine = d.lastJob?.line ?? 0;
      return { text: "", input: <LineInput value={a.contLine ?? String(stoppedLine)} onChange={v => setA({ contLine: v })} />,
        buttons: [B("Discard", () => { setA({ stoppedSeen: true, contLine: null }); send("discardStopped"); }, "ghost")] };
    }
    default: return { text: "", buttons: [] };
  }
}

// The main actions outside setup: Run and its relatives, always next to Stop
export function mainActions(k, ctx) {
  const { d, a, setA, send } = ctx;
  const B = (label, onClick, style = "") => ({ label, onClick, style });
  // Trace boundary moves around the chosen file's extent (the real machine reads the file for it)
  // Past the frame, Trace boundary and Run would drive the sled where it can't go
  const pastFrame = fitFor(d, a)?.level === "pastFrame";
  const PAST = "The job goes past the frame, where the machine can't reach.";
  const trace = d.notYet?.trace
    ? { ...B("Trace boundary", () => {}, "ghost"), disabled: true, title: d.notYet.trace }
    : pastFrame ? { ...B("Trace boundary", () => {}, "ghost"), disabled: true, title: PAST }
    : B("Trace boundary", () => send("trace", { file: a.file }), "ghost");
  switch (k) {
    case "ready": {
      const run = B(`Run ${a.file}`, () => send("run", { file: a.file }), "go");
      return [trace, pastFrame ? { ...run, disabled: true, title: PAST } : run];
    }
    case "stopped": return [{ ...B("Run from the start", () => { setA({ stoppedSeen: true }); send("run", { file: a.file }); }, "ghost"),
      ...(pastFrame ? { disabled: true, title: PAST } : {}) },
      (() => {
        const n = parseInt(a.contLine ?? d.lastJob?.line ?? 0, 10);
        const ok = Number.isInteger(n) && n >= 1;
        return { ...B(`Continue from line ${ok ? n.toLocaleString() : "…"}`, () => { setA({ stoppedSeen: true, contLine: null }); send("continueJob", { line: n }); }, "go"),
          disabled: !ok, title: ok ? "" : "Type a line number from 1" };
      })()];
    default: return [];
  }
}
