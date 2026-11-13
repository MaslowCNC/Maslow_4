import { useState } from "preact/hooks";

// What each screen asks for in the top bar, and what its buttons do. The rule from REWRITE.md: whatever the
// top bar asks for can be completed in the top bar (small inputs here, bigger tasks in a pop-up opened from here).
//
// ctx: { d: device state, a: answers, setA(patch), send(action, payload), mark(step), openWorkArea() }
// Returns { text, buttons: [{ label, onClick, style }], input }

function StickoutInput({ onSave }) {
  const [v, setV] = useState("31.5");
  return <span class="binput">
    <label class="field"><span>Stick-out</span><input value={v} onInput={e => setV(e.currentTarget.value)} aria-label="Stick-out in mm" /> mm</label>
    <button class="btn" type="button" onClick={() => { const n = parseFloat(v); if (n > 0) onSave(n); }}>Save measurement</button>
  </span>;
}

function StockInput({ value, onSecure }) {
  const [v, setV] = useState(String(value));
  return <span class="binput">
    <label class="field"><span>Stock</span><input value={v} onInput={e => setV(e.currentTarget.value)} aria-label="Stock thickness in mm" /> mm</label>
    <button class="btn" type="button" onClick={() => { const n = parseFloat(v); if (n >= 0) onSecure(n); }}>It's secure</button>
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

function askFor(k, ctx) {
  const { d, a, setA, send, mark, openWorkArea } = ctx;
  const B = (label, onClick, style = "") => ({ label, onClick, style });
  const stop = (label = "Stop") => B(label, () => send("stop"), "stopping");
  switch (k) {
    case "wifi": return { text: "Choose a network for the device to join. (Placeholder until the current WiFi dialog is documented.)",
      buttons: [B("Join network", () => send("joinNetwork"))] };
    case "workarea": return {
      text: `Work area ${d.workArea.w} × ${d.workArea.h} mm, spoil board ${d.workArea.spoil} mm. Keep it to skip the steps the device already knows.`,
      buttons: [B("Keep this work area", () => setA({ workAreaConfirmed: true })), B("Set new work area", openWorkArea, "ghost")] };
    case "newarea": return { text: "Set the table's size and the spoil board thickness in the Work area pop-up.", buttons: [B("Open work area", openWorkArea)] };
    case "recover": return { text: `${d.beltsKnown} of 4 belt lengths are still known. Apply tension holds those and pulls the others tight to recover them. Check every belt is attached first.`,
      buttons: [B("Apply tension", () => { mark(3); send("tension"); }), B("Retract belts", () => { mark(1); send("retract"); }, "ghost")] };
    case "unknown": return { text: "Clear the area around the sled so the belts can wind in freely.", buttons: [B("Retract belts", () => { mark(1); send("retract"); })] };
    case "retracting": return { text: "Winding the belts in. Each one stops when it meets resistance. Stopping loses the belt lengths.", buttons: [stop("Stop retracting")] };
    case "retracted": return { text: "Check each arm by eye: are all four belts fully wound in?",
      buttons: [B("Yes, all retracted", () => setA({ retractChecked: true })), B("No, retract again", () => send("retract"), "ghost")] };
    case "toextend": return { text: "Belts retracted and checked. Press Extend belts, then pull each one out toward its anchor.",
      buttons: [B("Extend belts", () => { mark(2); send("extend"); }), B("Retract again", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    case "extending": return { text: "Pull each belt out and attach it to its anchor. Press Done when all four are attached.",
      buttons: [B("Done extending", () => send("doneExtending")), stop("Stop extending")] };
    case "extstopped": return { text: "Extending stopped. Extend again, or retract from where the belts are.",
      buttons: [B("Extend again", () => send("extend")), B("Retract belts", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    case "extended": return { text: "Belts attached to the anchors. Apply tension next.", buttons: [B("Apply tension", () => { mark(3); send("tension"); })] };
    case "restored": return { text: "Belt lengths restored from the encoders. A reboot always releases tension, so apply it again.",
      buttons: [B("Apply tension", () => { mark(3); send("tension"); })] };
    case "tensioning": return { text: "Taking up the slack, then checking the position against the anchors.", buttons: [stop()] };
    case "notattached": return { text: "A belt wound in more than 300 mm. Check every belt is hooked to its anchor, then carry on.",
      buttons: [B("Continue applying tension", () => send("tension")), B("Retract belts", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    case "tensionfail": return { text: "The belt lengths don't match the saved anchors. Find anchors measures them again; the belts stay attached.",
      buttons: [B("Find anchors", () => { mark(4); send("findAnchors"); }), B("Apply tension again", () => send("tension"), "ghost")] };
    case "anchors": return { text: "Measuring the frame to find each anchor point. Keep clear of the sled and belts.", buttons: [stop("Stop finding anchors")] };
    case "noanchors": return { text: "Check each belt is hooked to its anchor, then try again.",
      buttons: [B("Find anchors again", () => send("findAnchors")), B("Retract belts", () => { setA({ retractChecked: false }); send("retract"); }, "ghost")] };
    case "tcheck": return d.toolSteps === false
      // The real machine's tool steps wait for Tristan's tool field; until then the user can go on without them
      ? { text: "The tool check isn't connected to the machine yet. Skip it to go on, and set Z the way you do today.",
          buttons: [{ ...B("Skip the tool check", () => { mark(5); setA({ zDone: true }); }),
            temp: "the tool check needs the tool state the firmware is adding" }, stop()] }
      : { text: "Checking for a tool. The spindle rises through the light beam.", buttons: [stop()] };
    case "toolq": return { text: "A bit is loaded. Have you set Z for this tool?",
      buttons: [B("Yes, Z is set", () => { mark(5); setA({ zDone: true }); }), B("No, set Z", () => { mark(5); mark(6); setA({ measuring: true, stickout: null }); }, "ghost")] };
    case "notool": return { text: "No bit loaded. Measure a tool to load one, or continue without a tool.",
      buttons: [B("Measure a tool", () => { mark(5); mark(6); setA({ measuring: true, stickout: null }); }), B("Continue without a tool", () => { mark(5); setA({ zDone: true }); }, "ghost")] };
    case "recovered": return { text: "Job stopped by a spindle fault. Check the bit isn't damaged, then check for a tool again.",
      buttons: [B("Check the tool", () => setA({ faultSeen: true, zDone: false, measuring: false, stickout: null }))] };
    case "toolremove": return { text: "Press Remove tool, then take the bit out so you can measure it.", buttons: [B("Remove tool", () => send("removeTool"))] };
    case "toolfreeing": return { text: "Raising the spindle so the bit can come out. Take it out once the spindle stops.", buttons: [stop()] };
    case "measure": return { text: "Measure how far the bit sticks out of the spindle socket, from the socket face to the tip.",
      buttons: [], input: <StickoutInput onSave={n => setA({ stickout: n })} /> };
    case "toolload": return { text: `Load the bit back in at ${a.stickout} mm stick-out and tighten the collet. The device detects it.`,
      buttons: [B("Measure again", () => setA({ stickout: null }), "ghost")] };
    case "zsetting": return { text: "Setting Z from the measured stick-out.", buttons: [stop()] };
    case "secure": return { text: "Enter the stock thickness. Is your work piece set up and secure?",
      buttons: [B("Release tension", () => { setA({ securing: true }); send("release"); }, "ghost")],
      input: <StockInput value={d.stock} onSecure={n => { mark(7); send("setStock", { stock: n }); setA({ secured: true }); }} /> };
    case "releasing": return { text: "Belts are slack. Secure the work piece, then apply tension again.", buttons: [B("Apply tension", () => send("tension"))] };
    case "choosefile": return { text: "Choose a job file, or upload one.", buttons: [],
      input: <FilePicker files={d.files} onPick={f => { mark(8); setA({ file: f }); }}
        onUpload={() => Promise.resolve(send("uploadFile")).then(f => { if (f) { mark(8); setA({ file: f }); } })} /> };
    case "error": return { text: d.error?.reason || "", buttons: [B("Clear error ($X)", () => send("clearError"))] };
    case "recovery": return { text: "Clear it by re-homing Z. (To be tested on the machine.)", buttons: [B("Re-home Z", () => send("rehomeZ"))] };
    case "incident": {
      const i = d.incident;
      return { text: `${i.title}. ${i.detail} ${i.next}`,
        // After a spindle fault the tool check runs again, so the page forgets the tool and Z answers
        buttons: i.clearWithStop ? [stop("Stop to clear")] : [B(i.ack || "Got it", () => {
          if (i.kind === "spindleFault") setA({ faultSeen: true, zDone: false, measuring: false, stickout: null });
          send("ackIncident");
        })] };
    }
    case "othertab": return { text: "Only one tab per browser can be connected.", buttons: [B("Use this tab", () => send("reconnect"))] };
    case "toomany": return { text: "Close tabs or devices you aren't using.", buttons: [B("Reconnect", () => send("reconnect"))] };
    case "lost": return { text: "Reconnecting automatically. If it doesn't come back, reboot the device or turn it off and on.", buttons: [B("Reboot device", () => send("reboot"), "ghost")] };
    default: return { text: "", buttons: [] };
  }
}

// The main actions outside setup: Run and its relatives, always next to Stop
export function mainActions(k, ctx) {
  const { d, a, setA, send } = ctx;
  const B = (label, onClick, style = "") => ({ label, onClick, style });
  // Trace boundary moves around the chosen file's extent (the real machine reads the file for it)
  const trace = d.notYet?.trace
    ? { ...B("Trace boundary", () => {}, "ghost"), disabled: true, title: d.notYet.trace }
    : B("Trace boundary", () => send("trace", { file: a.file }), "ghost");
  switch (k) {
    case "ready": return [trace, B(`Run ${a.file}`, () => setA({ finalCheck: true }), "go")];
    case "stopped": return [B("Run from the start", () => setA({ stoppedSeen: true, finalCheck: true }), "ghost"),
      B(`Continue from line ${(d.lastJob?.line ?? 0).toLocaleString()}`, () => { setA({ stoppedSeen: true }); send("continueJob"); }, "go")];
    default: return [];
  }
}
