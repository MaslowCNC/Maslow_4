import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import { createSimulator } from "./device/simulator.js";
import { createMachine } from "./device/machine.js";
import { derive, freshAnswers, isSetup } from "./flow/screens.js";
import { Desktop } from "./ui/desktop.jsx";
import { Phone } from "./ui/phone.jsx";
import { Legacy, legacyAnswers } from "./ui/legacy.jsx";
import { TOLERANCE, SLED_MASS } from "./accuracy.js";
import { Settings } from "./ui/modals.jsx";
import { SimPanel } from "./ui/sim.jsx";
import { savedStream } from "./ui/device.jsx";
import { createRecorder } from "./device/jobrecord.js";
import { openJobStore } from "./device/jobstore.js";
import { exampleJobs } from "./device/jobrecord.sample.js";
import { handleMessage } from "./device/jobreceiver.js";

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
// The interface (Settings › Display): "new", or "legacy" (the old page's layout, ui/legacy.jsx). Per browser; ?look=legacy
// or ?look=new picks one for this visit without saving it.
const LOOK_KEY = "maslow.look";
const lookParam = new URLSearchParams(location.search).get("look");
function useLook() {
  const [look, setLookState] = useState(() => {
    if (lookParam === "legacy" || lookParam === "new") return lookParam;
    try { return localStorage.getItem(LOOK_KEY) === "legacy" ? "legacy" : "new"; } catch { return "new"; }
  });
  const set = (v) => { setLookState(v); try { localStorage.setItem(LOOK_KEY, v); } catch { /* not remembered */ } };
  return [look, set];
}

export function App() {
  const d = useDevice();
  const [a, setAnswers] = useState(freshAnswers);
  const [modal, setModal] = useState(null);           // { name: "settings", section }
  const [theme, setTheme] = useState("dark");
  const [tolerance, setTolerance] = useTolerance();
  const [sledMass, setSledMass] = useSledMass();
  const [commandBox, setCommandBox] = useStoredFlag("maslow.commandBox");
  const [look, setLook] = useLook();
  const narrow = useNarrow();
  const [notice, setNotice] = useState(null);         // { text, bad }: a file received from Abundance, in the header

  const setA = (patch) => setAnswers(prev => ({ ...prev, ...patch }));
  const mark = (step) => setAnswers(prev => prev.ran.includes(step) ? prev : { ...prev, ran: [...prev.ran, step] });
  // Reset Z (rehomeZ) clears the work Z offset (the firmware zeroes Z at the seated bit), so the page asks for Set Z again.
  // The other answers (work piece, file) stay. A bit already in the beam homes at once, so the report may never show
  // homing: this page's own Reset Z counts too, from any button.
  const zLost = { zDone: false, helper: false, zTarget: null };
  const send = (action, payload) => { if (action === "rehomeZ" && !d.notYet?.rehomeZ) setA(zLost); return device.send(action, payload); };
  // The legacy UI works like the old page: it never asks the page-only questions (retract checked, tool and Z, work piece
  // secure), so derive() goes from tension straight to Ready to cut once a file is chosen
  const ans = look === "legacy" ? legacyAnswers(a, d) : a;
  const k = derive(d, ans);
  const openSettings = (section = "state") => setModal({ name: "settings", section });
  const ctx = { d, a: ans, notice, setNotice, setA, send, mark, tolerance, setTolerance, sledMass, setSledMass, commandBox, setCommandBox, openSettings, recorder };

  useEffect(() => { send("connect"); }, []);
  // Once the board's firmware version is known, check GitHub once per page load for a newer release in the chosen
  // stream (Settings › Device), for the top bar's notice. Never installs; with no internet it says nothing.
  const checked = useRef(false);
  useEffect(() => {
    if (checked.current || !d.connected || !d.fwVersion || d.update?.check) return;
    checked.current = true;
    send("checkUpdates", { stream: savedStream(), quiet: true });
  }, [d.connected, d.fwVersion]);
  // A G-code file from Abundance (device/jobreceiver.js, docs/protocol.md §7): uploaded to the SD card and chosen as
  // the job file, as Upload does. Never run. Refused while a job runs or the machine moves, or before it's connected.
  const live = useRef();
  live.current = { d };
  useEffect(() => {
    const onMessage = (event) => handleMessage(event, {
      ready: () => live.current.d.connected && !live.current.d.closed,
      state: () => ({ job: live.current.d.job, grbl: live.current.d.fluid }),
      upload: async (file) => {
        try {
          return await device.send("receiveFile", { file });
        } catch (err) {
          setNotice({ text: `Could not receive ${file.name}: ${err.detail || err.reason}`, bad: true });
          throw err;
        }
      },
      notify: (name, why) => setNotice({ text: `Could not receive ${name}: ${why.detail}`, bad: true }),
      received: ({ name }) => { mark(7); setA({ file: name }); setNotice({ text: `Received ${name} from Abundance` }); }
    });
    addEventListener("message", onMessage);
    return () => removeEventListener("message", onMessage);
  }, []);
  // The device reads the chosen file (for the preview and Trace boundary) whenever the choice changes
  useEffect(() => { send("chooseFile", { file: a.file }); }, [a.file]);
  // A (re)connection means the device may have changed: setup starts again from what the machine reports
  useEffect(() => { setAnswers(freshAnswers()); }, [d.connectId]);
  // Once the position is confirmed, the device checks for a tool on its own
  useEffect(() => { if (k === "tcheck" && d.tool === "unknown" && !d.pending) send("toolCheck"); }, [k, d.tool, d.pending]);
  // Waiting for the bit to be put back in: the board watches the beam by itself
  // (the legacy UI has no Load the tool step, so there it's whenever the belts are ready and no job runs)
  const waitingForBit = k === "toolload" || (look === "legacy" && d.connected && d.maslow === 7 && !d.job);
  useEffect(() => { if (waitingForBit && (d.tool === "removed" || d.tool === "none") && !d.pending) send("watchForTool"); }, [waitingForBit, d.tool, d.pending]);
  // Set Z (here, or the helper lowering the bit to the work) is done once the report shows the work offset it asked
  // for and the bit there: work Z 0
  useEffect(() => {
    const near = (v) => Math.abs(v - a.zTarget) < 0.005;
    if (a.zTarget != null && !a.zDone && near(d.wco.z) && near(d.pos.z)) { mark(4); mark(5); setA({ zDone: true, helper: false, zTarget: null }); }
  }, [d.wco.z, d.pos.z, a.zTarget]);
  // Reset Z from another connection, when the report shows it homing (TS 2)
  useEffect(() => { if (d.tool === "checking" && a.zDone) setA(zLost); }, [d.tool]);
  // A new retract, from whichever button, needs a new check by eye
  useEffect(() => { if (d.maslow === 1) setA({ retractChecked: false }); }, [d.maslow]);
  // Tension is back on after releasing it to secure the work piece
  useEffect(() => { if (d.maslow === 7 && a.securing) setA({ securing: false }); }, [d.maslow]);
  // A job starting means any earlier stop or fault has been dealt with, so the next one shows again
  useEffect(() => { if (d.job) setA({ stoppedSeen: false, faultSeen: false }); }, [!!d.job]);

  // Stop is in the top bar, which pop-ups don't cover, so they don't carry their own
  const mprops = { ctx, compact: narrow, stop: false };
  const overlay = modal?.name === "settings"
    ? <Settings key={modal.section} k={k} {...mprops} section={modal.section} theme={theme} setTheme={setTheme} look={look} setLook={setLook} onClose={() => setModal(null)} />
    : null;
  const props = { k, ctx, onSettings: () => openSettings(), overlay, closeOverlay: () => setModal(null) };
  return <div class={`appshell${narrow ? " narrow" : ""}`} data-panels={theme}>
    {look === "legacy" ? <Legacy {...props} narrow={narrow} onNewUI={() => setLook("new")} /> : narrow ? <Phone {...props} /> : <Desktop {...props} />}
    {device.isSimulated && <SimPanel d={d} device={device} />}
  </div>;
}
