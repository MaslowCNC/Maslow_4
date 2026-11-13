import { useEffect, useMemo, useState } from "preact/hooks";
import { createSimulator } from "./device/simulator.js";
import { createMachine } from "./device/machine.js";
import { derive, freshAnswers, isSetup } from "./flow/screens.js";
import { Desktop } from "./ui/desktop.jsx";
import { Phone } from "./ui/phone.jsx";
import { Settings } from "./ui/modals.jsx";
import { SimPanel } from "./ui/sim.jsx";
import { FINAL_CHECKS } from "./ui/desktop.jsx";

// The device behind the UI: the real machine (machine.js) or the simulator, both with the same interface
// (getState, subscribe, send). ?device=machine or ?device=sim picks one. Otherwise the page uses the machine
// when the machine serves it (a production build over http), and the simulator in development.
const choice = new URLSearchParams(location.search).get("device");
const useMachine = choice ? choice === "machine" : import.meta.env.PROD && location.protocol.startsWith("http");
const device = useMachine ? createMachine() : createSimulator();

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

export function App() {
  const d = useDevice();
  const [a, setAnswers] = useState(freshAnswers);
  const [modal, setModal] = useState(null);           // "settings"
  const [checks, setChecks] = useState(FINAL_CHECKS.map(() => false));
  const [theme, setTheme] = useState("dark");
  const narrow = useNarrow();

  const setA = (patch) => setAnswers(prev => ({ ...prev, ...patch }));
  const mark = (step) => setAnswers(prev => prev.ran.includes(step) ? prev : { ...prev, ran: [...prev.ran, step] });
  const send = (action, payload) => device.send(action, payload);
  const k = derive(d, a);
  const ctx = { d, a, setA, send, mark };

  useEffect(() => { send("connect"); }, []);
  // The device reads the chosen file (for the preview and Trace boundary) whenever the choice changes
  useEffect(() => { send("chooseFile", { file: a.file }); }, [a.file]);
  // A (re)connection means the device may have changed: setup starts again from what the machine reports
  useEffect(() => { setAnswers(freshAnswers()); setChecks(FINAL_CHECKS.map(() => false)); }, [d.connectId]);
  // Once the position is confirmed, the device checks for a tool on its own
  useEffect(() => { if (k === "tcheck" && d.tool === "unknown" && !d.pending) send("toolCheck"); }, [k, d.tool, d.pending]);
  // Waiting for the bit to be put back in: the board watches the beam by itself
  useEffect(() => { if (k === "toolload" && (d.tool === "removed" || d.tool === "none") && !d.pending) send("watchForTool"); }, [k, d.tool, d.pending]);
  // The bit is seated: set Z from the measured stick-out (planned firmware command). zBase is how many times Z had
  // been set when this measurement started, so measuring the same stick-out twice still sets Z again.
  useEffect(() => { if (a.measuring) setA({ zBase: d.zSetCount }); }, [a.measuring]);
  useEffect(() => { if (k === "zsetting" && !d.settingZ && !d.pending && d.zSetCount === a.zBase) send("setZFromStickout", { stickout: a.stickout }); }, [k, d.settingZ, d.pending, a.zBase]);
  useEffect(() => { if (a.measuring && a.zBase != null && d.zSetCount > a.zBase) setA({ zDone: true, measuring: false }); }, [d.zSetCount]);
  // A new retract, from whichever button, needs a new check by eye
  useEffect(() => { if (d.maslow === 1) setA({ retractChecked: false }); }, [d.maslow]);
  // Tension is back on after releasing it to secure the work piece
  useEffect(() => { if (d.maslow === 7 && a.securing) setA({ securing: false }); }, [d.maslow]);
  // A job starting means any earlier stop or fault has been dealt with, so the next one shows again
  useEffect(() => { if (d.job) setA({ stoppedSeen: false, faultSeen: false }); }, [!!d.job]);
  // A new job starts with a fresh final check
  useEffect(() => { if (k !== "finalcheck") setChecks(FINAL_CHECKS.map(() => false)); }, [k === "finalcheck"]);

  // Stop is in the top bar, which pop-ups don't cover, so they don't carry their own
  const mprops = { ctx, compact: narrow, stop: false };
  const overlay = modal === "settings"
    ? <Settings k={k} {...mprops} theme={theme} setTheme={setTheme} onClose={() => setModal(null)} />
    : null;
  const props = { k, ctx, onSettings: () => setModal("settings"), checks, setChecks, overlay };
  return <div class={`appshell${narrow ? " narrow" : ""}`} data-panels={theme}>
    {narrow ? <Phone {...props} /> : <Desktop {...props} />}
    {device.isSimulated && <SimPanel d={d} device={device} />}
  </div>;
}
