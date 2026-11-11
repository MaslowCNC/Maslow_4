// Incidents: things that happened which the user has to know about before the flow goes on, such as the machine
// restarting or a job ending on an error. The device raises one in its state (d.incident); the page shows it as an
// Error screen that says what happened and what comes next, and waits until the user acknowledges it. Then the
// device clears it ("ackIncident") and the page carries on from whatever the machine's state now is: setup again
// after a restart, Job stopped after a failed job.
//
// { kind, title, detail, next, ack, clearWithStop }
//   next:          what happens after acknowledging, shown on the screen
//   ack:           the acknowledge button's label (none: the incident clears itself, e.g. once a hold ends)
//   clearWithStop: Stop is how to clear it (a hold), so Stop is the main button

const SETUP_FROM = { 0: "Retract belts", 2: "Extend belts", 4: "Apply tension" };

/** The machine restarted while the page was away: before and after are Maslow states. */
export const restarted = (before, after, reason) => ({
  kind: "restarted",
  title: "The machine restarted",
  detail: `${reason ? `It reported ${reason}. ` : ""}A restart stops any job, releases the belt tension and clears the work Z offset.` +
    (after === 4 ? " The belt lengths were restored from the encoders." : after === 2 ? "" : " The belt lengths weren't restored."),
  next: `Setup starts again${SETUP_FROM[after] ? ` from ${SETUP_FROM[after]}` : ""}. Check the bit and the work piece before cutting.`,
  ack: "Got it, set up again",
  before, after
});

/** A job ended on an error in the file, or another failure. line: where it stopped, if known. */
export const jobFailed = (detail, line) => ({
  kind: "jobFailed",
  title: "The job stopped on an error",
  detail: `${detail}.`,
  next: line ? `You can continue from line ${line.toLocaleString()} once it's fixed, or run the file from the start.` : "You can run the file again once it's fixed.",
  ack: "Got it"
});

/** The machine is holding with no job running, for example after a spindle fault. Stop clears it. */
export const holding = () => ({
  kind: "holding",
  title: "The machine is holding",
  detail: "Something paused the machine, for example a spindle fault. It won't move until the hold is cleared.",
  next: "Press Stop to clear the hold. If the spindle faulted, re-home Z before cutting.",
  clearWithStop: true
});

/** The spindle tripped during a job; the machine ended the job and re-homed Z (simulator, and later the firmware field). */
export const spindleFault = (line) => ({
  kind: "spindleFault",
  title: "The spindle tripped",
  detail: `The job stopped${line ? ` at line ${line.toLocaleString()}` : ""}, and the machine re-homed Z.`,
  next: "Check the bit isn't damaged. The tool check runs next, then you can continue the job.",
  ack: "Got it, check the tool"
});
