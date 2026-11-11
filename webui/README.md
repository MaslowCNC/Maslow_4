# Maslow WebUI (rewrite)

The new WebUI described in `ESP3D-WEBUI/REWRITE.md`, built with Preact and Vite. It doesn't talk to a machine yet:
it runs against a **simulated device**, and whenever the real firmware would report something (a step finishing, a
tool being found, a fault), a "Simulated device" panel asks the tester what happened.

## Run it

    npm install
    npm run dev        # http://localhost:5173
    npm run build      # dist/index.html, one self-contained file

The build is a single `index.html` with everything inlined, so it can later be gzipped and served from the board
like the current WebUI.

## Against a real machine (read-only for now)

    npm run dev                              # then open http://localhost:5173/?device=machine
    MASLOW_HOST=10.0.0.42 npm run dev        # if the machine isn't at maslow.local

`?device=machine` uses the real machine through `ESP3D-WEBUI/www/js/machine.js` and `wscommands.js`, imported
as they are (`src/device/machine.js`). The dev server forwards the socket and file requests to the machine.
`?device=sim` forces the simulator. A production build served by the machine uses the machine by default.

For now the page only reads: the setup and job buttons write "Not sent" in the Log. Stop is the exception:
it sends the real stop (0x18, then `$STOP`) when pressed. The machine needs #1132 firmware.

## How it's put together

| Folder | What's in it |
|---|---|
| `src/device/` | The device. `simulator.js` holds the machine's state (Maslow state, FluidNC state, tool, job, belts) and asks the tester for every response. `commands.js` maps each action to the firmware command it will send, marked `exists` or `planned`. |
| `src/flow/` | The setup flow. `screens.js` turns the device's state plus the user's answers into the screen to show (`derive()`), with each screen's mode, subheader and message. `asks.jsx` says what each screen asks for in the top bar and what its buttons do. |
| `src/ui/` | The screens: desktop layout 6 (`desktop.jsx`), phone layout B (`phone.jsx`), the Settings and Work area pop-ups (`modals.jsx`), shared pieces (`parts.jsx`) and the simulator panel (`sim.jsx`). |
| `src/art.js` | Step graphics and the frame drawing, carried over from the mockup. |

**Swapping in the real device:** the UI only uses `getState()`, `subscribe(fn)` and `send(action, payload)`. A firmware
adapter implements the same three, translating actions with `commands.js` and building the same state shape from
the status report, `$GSTATE`, `$MINFO` and the log. The screens don't change. Actions marked `planned` in
`commands.js` need the firmware changes listed in REWRITE.md before they can work.

**The two states:** as in REWRITE.md, the screen comes from what the device reports *and* what the user has
confirmed on this page (work area kept, retract checked by eye, Z set, work piece secure, file chosen). Those answers
live in the app and reset whenever the page (re)connects.

**Two positions:** the device keeps the machine position (measured from the anchors: X 0, Y 0 is where Apply
tension leaves the sled, Z 0 is where the bit is seated) and the work offset the user sets with Set home XY and
Set Z. The UI shows both: the work position (machine − offset) first, the machine position under it. These are
FluidNC's `MPos`, `WCO` and `WPos`.

**Automatic steps:** once the position is confirmed the app starts the tool check by itself; while waiting for a bit
it watches for it; once the measured bit is seated it sets Z from the stick-out.

## Testing with the simulator

- On load, the panel asks what state the device is in: first boot through the hotspot, no saved belt lengths,
  belts retracted, belts extended after a reboot, still tensioned after a page reload, or an emergency stop.
- Each step that takes time asks for its outcome, including the failures seen on the machine (Apply tension's two
  failures, Find anchors failing or the board resetting, spindle faults, a failed recovery).
- **Cause an event** injects a lost connection, the emergency button or a board reset at any time.
- The panel folds away when there's nothing to answer.
