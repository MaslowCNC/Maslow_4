import { useEffect, useMemo, useState } from "preact/hooks";
import { createSimulator } from "./device/simulator.js";
import { createMachine } from "./device/machine.js";
import { derive, freshAnswers, isSetup } from "./flow/screens.js";
import { Desktop } from "./ui/desktop.jsx";
import { Phone } from "./ui/phone.jsx";
import { TOLERANCE, SLED_MASS } from "./accuracy.js";
import { Settings } from "./ui/modals.jsx";
import { SimPanel } from "./ui/sim.jsx";
import { createRecorder } from "./device/jobrecord.js";
import { openJobStore } from "./device/jobstore.js";
import { exampleJobs } from "./device/jobrecord.sample.js";

// The device behind the UI: the real machine (machine.js) or the simulator, both with the same interface
// (getState, subscribe, send). ?device=machine or ?device=sim picks one. Otherwise the page uses the machine
// when the machine serves it (a production build over http), and the simulator in development.
const choice = new URLSearchParams(location.search).get("device");
const useMachine = choice ? choice === "machine" : import.meta.env.PROD && location.protocol.startsWith("http");
const device = useMachine ? createMachine() : createSimulator();
// Records each job's motor currents while this page is open (Settings › Jobs). The simulator also shows
// example jobs (one of each outcome), never saved, so the list has something in it before a job has run.
const recorder = createRecorder({ device, store: openJobStore(), examples: device.isSimulated ? exampleJobs() : [] });

function useDevice() {
  const [d, setD] = useState(device.getState());
  useEffect(() => device.subscribe(setD), []);
  return d;
}

function useNarrow(px = 760) {
  const q = `(max-width: ${px}px)`;
  const [narrow, setNarrow] = useState(() => matchMedia(q).matches);
  useEffect(() => { const m = matchMedia(q); const on = () => setNarrow(m.matches); m.addEventListener("change", on); return () => m.removeEventListener("change", on); }, []);
  return narrow;
}

// The workspace tolerance (mm) is a display preference, remembered in this browser only. Storage can be blocked
// (private windows), so it falls back to the default.
const TOL_KEY = "maslow.workspaceTolerance";
function useStored(key, fallback) {
  const [val, setVal] = useState(() => { try { const v = parseFloat(localStorage.getItem(key)); return v > 0 ? v : fallback; } catch { return fallback; } });
  const set = (v) => { setVal(v); try { localStorage.setItem(key, String(v)); } catch { /* not remembered */ } };
  return [val, set];
}
const useTolerance = () => useStored(TOL_KEY, TOLERANCE);
// The command box under the log (Settings › Advanced): off unless turned on, remembered in this browser
function useStoredFlag(key) {
  const [on, setOn] = useState(() => { try { return localStorage.getItem(key) === "1"; } catch { return false; } });
  const set = (v) => { setOn(v); try { localStorage.setItem(key, v ? "1" : "0"); } catch { /* not remembered */ } };
  return [on, set];
}
// The sled's mass with the router (kg), for the workspace on a vertical frame. Also per browser, until the firmware
// has a setting for it
const useSledMass = () => useStored("maslow.sledMass", SLED_MASS);

export function App() {
  const d = useDevice();
  const [a, setAnswers] = useState(freshAnswers);
  const [modal, setModal] = useState(null);           // { name: "settings", section }
  const [theme, setTheme] = useState("dark");
  const [tolerance, setTolerance] = useTolerance();
  const [sledMass, setSledMass] = useSledMass();
  const [commandBox, setCommandBox] = useStoredFlag("maslow.commandBox");
  const narrow = useNarrow();

  const setA = (patch) => setAnswers(prev => ({ ...prev, ...patch }));
  const mark = (step) => setAnswers(prev => prev.ran.includes(step) ? prev : { ...prev, ran: [...prev.ran, step] });
  const send = (action, payload) => device.send(action, payload);
  const k = derive(d, a);
  const openSettings = (section = "state") => setModal({ name: "settings", section });
  const ctx = { d, a, setA, send, mark, tolerance, setTolerance, sledMass, setSledMass, commandBox, setCommandBox, openSettings, recorder };

  useEffect(() => { send("connect"); }, []);
  // The device reads the chosen file (for the preview and Trace boundary) whenever the choice changes
  useEffect(() => { send("chooseFile", { file: a.file }); }, [a.file]);
  // A (re)connection means the device may have changed: setup starts again from what the machine reports
  useEffect(() => { setAnswers(freshAnswers()); }, [d.connectId]);
  // Once the position is confirmed, the device checks for a tool on its own
  useEffect(() => { if (k === "tcheck" && d.tool === "unknown" && !d.pending) send("toolCheck"); }, [k, d.tool, d.pending]);
  // Waiting for the bit to be put back in: the board watches the beam by itself
  useEffect(() => { if (k === "toolload" && (d.tool === "removed" || d.tool === "none") && !d.pending) send("watchForTool"); }, [k, d.tool, d.pending]);
  // Set Z (here, or the helper lowering the bit to the work) is done once the report shows the work offset it asked
  // for and the bit there: work Z 0
  useEffect(() => {
    const near = (v) => Math.abs(v - a.zTarget) < 0.005;
    if (a.zTarget != null && !a.zDone && near(d.wco.z) && near(d.pos.z)) { mark(4); mark(5); setA({ zDone: true, helper: false, zTarget: null }); }
  }, [d.wco.z, d.pos.z, a.zTarget]);
  // A new retract, from whichever button, needs a new check by eye
  useEffect(() => { if (d.maslow === 1) setA({ retractChecked: false }); }, [d.maslow]);
  // Tension is back on after releasing it to secure the work piece
  useEffect(() => { if (d.maslow === 7 && a.securing) setA({ securing: false }); }, [d.maslow]);
  // A job starting means any earlier stop or fault has been dealt with, so the next one shows again
  useEffect(() => { if (d.job) setA({ stoppedSeen: false, faultSeen: false }); }, [!!d.job]);

  // Stop is in the top bar, which pop-ups don't cover, so they don't carry their own
  const mprops = { ctx, compact: narrow, stop: false };
  const overlay = modal?.name === "settings"
    ? <Settings key={modal.section} k={k} {...mprops} section={modal.section} theme={theme} setTheme={setTheme} onClose={() => setModal(null)} />
    : null;
  const props = { k, ctx, onSettings: () => openSettings(), overlay };
  return <div class={`appshell${narrow ? " narrow" : ""}`} data-panels={theme}>
    {narrow ? <Phone {...props} /> : <Desktop {...props} />}
    {device.isSimulated && <SimPanel d={d} device={device} />}
  </div>;
}
