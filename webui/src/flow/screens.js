// The screen the UI shows comes from two things (see "Two states the UI has to combine" in REWRITE.md):
// what the device reports, and what the user has answered or confirmed on this page. derive() is that rule.

export const STEPS = ["Confirm the work area", "Retract belts", "Extend belts", "Apply tension", "Find anchors",
  "Check the tool", "Measure the tool", "Secure the work piece", "Choose a job file"];

// Answers the user gives on this page. They reset whenever the page (re)connects, because the device may have
// changed in the meantime: setup always starts again from Confirm the work area.
export const freshAnswers = () => ({
  workAreaConfirmed: false, editingWorkArea: false, retractChecked: false,
  zDone: false, measuring: false, stickout: null, zBase: null, secured: false, securing: false,
  file: null, finalCheck: false, stoppedSeen: false, faultSeen: false, ran: [0]
});

export const T = { setup: "var(--setup)", ready: "var(--ready)", job: "var(--job)", error: "var(--danger)", wait: "var(--muted)" };

// Mode and subheader for each screen, as in the UI modes table. `step` is the setup step it belongs to.
// nopos: no trustworthy position yet. moving: the device is moving, so jogging is locked.
export const SCREENS = {
  connecting: { mode: "Connecting", tone: T.wait, sub: "", msg: "Waiting for the device to report its state", stale: true },
  lost: { mode: "Error", tone: T.error, sub: "Not responding", alert: true, stale: true, msg: "No reply from the device. Values shown are out of date" },
  wifi: { mode: "Connecting", tone: T.wait, sub: "WiFi setup", nopos: true, msg: "Connected through the Maslow hotspot. Choose a network for the device to join" },
  workarea: { mode: "Setup", tone: T.setup, sub: "Confirm the work area", step: 0, nopos: true, msg: "Confirm the work area before anything moves" },
  newarea: { mode: "Setup", tone: T.setup, sub: "Confirm the work area", step: 0, nopos: true, msg: "Set the table size and the spoil board thickness in the Work area pop-up" },
  unknown: { mode: "Setup", tone: T.setup, sub: "Belt lengths unknown", step: 1, nopos: true, msg: "No saved belt lengths, or a retract was stopped. Start by retracting the belts" },
  retracting: { mode: "Setup", tone: T.setup, sub: "Retracting belts", step: 1, nopos: true, moving: true, msg: "Winding each belt in until it meets resistance" },
  retracted: { mode: "Setup", tone: T.setup, sub: "Belts retracted", step: 1, nopos: true, msg: "Retract finished. Check each arm by eye before extending" },
  toextend: { mode: "Setup", tone: T.setup, sub: "Belts retracted", step: 2, nopos: true, msg: "All four belts are fully wound in. Press Extend belts when you're ready" },
  extending: { mode: "Setup", tone: T.setup, sub: "Extending belts", step: 2, nopos: true, moving: true, msg: "Pull each belt out and attach it to its anchor. Press Done when all four are attached" },
  extstopped: { mode: "Setup", tone: T.setup, sub: "Extending stopped", step: 2, nopos: true, msg: "Extending was stopped. The belt lengths are still known, so you can extend again or retract" },
  extended: { mode: "Setup", tone: T.setup, sub: "Belts extended", step: 3, nopos: true, msg: "All four belts are attached to their anchors. Apply tension next" },
  restored: { mode: "Setup", tone: T.setup, sub: "Belts extended", step: 3, nopos: true, msg: "Belt lengths restored from the encoders. A reboot always releases tension, so apply it again" },
  tensioning: { mode: "Setup", tone: T.setup, sub: "Applying tension", step: 3, nopos: true, moving: true, msg: "Taking up slack, then checking the position against the anchors" },
  notattached: { mode: "Setup", tone: T.setup, sub: "A belt may not be attached", step: 3, nopos: true, alert: true, msg: "A belt wound in more than 300 mm while applying tension. Check every belt is hooked to its anchor" },
  tensionfail: { mode: "Setup", tone: T.setup, sub: "Couldn't confirm the position", step: 4, nopos: true, alert: true, msg: "The belt lengths don't match the saved anchors, so the anchors may have moved. Find anchors next" },
  anchors: { mode: "Setup", tone: T.setup, sub: "Finding anchor points", step: 4, nopos: true, moving: true, msg: "Measuring the frame. This takes about 11 minutes" },
  noanchors: { mode: "Error", tone: T.error, sub: "Can't find anchors", step: 4, nopos: true, alert: true, msg: "Find anchors didn't finish. Check each belt is hooked to its anchor, then try again" },
  tcheck: { mode: "Setup", tone: T.setup, sub: "Checking for a tool", step: 5, moving: true, msg: "Position confirmed. Raising the spindle through the light beam to see if a bit is loaded" },
  toolq: { mode: "Setup", tone: T.setup, sub: "Tool found", step: 5, msg: "A bit is loaded. Have you set Z for this tool?" },
  notool: { mode: "Setup", tone: T.setup, sub: "No tool loaded", step: 5, msg: "No bit was detected. Measure a tool to load one, or continue without a tool" },
  recovered: { mode: "Setup", tone: T.setup, sub: "Job stopped: spindle fault", step: 5, alert: true, msg: "The spindle tripped and the job ended. Z was re-homed. Check the bit is still OK, then check for a tool again" },
  toolremove: { mode: "Setup", tone: T.setup, sub: "Remove the tool", step: 6, msg: "Press Remove tool. The spindle rises so you can take the bit out and measure it" },
  toolfreeing: { mode: "Setup", tone: T.setup, sub: "Removing the tool", step: 6, moving: true, msg: "Raising the spindle until the bit clears the light beam" },
  measure: { mode: "Setup", tone: T.setup, sub: "Measure the tool", step: 6, msg: "Measure how far the bit sticks out of the spindle socket, then enter it" },
  toolload: { mode: "Setup", tone: T.setup, sub: "Load the tool", name: "Load the tool", step: 6, msg: "Put the bit back in at the stick-out you measured. The device detects it when the bit breaks the beam" },
  zsetting: { mode: "Setup", tone: T.setup, sub: "Setting Z", step: 6, moving: true, msg: "Setting Z from the measured stick-out" },
  secure: { mode: "Setup", tone: T.setup, sub: "Secure the work piece", step: 7, msg: "Enter the stock thickness and make sure the work piece is clamped down" },
  releasing: { mode: "Setup", tone: T.setup, sub: "Tension released", step: 7, nopos: true, msg: "The belts are slack so you can reach the work piece. Apply tension again once it's secure" },
  choosefile: { mode: "Setup", tone: T.setup, sub: "Choose a job file", step: 8, msg: "Tool, Z and work piece are ready. Choose what to cut" },
  ready: { mode: "Ready to cut", tone: T.ready, sub: "", msg: "Set home XY, trace the boundary if you like, then run" },
  finalcheck: { mode: "Ready to cut", tone: T.ready, sub: "Final check", msg: "Check these before the job starts" },
  tracing: { mode: "Ready to cut", tone: T.ready, sub: "Tracing the boundary", moving: true, msg: "Moving around the edge of the job with Z raised" },
  parking: { mode: "Ready to cut", tone: T.ready, sub: "Parking", moving: true, msg: "Moving to the park position" },
  stopped: { mode: "Ready to cut", tone: T.ready, sub: "Job stopped", msg: "The job stopped. Z lifted, spindle off. Progress is saved, so you can continue from there" },
  cutting: { mode: "Job", tone: T.job, sub: "Cutting", msg: "The job is running" },
  success: { mode: "Job", tone: T.job, sub: "Success", moving: true, msg: "The job finished. Parking next" },
  spindle: { mode: "Job", tone: T.job, sub: "Spindle fault: recovering", alert: true, moving: true, msg: "The spindle tripped. Ending the job, lifting Z and re-homing it" },
  error: { mode: "Error", tone: T.error, sub: "Emergency stop", alert: true, msg: "The machine stopped itself. Motion is disabled" },
  recovery: { mode: "Error", tone: T.error, sub: "Recovery failed", alert: true, msg: "Spindle recovery didn't finish. The Z position isn't known" }
};

export function derive(d, a) {
  if (!d.connected) return d.everConnected ? "lost" : "connecting";
  if (d.hotspot) return "wifi";
  if (d.fluid === "Alarm") return "error";
  if (d.spindle === "recoveryFailed") return "recovery";
  if (d.spindle === "recovering") return "spindle";
  if (d.job) return "cutting";
  if (d.motion === "parking") return d.lastJob?.result === "success" ? "success" : "parking";
  if (d.motion === "tracing") return "tracing";
  if (!a.workAreaConfirmed) return a.editingWorkArea ? "newarea" : "workarea";
  switch (d.maslow) {
    case 0: return "unknown";
    case 1: return "retracting";
    case 2: return a.retractChecked ? "toextend" : "retracted";
    case 3: return "extending";
    case 5: return "tensioning";
    case 6: return "anchors";
    case 8: return "releasing";
    case 4: {
      const r = d.result || {};
      if (r.step === "tension" && r.outcome === "nofit") return "tensionfail";
      if (r.step === "tension" && r.outcome === "notattached") return "notattached";
      if (r.step === "anchors" && r.outcome === "failed") return "noanchors";
      if (r.step === "extend" && r.outcome === "stopped") return "extstopped";
      if (r.step === "release" && a.securing) return "releasing";
      if (d.restored) return "restored";
      return "extended";
    }
    case 7: {
      if (d.lastJob?.result === "fault" && !a.faultSeen) return "recovered";
      if (!a.zDone) {
        if (a.measuring) {
          if (d.settingZ) return "zsetting";
          if (d.tool === "removing") return "toolfreeing";
          if (a.stickout == null) return d.tool === "loaded" ? "toolremove" : "measure";
          return d.tool === "loaded" ? "zsetting" : "toolload";
        }
        if (d.tool === "loaded") return "toolq";
        if (d.tool === "none") return "notool";
        return "tcheck";
      }
      if (!a.secured) return "secure";
      if (!a.file) return "choosefile";
      if (d.lastJob?.result === "stopped" && !a.stoppedSeen) return "stopped";
      if (a.finalCheck) return "finalcheck";
      return "ready";
    }
  }
  return "unknown";
}

export const isSetup = (k) => SCREENS[k].mode === "Setup";
// The step's name as shown. A screen can rename its step: once the bit is measured, step 7 is about loading it.
export const stepName = (k) => SCREENS[k].name || STEPS[SCREENS[k].step];

// Help shown when the setup bar is expanded, one list per step
export const STEP_HELP = [
  ["Measure the table's usable width and height.", "Keep the saved work area if nothing about the table changed. Setup then skips the steps it already knows.", "Set a new work area if the table changed. That clears the anchors, so every step runs."],
  ["Clear the area around the sled and the arms.", "Press Retract belts. Each belt winds in until it feels resistance.", "Check each arm by eye: every belt should be fully wound in.", "If one stopped early, pull out a little slack by hand and retract again. Stopping a retract loses the belt lengths."],
  ["Press Extend belts.", "Pull each belt out by hand toward its anchor.", "Hook the end of each belt onto its anchor.", "When all four are attached, press Done extending. Stop extending keeps the belt lengths, so you can extend again or retract."],
  ["Check that all four belts are attached and nothing is in their way.", "Press Apply tension. The belts wind in to take up the slack, which takes about 30 seconds.", "The device checks its position against the saved anchors.", "If a belt winds in more than 300 mm, check that it's attached. If the position doesn't fit, Find anchors runs next."],
  ["Keep clear of the sled and belts.", "The sled moves to a grid of points and measures each one. This takes about 11 minutes.", "If it fails, check that each belt is hooked to its anchor, then try again."],
  ["The device raises the spindle through the light beam to check for a bit.", "If a bit is found, say whether you've set Z for this tool.", "If none is found, measure a tool to load, or continue without one."],
  ["Press Remove tool. The spindle rises so the bit can come out, then take it out.", "Measure how far it sticks out of the spindle socket, from the socket face to the tip, and enter it.", "Load it back in at that stick-out and tighten the collet. The device detects it when the bit breaks the light beam.", "Z is set from the measurement."],
  ["Enter the stock thickness.", "Clamp or screw the work piece down so it can't move.", "Belts too tight to reach the clamps? Release tension, secure the work piece, then apply tension again."],
  ["Upload a G-code file (.nc), or choose one already on the device.", "Check the preview fits on the work piece."]
];
