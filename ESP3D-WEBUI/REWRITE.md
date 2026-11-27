# WebUI rewrite

Why we're rewriting the WebUI, what we decided, and what's left. Updated 2026-11-27. To pick the work
up in a new session, read "Where we are", "How to work on it" and "What we still need" › 2.

**Related:**
- `webui/`: the working site, with a simulated device (`webui/README.md`)
- `tools/workflow-map/`: the UI modes map
- `tools/ui-mockups/`: the layout mockups
- `docs/protocol.md`: the firmware's connection protocol
- `.github/copilot-instructions.md`, "Maslow architecture rules": rules every UI change follows
- `ISSUES-LOG.md`: what went wrong on the machine, with evidence
- `BACKLOG.md` at the repository root: known bugs

## Where we are

- **The current UI** is plain HTML and JavaScript in `www/`: about 55 script files (about 16,800
  lines) that share global variables. Gulp packs it into one `index.html.gz` (about 150 KB), which
  the board serves from its 2 MB flash.
- **The machine API (#1132)** is on `Maslow-Main` and merged into `firmwareRewrite`
  (`wscommands.js`, `machine.js`). `Maslow-Main` was merged again on 2026-11-18 (Stop leaves Z where
  it is; Tristan's `setWorkZero`).
- **The new UI** (`webui/`, Preact + Vite, one self-contained page, about 70 KB gzipped) runs the whole
  Mini flow on a real Maslow Mini (`?device=machine`) and on the simulator (`?device=sim`):
  - setup (retract, extend, tension, find anchors, tool check, secure, choose file), Run, Stop,
    Continue from a typed line, Discard, Trace boundary, incidents (acknowledge first);
  - jog pad (one XY step, Z step, labelled arrows, a house for XY home and one for work Z 0), Set home XY /
    Set Z (one click, here), Reset Z (the spindle board's homing, called Home Z before 2026-11-26), Park, Remove tool;
  - Frame tab (live belt lengths, motor current rings, Workspace accuracy overlay, the file's
    toolpath in light gray) and Toolpath tab (the file alone, fitted);
  - files: list, upload, delete; the File card with size and X / Y range; the fit check (below);
  - log with Save, and a command box (off by default, Settings › Advanced);
  - Settings: State, Device, Network, Frame and belts (extend distance, spoil board,
    stock thickness, work area and offset, Park, scale, belt retraction limit, retraction force,
    Workspace tolerance, sled weight), Home (typed home, Reset home), Tool and Z (Spindle on / off,
    Mini only), Display, Advanced (command box, belt motor test).
- **The new UI is the page the board serves on `firmwareRewrite` (2026-11-27).** `npm run build:board` in
  `webui/` gzips it into `index.html.gz` (about 104 KB, checked against the 145 KB budget); `release.yml`,
  `compile-on-review.yml` and `flash-maslow.sh --fs` build it instead of the old UI, and the old UI no longer
  ships. Decided 2026-11-27: this is what `Maslow-Main` should get when `firmwareRewrite` merges, so it's the
  default now, ahead of the rest of part 2. **Firmware update is in Settings › Device** (2026-11-27,
  #1174; a release install tried on the Mini). The Mini serves the new page since 2026-11-27 (flashed with `--fs`);
  other machines run the old page until their filesystem is updated (part 3).
- **Tested on the Mini, from the dev server (2026-11-18):** belt labels, motor rings, command box, Save log, settings read
  and save, Set home, Settings › Home, delete, spindle on / off. **On 2026-11-27:** the Set Z helper (the tip
  landed just short of the work at 30 mm, so seat to work is now 31), mm / inch, Move Z to work Z 0 and Reset Z,
  the fit check (warning shown, Run off past the frame), Continue from a changed line, Discard, Continue with no tool,
  Remove tool and reloading a bit with slack belts (Extended, about 15 s out and 18 s back in),
  the belt motor test (all four arms passed),
  and no `encoderTeeth` warning with the right value (35). Fixed after the test: Check the tool (Settings ›
  State) did nothing after Continue with no tool (the real adapter drops `toolCheck`; it now goes back to the
  tool steps, and Reset Z when Z needs homing), and Remove tool was offered with no bit loaded (`TS:4`).
  **Not yet:** the spoil board box at Find anchors,
  the `encoderTeeth` warning with a wrong value, the phone layout on a real phone.
- **The Mini's tool check is connected (2026-11-20; on the Mini 2026-11-25):** the
  firmware reports the tool state in the status report's `TS` field, and the tool steps and the spindle-fault
  screens follow it. **On the Mini (2026-11-25):** both boards flashed (spindle by USB, boost kept at 0 V; WiFi
  OTA to the spindle board failed both times). Seen working: `TS:3` in the report; the tool check answered by
  itself with no Z move; Remove tool (TS 6 → 4, about 15 s) and reloading the bit (TS 5 → 3, about 12 s), each
  re-zeroing Z; Home Z from the jog pad (re-synced Z after issue 24). The revised Set Z step (2026-11-25): Set Z,
  the Set Z helper (15 mm stick-out after Home Z: offset Z −5, bit lowered to machine Z −5, work Z 0) and Remove
  tool, on the Mini. **Not yet on the machine:** Continue with no tool; reloading the page mid-setup; a spindle fault
  (TS 7, can't be triggered on purpose: unit tests and the simulator only). Home Z clears the work Z offset, so the page goes
  back to Set Z (keeping the work piece and file answers); Reset home resets X and Y only (decided 2026-11-25).
  Home Z from another tab with a bit loaded isn't noticed (the report may never show homing). Issue 24 (Z slips under load) is open; 25 is fixed
  (seat to work set to 31 mm, 2026-11-27). The PR to `firmwareRewrite` should @-mention @tristan-huber for the report field.
- **Next:** the rest of the gap list ("What we still need" › 2), and the 4.1's setup flow.

## How to work on it

- **Run it:** `cd webui && npm run dev -- --port 5199`, then `http://localhost:5199/?device=sim`
  (simulator) or `?device=machine` (the real machine, proxied to `maslow.local`). Only one browser tab
  can drive the machine; a second shows "Connected in another tab".
- **Simulator:** the yellow "Simulated device" panel picks what the firmware reports ("Still
  tensioned" jumps to the tool check). Upload picks a real G-code file from your computer and keeps it
  in the page, so the preview and the fit check use the real file.
- **Build and test:** `npm run build`; `node --test --test-force-exit "src/**/*.test.js"` (the
  simulator's timer keeps the test run open without `--test-force-exit`). The old UI's bun tests
  (`ESP3D-WEBUI/www/js/tests`) need `bun`, which isn't installed on the main Mac.
- **Code map:** `src/flow/` (screens, what each asks, incidents, fit check), `src/ui/` (desktop,
  phone, parts, modals), `src/device/` (simulator, the real-machine adapter `machine.js`, settings),
  `src/art.js` (the SVG drawings), `src/gcode/reader.js`, `src/accuracy.js` (the Workspace map).
- **Test on the machine from the dev server,** not the board: `?device=machine` with the machine on
  the network (`MASLOW_HOST=<ip>` if `maslow.local` is slow to resolve on macOS). Putting the new UI
  on the board (part 3 below) waits until part 2 is done. Flash only through `/flash-maslow`.
- **The fit check** (`src/flow/fit.js`): the chosen job, placed at home, is compared with the work
  area (Settings, 0 = automatic 80 % × 60 % of the frame, plus the centre offset) and the frame
  (the rectangle inside the anchors). Outside the work area: a warning in the Ready bar. Past the
  frame: the warning, and Run, Trace boundary and Run from the start are disabled. The firmware has
  no limits of its own (`soft_limits: false`). Messages don't suggest moving home (confusing next to
  the G-code's own origin).

**Decided (2026-11-11, from the team meetings):**

- **Maslow Mini and Maslow 4.1 get different setup flows.** The UI checks which machine it's
  connected to and picks the flow. The Mini has the spindle board (`SpindleBoard:` in its config: the
  tool beam, Z homing, spindle faults) and no Z steppers; the 4.1 has TMC2209 Z steppers and no
  spindle board. So the tool and Z-homing steps below are the Mini's; the 4.1's Z flow is still to
  define. The UI tells them apart from `[ESP800]`'s `board:` field (see Open questions).
- **mo takes the tool state and Z homing** (firmware and UI), because they're part of the Mini
  flow, which Tristan can't test. Tristan keeps the rest of the firmware work.

## Goals

1. **Easier to change.** Each part of the screen is a component with clear inputs. A new feature
   means a new component, not edits in ten files.
2. **One source of truth.** Machine state comes from one store, `machine.state`, fed by the status
   report. Nothing keeps its own copy.
3. **Works on a phone at the machine,** often over the board's hotspot: touch-sized, readable at
   arm's length, quick to load.
4. **Safer to use.** Actions that move the machine are clear about what they'll do. Stop is
   always reachable.
5. **Testable without a machine.** The simulator covers every flow, including the failures seen on
   the machine.
6. **No loss of features.** Everything the current UI does has a place in the new one before it
   becomes the default.

## Design principles

### Fits on the board

- **Everything is bundled.** No CDN scripts, web fonts or remote images: the page works with no
  internet.
- **One file, served gzipped,** as `index.html.gz`, so the firmware's web server and the update
  process don't change.
- **A size budget (decided 2026-11-14): no bigger than the old page, about 145 KB gzipped.** Every
  dependency justifies its size. **No three.js in the page** (about 120 KB or more gzipped on its
  own): the 2D toolpath preview is a plain canvas or SVG, a 3D preview would be a small hand-written
  WebGL line viewer, and the setup line art is pre-rendered SVG (three.js only as our own export
  tool), animated with CSS. `npm run build:board` fails over the budget (2026-11-27). The budget is ours, not
  the board's: the flash has 2 MB for files.

### The machine is the authority

- **State comes from the machine, never from what we asked for.** The screen follows
  `machine.state`; a button press changes nothing on screen until the machine reports it.
- **Firmware and UI move together.** The UI reads fields the firmware added for it (`MS`, `BL`,
  `STOPPED` and so on). When the UI needs state it doesn't have, the field goes in the status
  report, not a log line. Status report changes need review from Tristan or BarbourSmith.
- **Every connection is equal.** Another browser, a phone or a script may be driving. The UI
  shows changes it didn't cause, and no client is special.
- **The connection can always drop.** The UI shows it, reconnects by itself, and never leaves
  controls looking live while the machine can't hear them.

### Safety

- **Stop is always on screen,** never blocked by a dialog, a loading state or scrolling.
- **No Pause.** Stop ends the job and keeps the line it stopped at, so the job can continue.
- **Motion needs intent.** Belt operations, calibration, large jogs, deleting files and flashing
  firmware have clear labels, and a confirmation where a mistake is costly.
- **Errors are shown, not hidden,** with what to do next.
- **Unavailable means disabled,** not just greyed (the current UI ran Retract from a grey button),
  and every state has a way out.
- **A setting that can cut the connection only sends what was typed, and says so first** (learnt
  2026-11-20, below). Each network setting has its own Save. Network changes warn when the board can't see
  the network, and name the way back in (the hotspot, or USB with "Join WiFi only").
- **Stopgaps are marked.** A control that exists only until the firmware work is finished (such as
  "Skip the tool check" while the tool state isn't reported) is orange with a dashed border, tagged
  "Temporary", and its tooltip says what it's waiting for. Stop stays beside it.

### Code

- **Machine actions go through `machine.js`,** shared with the current UI and used as it is
  (see "Architecture"). No second command path.
- **Small, plain components.** Logic that can be a plain function stays out of components.
  Calibration math, G-code parsing and the preview stay framework-free, with their own tests.
- **Replace, don't mix.** The new UI replaces `index.html.gz` as a whole; it doesn't run panel by
  panel inside the old page. The two share only `machine.js` and `wscommands.js`.
- **Styling:** a small set of design tokens, following the Abundance dark tool style (graphite
  `#3f4243` page, slate `#232832` panels, lilac `#c4a3d5` accents, `#e0e5ef` text, JetBrains Mono for
  numbers). Dark by default, with a light panels option in Settings.
- **Accessible:** real buttons and labels, keyboard reachable, readable contrast, touch targets of
  at least 44 px.
- **Pinned dependencies,** with exact versions.

## Architecture

The screens don't talk to the machine directly. They read a state and call actions on a device
source, and there are two:

- **Simulated** (built): `webui/src/device/simulator.js`. It stands in for the machine and asks the
  tester what happened. It stays as the demo.
- **Real** (to build): Tristan's `machine.js` and `wscommands.js`, imported as they are from
  `ESP3D-WEBUI/www/js/`, the way their bun tests load them.

`machine.js` gives:

- **`machine.state`** and **`machine.on("state", fn)`**, filled from the status report: the Maslow
  state, the FluidNC state, position and work offset, belt lengths and errors, motor currents, the
  running job and a stopped job.
- **Actions** (`retractBelts`, `extendBelts`, `applyTension`, `calibrate`, `releaseTension`, `jog`,
  `moveTo`, `tracePath`, `runFile`, `resumeFile`, `stop`, `getSetting`, `setSetting`, `files.*`).
  Each returns an **Operation**: `.accepted` when the machine took the command, then done, or
  failed with a `reason` (`rejected`, `not-sent`, `disconnected`, `timeout`, `reset`, `cancelled`,
  `failed`, `not-ready`).

**Two states make the screen.** `derive()` in `webui/src/flow/screens.js` combines:

1. **The machine's state:** the Maslow state (how far through belt setup), plus the FluidNC state
   (what it's doing right now). Idle alone doesn't mean ready: `UNKNOWN` and `READY_TO_CUT` are
   both Idle.
2. **The user's answers on this page:** retract checked by eye, Z set, work piece
   secure, file chosen. The machine can't store these, so a second browser or a reload asks
   again, which is safe.

**Screens come from state, never from an Operation.** A phone watching, or a page reloaded
mid-step, has no Operation. Operations only give the person who pressed the button "starting" and
the reason something failed.

**Two positions, both shown.** The machine position is measured from the anchors: X 0, Y 0 is where
Apply tension leaves the sled, and Z 0 is where the bit is seated. Set home XY and Set Z don't move
the machine; they set the work offset, so the work position becomes 0 there. The UI shows the work
position with the machine position under it, and the frame drawing marks home with a crosshair.
These are FluidNC's `MPos`, `WCO` and `WPos = MPos − WCO`.

## Layout

**Desktop: layout 6, Tall compass** (decided 2026-11-04, revised 2026-11-13):
- the app header across the top: the Maslow logo, the connected machine ("Maslow Mini"), the
  connection, and ⚙ Settings. These appear only here;
- under it, the top bar: the setup guide during Setup, the mode and its actions otherwise, and
  always Stop;
- one large panel on the left for the visuals and the device state, sized to fit the window: the
  Toolpath / Frame / 3D tabs, with **Belts** and **Motors** toggles on the right of that row; the
  drawing, with the log as a collapsible overlay on the left and the Belts and Motors overlays on the
  right when toggled on (they'll become a graphic overlay later); and the **Status, Position and
  File** cards along the bottom. It has no Run or Stop of its own;
- Jog / Home tall on the right, where a thumb reaches on a tablet, with File management below it.

**Phone: side tabs** (`webui/src/ui/phone.jsx`, `phone.css`; decided 2026-11-27, replacing layout B's folding
sections, which hid the Setup answers under the Run / Stop bar and scrolled about 300 px at 360 × 640):
- the header: connection and ⚙, then the mode and its one-line message (or the setup progress);
- a rail of tabs down the left edge (icon and label): Setup or Next, Frame, Jog, Files, Details, Log. Each tab is one
  panel that fits the screen without scrolling (checked at 375 × 812 and 360 × 640). The open tab is remembered per
  browser. A dot on Setup / Next means it's asking something while another tab is open, and an incident or error
  brings it forward;
- Setup / Next: the step's drawing, its question and its answers at the bottom of the panel, by the thumb. Outside
  setup (a job running, Ready to cut), the frame drawing, the Position and File cards (progress during a job) and the
  question all at once; in landscape the drawing is on the left and the Position card is left to Jog and Details.
  Frame: the drawing full size with Frame / Toolpath and the Belts, Motors and Area toggles;
- Run and Stop fixed at the bottom. In landscape (height under 500 px) the header is one line, the tabs show icons
  only, and Run and Stop run down the right edge;
- Settings opens as a list of sections, then one section with ‹ All settings (the legacy look keeps its pills).
- Decision note: left rail picked over a right rail (thumb reach) and a bottom tab bar (loses height in landscape),
  2026-11-27. On the Set Z step, the jog pad's Set Z now completes the step too, since the jog pad is on its own tab.

**Legacy UI** (2026-11-25, `webui/src/ui/legacy.jsx`): Settings › Display › Theme › Legacy, per browser, or
`?look=legacy` for one visit. For anyone finding the switch hard; it won't get new features, so the new
UI is the default and a bar on top says "You are in the legacy UI" with Switch to the new UI. It draws the old Maslow tab
(state label, Setup / Options / action / Release Tension tiles, the jog grid, file picker, Play / Stop,
serial messages) over the new logic: `machine.state`, `derive()`, actions through `machine.js`. It works
like the old page: no setup steps or prompts, the old state names, and it answers the page-only questions
itself (retract checked, tool and Z, work piece secure), so there's no tool check or Set Z step; Define Z
Home is on the jog grid. Kept from the new UI: unavailable buttons are disabled, no Pause, the Play / Stop
row never scrolls away, pop-ups and incidents (an OK alert) cover only the drawing, and Home Z or Remove tool lock
the controls while Z moves. Its Settings tab shows the Maslow and FluidNC lists, as the old page did. About 7 KB gzipped.

**Rules:**

- The mode and Stop are always in view.
- **Prompts are completed where they're asked.** If the top bar asks for something, it can be done
  in the top bar: the stick-out, the job file. Bigger tasks
  open a pop-up from the bar. On a phone, the Setup / Next tab plays this part.
- **The setup bar expands** with an arrow on its bottom edge: a big step graphic beside fuller
  numbered instructions. It's closed by default. The big graphic is where the line art for
  first-time users goes (on hold).
- **The Status, Position and File cards:** the File card shows the progress bar, line and percent
  during a job, otherwise the chosen file's size, depth and line count (from the G-code reader). On a
  phone they're on the Details tab, with Belts (live lengths, `BL`) and Motors (currents, `MC`)
  as tabs. Values go grey with "out of date" when the machine stops answering.
- **Settings pop-up** (⚙ at the end of the top bar; draft):

| Section | Holds |
|---|---|
| State | The mode and every machine action in a fixed order, greyed out with a reason when unavailable |
| Device | Name, firmware version and update, reboot |
| Network | WiFi network, how it connects (WiFi with hotspot fallback, or hotspot only), hotspot |
| Frame and belts | Extend distance (0 = until Done extending), spoil board thickness, stock thickness (optional), work area (0 = automatic), park position, belt retraction limit, anchors, work area centre offset, scale, retraction force |
| Tool and Z | Last stick-out, default bit |
| Display | Dark or light panels, units |
| Advanced | Raw FluidNC and Maslow settings, `maslow.yaml` |

Actions that move or restart the machine are unavailable during a job or while disconnected, with
a note saying why.

## Workflow

### Modes

Four modes: **Setup**, **Ready to cut**, **Job** and **Error**, each with a subheader for what's
happening. **Connecting** is a short wait at page load or reconnect, until the first status report
arrives. The full table of states, subheaders and actions is the UI modes map.

### Setup steps

1. Retract belts
2. Extend belts
3. Apply tension
4. Find anchors
5. Check the tool
6. Set Z
7. Secure the work piece
8. Choose a job file

- **No work area to confirm (decided 2026-11-13).** The work area is worked out from how far the
  belts extend, so setup starts straight from the machine's state. Steps the machine already knows
  show as skipped, not done.
- **Extend distance** is in Settings › Frame and belts. It defaults to **0, meaning "extend until
  the user presses Done extending"**. A set length is for a machine standing vertically, where the
  belts can't be pulled out by hand. Until the Done extending firmware change exists, the real
  machine can't take 0 (it would extend 0 mm), so the page refuses to save it and says why.
- **Setup starts from the Maslow state:**

| Maslow state | Setup starts at |
|---|---|
| `UNKNOWN`, no belt known | Retract |
| `UNKNOWN`, some belts known (`BL` has numbers) | Apply tension, which recovers the rest |
| `RETRACTED` | Check each arm by eye, then Extend. Extending never starts on its own |
| `EXTENDEDOUT` | Apply tension |
| `TENSIONED` (10) | "The belts don't fit the saved anchors": Find anchors, or Apply tension again, Release tension, Retract |
| `READY_TO_CUT` | The tool check |

- **After a reboot, Apply tension always runs:** the machine comes back as `RETRACTED` or
  `EXTENDEDOUT`, never `READY_TO_CUT` or `TENSIONED`.
- **Extend ends when the user says the belts are attached** (Done extending; needs firmware).
  Today it ends by itself at `extendDist`.
- **Spoil board thickness** raises the sled above the frame, so the firmware adds it to every
  anchor's height when it works out belt lengths. A wrong value costs accuracy, most near the
  anchors. It's set in Settings › Frame and belts.
- **Spoil board at Find anchors, stock in Settings** (decided 2026-11-26, replacing 2026-11-13). Since
  Maslow-Main #1164, Find anchors and cutting both work out belt lengths from anchor Z + spoil board + stock
  thickness (`MaslowKinematics.cpp`, `Calibration.cpp`). Anchors and the spoil board rarely change; the
  stock does. So:
  - **Find anchors offers the spoil board thickness** (revised again 2026-11-26, built): the screens that lead
    to Find anchors (`tensionfail`, `noanchors`) have a Spoil board box filled in with the saved value and a
    Save (`setSpoilboard`). The stock isn't asked. Both are also in Settings › Frame and belts, default 0 in
    the shipped `maslow-mini.yaml` and `maslow-m4.yaml` (the simulator's 12.7 / 6.35 are demo values only).
    The legacy layout doesn't get the box: it keeps them in its Settings tab, as the old UI did.
  - **The setup notes say to set them:**
    - Find anchors' expanded help: if the sled sits on a board while calibrating, enter that thickness first,
      for accuracy; keep the stock at 0 unless the sled is on the work piece now. (Built.)
    - Secure the work piece's tip: to improve accuracy, set the stock thickness in Settings before cutting.
      A new sheet needs only the new thickness, not a new Find anchors (Built.)
- **The tool and Z steps.** Once the position is confirmed, the machine checks for a tool by
  itself: the spindle board raises the spindle through its top-of-travel light beam.
  - **A bit is found: Set Z** (revised 2026-11-27, replacing the separate Set Z helper screen). Two ways: **pick the
    bit** from the tool library (asked at every tool check; the one last used in this browser is marked, not chosen),
    which shows its saved stick-out, still editable, and **Set Z (auto)** lowers the bit to the work and sets work Z 0 there,
    assuming the seated socket face is 31 mm above the work (ISSUES-LOG issue 25); "Another bit" types a stick-out, with
    suggestions (shown at once if the library is empty). Once a bit is picked, Set Z manually is hidden (Back brings it
    back). Or **Set Z manually**: jog the bit to the work, and where it is
    becomes work Z 0. **Remove tool** raises the spindle until the bit is out, then Load the tool. The step is done once
    the report's work offset shows the new Z.
  - **No bit: Load the tool.** The board seats a bit put into the beam by itself, then Set Z. Or
    **Continue with no tool**.
  - **Back** on every step the page asks about (2026-11-25) clears that answer so the step can be redone;
    the belt steps are redone with their own actions.

  These steps are the Maslow Mini's (its spindle board). **Done** (2026-11-20): they follow the report's `TS`
  field (`docs/protocol.md`). The spindle board homes Z at power-up, so on the machine the check is the field's
  answer, not a new move. One stopgap, orange and tagged "Temporary": **Skip the tool check**, only when the
  report has no `TS` (the 4.1, or older firmware). The 4.1 sets Z with its own steppers; its
  flow is still to define.
- **Secure the work piece** asks rather than assumes: the user may have calibrated on top of a
  large enough work piece. Its tip: belts too tight? Release tension, secure it, Apply tension
  again; the belt lengths are kept.
- **Ready to cut shows only once all nine steps are done or skipped,** even though the machine
  reports `READY_TO_CUT` earlier. **Jogging unlocks once the position is confirmed** (steps 6 to 9),
  and locks while anything moves, while the belts are slack, and in Error.

Run the tool check again after a power cycle, a page load, a tool change, re-homing Z, or a spindle
fault recovery; not after a finished or stopped job, or releasing and re-applying tension.

### Ready to cut and jobs

- **Ready to cut** offers Trace boundary (optional), Run, jog and Park. Trace boundary is
  `tracePath()`; Stop leaves the machine where it stopped.
- **Run starts the file** (decided 2026-11-13): there's no final checklist before a job. The setup
  steps already cover the work piece and the file.
- **Job: Cutting** while a file runs. Progress is the report's running line ÷ the file's line count,
  counted from the file the preview loads; the report's `SD` percent is bytes read, not progress.
- **Stop** (`machine.stop()`) sends 0x18, which halts at once and keeps the belt positions, then
  `$STOP`, which saves. Z stays where it stopped (Maslow-Main, 2026-11-17), so the bit may still
  be in the work; Continue first raises Z to at least 2 mm above the work Z 0. The job leaves `STOPPED:line,file` in the report, so
  every browser sees it. Ready to cut then shows **Job stopped** with Continue from line N (the line
  can be changed), Run from the start, or choose another file.
- **Success:** the job ends with no `STOPPED`. Park follows a success (not a stop or a fault).
  Who starts Park, the UI or the firmware, is open.
- **Stop during Setup:** Apply tension ends in `EXTENDEDOUT` (or `UNKNOWN` when it was recovering
  belts); a stopped Retract loses the belt lengths. A Stop during a belt operation can flash
  `ALARM` "Homing fail reset" (backlog). The UI treats an alarm right after its own Stop as part of
  the stop.

### Faults and errors

**Acknowledge first.** When something happens that the user has to act on, the page doesn't jump
straight to the next step. It shows an Error screen that says what happened and what comes next, and
waits until the user acknowledges it ("Got it"). Then the flow carries on from the machine's state:
setup again after a restart, Job stopped (with Continue) after a job error, the tool check after a
spindle fault. A hold is cleared with Stop instead. These incidents live in `webui/src/flow/incidents.js`:

| Incident | Raised when | After acknowledging |
|---|---|---|
| The machine restarted | The page reconnects and the machine is back in an earlier setup state than it left (a boot restores Belts retracted or Belts extended at best) | Setup again, from the step the state calls for |
| The job stopped on an error | A job ends on an error in the file | Job stopped: continue from the line, or run from the start |
| The spindle tripped | The report's `TS` leaves 7 (recovering): the machine ended the job and re-homed Z, or gave up (`TS` 1, needs homing: then Re-home Z) | The tool check, then Job stopped |
| The machine is holding | Held with no job running | Cleared with Stop |

- **Spindle fault:** the machine holds, ends the job, re-homes Z (up to the top of travel), and reports
  `READY_TO_CUT` again; the job leaves `STOPPED`. The UI shows **Job: Spindle fault: recovering**,
  with no Run, until recovery finishes, then the tool check. **Done** (2026-11-20): from `TS` 7, since during
  recovery the machine already looks ready. (Maslow Mini only.)
- **Emergency stop** comes from the machine's button or an error it detects (such as a belt over
  15 mm off target during a job), never from the UI. Alarm wins over every other state. Motion
  stays blocked until `$X` (Clear error) or a power cycle. Z isn't raised, so the bit may be in the
  material: check it, then go back through Setup.
- **Connection:**
  - **Not responding:** nothing heard for a few seconds. The UI reconnects by itself every 3 s.
  - **Another tab took over** (close code 4001) and **too many connections** (4002): a notice with
    a button, and no automatic reconnect.
- **Failure reasons** (a belt wound in more than 300 mm, anchors that can't be solved, the alarm
  reason) are only log text today. Tristan is adding them to the report.

## Known machine behaviour

From testing on 2026-11-06 (`ISSUES-LOG.md`) and reading the firmware. Recheck on #1132 firmware.

- **Find anchors** measures 33 points in about 11 minutes. It needs a progress view.
- **Apply tension** takes about 30 s and checks the saved anchors:
  - within 12 mm: ready;
  - 12 to 25 mm: a warning, then ready;
  - over 25 mm, or lengths that don't fit: `TENSIONED`;
  - a belt winding in more than 300 mm: probably not attached, so "check the belts", not Find anchors.
- **A retract can report success with the belts still out.** Under tension it finished in under
  2 s and zeroed the lengths with the belts about 680 mm out. The check by eye after Retract stays.
- **Boot can restore the wrong state:** it once came back `RETRACTED` with the belts about 680 mm out.
- **Anchor points survive a restart** (`maslow.yaml`), but nothing records whether Find anchors ever
  ran, or for which work area. So the UI can't decide on its own to skip Find anchors.
- **The tool beam works:** Home Z detects a loaded bit, Remove tool ejects it (about 14 s), and
  inserting a bit is detected by itself (about 13 s).
- **Spindle over-current** about 1 s into spin-up on a first job (issue 17); recovery took 8 s.
- **Apply tension can stay in Home** if its measurements disagree nine times in a row: the Maslow
  state goes to `EXTENDEDOUT` but the FluidNC state doesn't return to Idle (from the code, not
  tested). Stop clears it.
- Findings from older firmware, still to recheck, are in `ISSUES-LOG.md` ("From earlier testing").
- **Locked out by the network settings (2026-11-20).** Testing the fallback, we saved a WiFi network
  that doesn't exist. The board fell back to its hotspot as it should, but the hotspot refused
  `12345678`: the browser had filled the Hotspot password box (then `type=password`) and the new
  UI saved it. Network settings were also greyed out without the socket, so the page that should fix
  it couldn't. What changed: password boxes are text drawn as dots, ignored by password managers,
  with a show button; each setting saves on its own; warnings for a network the board can't see and for
  the way back in; Network works over HTTP without the socket. For testing: **before any test that
  takes the board off WiFi, join its hotspot (or plug in USB) while it's still reachable.** The dev
  server can't follow the board to the hotspot, so recover from the board's own page,
  `http://192.168.0.1`.

## What we still need

To replace the current UI on the board. The simulator isn't on this list: it stays as the demo.

**1. Connect the screens to the machine** (ours, in `webui/`). **Done by 2026-11-18**; kept as a record:

1. **Load `wscommands.js` and `machine.js`.** Before importing them, provide the globals they read:
   - `ws_source`: the open WebSocket;
   - `addMessage(text)`: where `wscommands.js` reports problems (our Log panel);
   - for `machine.files`, the helpers from the current `http.js` (`SendGetHttp`,
     `buildHttpFileCmd`, `SendFileHttp`, `BuildFileUploadFormData`, `httpCmd`,
     `http_communication_locked`): import it too, or write small stand-ins.
2. **A socket module** (the current one is `socket.js`):
   - open `ws://<host>/` (the firmware's socket is at `/`) with the `arduino` subprotocol;
   - on every open, reset `wsCommands` and send `$Report/Interval=50`, `?`, `$G`, `$MINFO` and our
     setting reads;
   - pass every line to `wsCommands.handleLine` and `machine.handleLine`;
   - reconnect every 3 s, except after close codes 4001 and 4002.
3. **A switch between the two device sources:** real when the machine (or
   `ESP3D-WEBUI/dev-server.js`, which proxies to `maslow.local`) serves the page, simulated
   otherwise.
4. **One state shape for `derive()`:** the simulator renames its fields to `machine.state`'s
   (`maslow`, `grbl`, `mpos`, `wco`, `belts`, `motorCurrent`, `job`, `stopped`, `conn`).
5. **Operation feedback** on every button: "starting", then the result or the reason it failed.
6. **The new screens:** `TENSIONED`, `UNKNOWN` with known belts, Job stopped with Continue, the
   connection notices, an alarm after our own Stop.
7. **Job progress, and the Belts and Motors tabs** (mA = 1000 × 3.3 × ADC / 4095 / 1.5).

**2. Everything else the current UI does** (goal 6). Cross-checked against every tab and pop-up
of the current UI on 2026-11-18 (its Tablet, Main and Settings tabs show; Camera, Config and
Debugging are hidden by default).

Already in the new UI: Retract, Extend, Apply tension, Find anchors, Release tension, Stop; jog with
a step, go to XY home, go to work Z 0, Set home XY, Set Z, Reset Z, Remove tool, Park; SD files (list, upload,
delete); Run, Continue after a stop, Trace boundary; log, state, toolpath on the frame, motor
currents; Extend distance, spoil board and stock thickness, Reboot.

*Machine settings* (Settings › Frame and belts, or a new Machine section):

| Current UI | Where it is today | In the new UI |
|---|---|---|
| WiFi network and password | Calibration › Config, `setupdlg.js`, `scanwifidlg.js` | **Done** (2026-11-20; read, scan, save and restart tested on the Mini from the dev server): Settings › Network, `webui/src/device/network.js`. Shows where the board is now (`[ESP420]`), reads the settings (`[ESP400]`), scans (`[ESP410]`), saves each changed one (`[ESP401]`, value %-encoded): WiFi network and password, how it connects (`WiFi/Mode`: "WiFi, with the hotspot as a fallback" (STA>AP) or "Hotspot only" (AP). WiFi only (STA) and Off aren't offered, decided 2026-11-20: the board always keeps a way in without USB. A board set to STA elsewhere shows it, with a warning, so it can be changed. Hotspot only is for a machine always used over its hotspot; the WiFi setup screen offers it as "Use the hotspot only", which takes effect at once), hotspot name and password. Then Restart now (`[ESP444]RESTART`, now wired on the machine too). Settings › Device › Name sets `Hostname`. The hotspot screen's Join network opens it. Password boxes are text drawn as dots with a show button (not `type=password`, see "Locked out" under Known machine behaviour), and each setting has its own Save (the WiFi network and its password save together), so one change can't carry another; each Save warns before a risky change (`netWarnings`). A password can't be cleared (the firmware reads an empty value as a query), so joining an open network keeps the old password stored. On each connect the adapter reads `[ESP420]`: the hotspot running with no WiFi joined, and `WiFi/Mode` not "Hotspot only" (`onFallbackHotspot`; `[ESP400]` is read only then, it's about 40 KB), sets `d.hotspot`, so a first boot or a board that can't reach its network opens on the WiFi setup screen. Tested on the Mini on 2026-11-20: a saved network it couldn't find, the fallback hotspot, the WiFi setup screen from the dev server, and back to the home WiFi. To repeat it: join `maslow` (password `12345678`), run the dev server with `MASLOW_HOST=192.168.0.1`; to get there, save a WiFi network that doesn't exist and restart (it falls back; type the real network and password again from the hotspot). "Hotspot only" should open on the normal flow instead |

*Actions* (Jog / Home panel, or Settings › Advanced):

| Current UI | Where it is today | In the new UI |
|---|---|---|
| Spindle on / off | Spindle controls | **Done** (2026-11-18): Settings › Tool and Z, Mini only (the 4.1 has `NoSpindle`). `machine.spindleOn()` (`M3 S12000`) / `spindleOff()` (`M5`); follows the report's `FS` speed, with a "Spindle running" tag in the header. Off during a job and while moving; `$STOP` turns it off |
| Belt motor test (Test) | Calibration popup | **Done** (2026-11-18): Settings › Advanced, `machine.testMotors()` (`$TEST`). Only while Idle with slack belts (Maslow 0, 2, 4): each motor pays its belt out at full power for up to 100 ms; shows each arm's result |
| Set Z-Stop | Calibration popup | Not used on the Mini; the 4.1 needs it (its flow isn't defined) |
| Set home by typing X / Y / Z | Set Home, Set Z Home popups | **Done** (2026-11-18): Set home XY / Set Z open a box filled in with "here"; XY in machine coordinates, Z relative to the current Z home |
| mm / inch | Units toggle | **Done** (2026-11-27, simulator only): Settings › Display › Units, per browser (the legacy look has the old Toggle Units button beside its position). Display and input only, `webui/src/units.js`: the machine is always sent mm, untouched boxes save the stored value (no drift); inch boxes take fractions ("1 1/2", `3/4"`); belt lengths, belt errors and the settings lists stay mm. Firmware checked: `report_inches` ($13) defaults off and the UI assumes it is (a client setting `$13=1` would break every position); Continue after a stopped G20 job replays the skipped lines in check mode, so G20 is restored before the entry move |
| Discard a stopped job | Discard button | **Done** (2026-11-18): Discard on the Job stopped bar (`$SD/Discard`) |
| Change the Continue line | Resume line with ▲▼ | **Done** (2026-11-18): a Line box on the Job stopped bar, the stopped line to start |

*Support and maintenance* (Settings › Advanced, or a Tools section):

| Current UI | Where it is today | In the new UI |
|---|---|---|
| Command box (send `$` commands, verbose) | Main › Commands (`commandpanel.html`, `commands.js`) | **Done** (2026-11-18): off by default, turned on in Settings › Advanced (per browser); under the log, sent as typed through `wsCommands` (`[ESP…]` over HTTP), up/down history. The log keeps 200 lines |
| Save the log to a file | Save Serial | **Done** (2026-11-18): the download icon by the Log title saves `maslow-log-<date>-<time>.txt` |
| FluidNC and Maslow settings list | Settings tab (`settingstab.html`, `settings.js`) | **Done** (2026-11-20, simulator only): Settings › Advanced, tabs Tools / Maslow / FluidNC. Read with `[ESP400]`, typed editors, filter, Save sends only the changes (`[ESP401]` for flash settings; `setSetting()` then `$CO` for config items); off while moving or disconnected; "Restart" tag on network settings. `webui/src/device/settingslist.js` |
| Config editor | Config tab (`configtab.js`, hidden) | **Done** (2026-11-20): every running config item (`maslow.yaml`) is editable in Settings › Advanced › Maslow / FluidNC. **Done** (2026-11-20, simulator only): editing the file as text (adding sections, items locked while running such as `stepping/engine`) is Settings › Advanced › Files › maslow.yaml › Edit: keeps the original as `maslow.yaml.bak` before the first save, Undo save, then Restart now |
| Flash files (`maslow.yaml`, `index.html.gz`) | `SPIFFSdlg.js` | **Done** (2026-11-20, simulator only): Settings › Advanced › Files (`webui/src/ui/files.jsx`, `webui/src/device/flashfiles.js`): list, folders, upload, download, rename (with index-old / index-new names offered, a taken name refused), delete; warnings for the page and the config; off during a job or while disconnected. Uses `/files` as `SPIFFSdlg.js` does |
| Firmware update, check for updates | `updatedlg.js`, `checkupdatesdlg.js` | **Done** (2026-11-27, #1174; on the Mini: from `maslow.local`, the Release stream's v1.27.0 installed over the real path (GitHub list, `web-assets` mirror, `/updatefw`, restart), `[ESP800]` then reported v1.27.0 and the config was unchanged. Not yet on a machine: an install newer than the board, the top-bar notice, files from the computer): Settings › Device (`webui/src/ui/device.jsx`, `webui/src/device/update.js`). The browser checks GitHub's release list (says so plainly when it has no internet, e.g. on the hotspot) and shows the newest release in the stream with its notes. Install downloads `firmware.bin` and `index.html.gz` from the `web-assets/<tag>/` mirror, checked against that folder's `manifest.json` (tag, size, sha256 where the browser can: not over plain http); no fallback to `latest/`. Or choose the files from the computer. Each firmware is checked first: an ESP32-S3 app image, a Maslow build, its version read from the file (one firmware fits both boards, so no board check). Confirmation says it restarts and setup runs again from Apply tension. Then the page goes to `/files`, the firmware to `POST /updatefw` with progress (XHR), the restart, the reconnect, and `[ESP800]`'s version compared with the one installed. Off during a job, while moving, disconnected or read-only. Never automatic. Once per page load, after `[ESP800]` answers, the page checks the chosen stream quietly; a newer release shows "Firmware vX available" in the top bar, which opens Settings › Device (× hides it until the next release; not in the legacy look) |
| System status (`[ESP420]`) | `statusdlg.js` | **Done** (2026-11-27, #1174): Settings › Device › System status, `[ESP420]` plus `$wifi/status` (signal, channel, gateway, DNS) and the flash's free space; and this page's version (`git describe` at build time). The firmware reports no uptime |
| Motor current history (the old Debugging tab had live gauges only, which the motor rings cover) | Debugging tab (hidden) | **Done** (2026-11-20, simulator only): Settings › Jobs lists the last 10 jobs (when, how long, how it ended); each opens to its motor current and belt error chart, a CSV download and the job's log (Download log). Recorded in this browser (one row a second, IndexedDB); only while the page is open, so a sleeping phone or background tab leaves gaps. `webui/src/device/jobrecord.js` |
| Feed override, motors off, probe | Main › GRBL and Controls panels | **Dropped** (decided 2026-11-27). Probe may come back with the 4.1's Z flow |
| Camera (IP webcam tab) | `camera.js` | **Dropped for now** (decided 2026-11-27); might come back as a Settings field plus a panel |
| Languages | `translate.js`, `js/language/` | **To try** (decided 2026-11-27): translations for the new UI; size and how strings are looked up still to work out |
| Macros | `macrodlg.js` | **New version, to build** (decided 2026-11-27): named buttons (a label plus one or more command lines) edited in Settings, saved on the board's flash as `macros.json` (the file manager's path), so every connection sees the same set. Shown as a Macros row on the jog pad only once the user has set at least one. Off during a job; a macro that moves the machine asks to confirm |
| Login | `logindlg.js` | **Dropped** (decided 2026-11-27): the firmware is built without `ENABLE_AUTHENTICATION` |
| UI lock | `controls.js` (`lock_UI`) | **Dropped** (decided 2026-11-27): jogging already locks by state. New idea instead: a sleep mode, so an idle page left open goes dormant (no accidental taps, less traffic) until woken |
| Preferences | `preferencesdlg.js`, `preferences.json` | **Dropped** (decided 2026-11-27): Settings › Display covers what's left |
| Credits | `creditsdlg.js` | **One line** (decided 2026-11-27): a one-line credit and the licence in Settings › Device |
| Spindle speed override, jog feed rate (mm/min) | Main › GRBL and Controls panels | **Dropped** (decided 2026-11-27) |
| SD card space (used / total) | Main › SD Files | Missing |
| The file's G-code as text, under the preview | Maslow tab (`tablettab_gcode`) | **Not under the preview** (decided 2026-11-27): a G-code tab in the log instead, with line numbers, the running line highlighted and followed during a job |
| On-screen numpad for the jog step sizes | Maslow tab (`numpad.js`) | **Dropped** (decided 2026-11-27): phones and tablets bring their own number keyboard |

*From Maslow-Main* (merged into `firmwareRewrite` by #1170, 2026-11-25; the current UI has these, the new UI doesn't):

| Change | Current UI / firmware | In the new UI |
|---|---|---|
| Stock thickness counts in Find Anchors (#1164) | Find Anchors and cutting both use anchor Z + spoil board + stock thickness. A wrong value scales X (about 1.7 % with 25 mm of spoil board plus stock) | **Done** (2026-11-26, simulator only): Find anchors offers the spoil board; stock stays in Settings, and Secure the work piece's help says to set it |
| Wrong `encoderTeeth` warning (#1162, #1163) | On connect, compares `encoderTeeth` with the board from `[ESP800]` (22 on the 4.1, 35 on the Mini); the popup gives the scaled Extend distance and says to restart, then Retract, Extend, Apply tension, Find anchors | **Done** (2026-11-26, simulator only): an incident on connect with Fix it (sets the teeth and the scaled Extend distance, `$CO`, then offers Restart now), once per connection (`device/encoderteeth.js`) |
| Move Z to Home: go to work Z 0 (#1157) | Jog pad button, `machine.moveTo({ z: 0 })` (G0). The firmware now allows it in any Maslow state (the belt position-error e-stop runs only in Ready to cut) | **Done** (2026-11-26, simulator only): a house (Z 0) between +Z and −Z, locked like the jog pad; the spindle board's Home Z is now Reset Z (not in the legacy layout) |
| Receive a G-code file from Abundance (#1166) | Abundance opens the machine's UI and hands it a file with `window.postMessage`; the UI uploads and selects it, never runs it (`docs/protocol.md` §7, `jobreceiver.js`) | **Done** (2026-11-26, simulator only): `device/jobreceiver.js` (same rules and replies, tested against the old one), uploads, chooses the file and says so in the header; the simulator panel has Send a file from Abundance |
| Update check streams (#1142) | Release / Experimental / Nightly, newer by semver | **Done** (2026-11-27, #1174; the Release stream tried on the Mini): the same rules in `device/update.js` (stream remembered per browser). No workflow publishes a `nightly` release yet, so that stream finds none |

Most needed first, all done on 2026-11-18 (simulator only so far): the command box and saving the log,
Park and work area settings, spindle on / off, and the belt motor test.

*Machine settings* (Settings › Frame and belts; names, ranges and defaults in `webui/src/device/settings.js`).
Each is read on connect and saved with `setSetting()` for what changed, then `saveConfig()` (`$CO`), as the
current UI's popups do:

| Setting | Firmware setting | Current UI | In the new UI |
|---|---|---|---|
| Work area X / Y, 0 = automatic | `Maslow_Work_Area_X/Y` | Set Work Area | Done. Shows the automatic size (80 % × 60 % of the frame) |
| Work area centre offset X / Y | `Maslow_Work_Area_Center_Offset_X/Y` | Set Work Area | Done |
| Park X / Y / Z | `Maslow_Park_X/Y/Z` | Set Park | Done |
| Scale X / Y | `Maslow_Scale_X/Y` | Set Scale and Thickness | Done |
| Belt retraction limit | `Maslow_Apply_Tension_Belt_Retraction_Limit` | Apply Tension Belt Limit | Done |
| Retraction force | `Maslow_Retract_Current_Threshold` | Calibration › Config | Done |
| Spoil board, stock thickness, extend distance | `Maslow_spoilboardThickness`, `Maslow_workThickness`, `Maslow_Extend_Dist` | Set Scale and Thickness, Config | Done earlier |

Checked against the simulator only, not yet on a machine.

*Planned new features* (not in the current UI; added 2026-11-27):

| Feature | Where | Notes |
|---|---|---|
| Setup line art | The expanded setup bar's big step graphic | On hold since 2026-10-29. Pre-rendered SVG from the machine's 3D model, one named group per part, animated with CSS; three.js only as our own export tool. Pickup: `tools/line-art-prototype/` and the line-art pickup notes. Waits for the new machine's assembled model and a settled step list |
| 3D toolpath viewer | The drawing's 3D tab (there, disabled, "Not built yet") | A small hand-written WebGL line viewer (no three.js in the page, size budget): the file's toolpath in 3D with depth, orbit and zoom, maybe the stock outline and the running line |
| Tool library | Settings › Tool and Z, and the tool steps in Setup | Saved bits (name, type, diameter, stick-out), kept on the board (a `tools.json`, like `macros.json`) so every connection sees the same list. Pick the loaded bit at the tool check; the page remembers the last one used. Later the file's tool could be matched against it (added 2026-11-27). **Done 2026-11-27** (simulator only): `device/tools.js`, edited in Settings › Tool and Z, a Bit picker on the Mini's tool steps; matching the file's tool is still a later idea |
| Stick-out suggestions | The Set Z helper's stick-out box | A drop-down of suggestions while typing: the saved tools' stick-outs and the last few values entered, newest first. Still a free text box; a suggestion only fills it in (added 2026-11-27). **Done 2026-11-27** (simulator only): a datalist of the bits' stick-outs and the last 5 typed in this browser |

*Small style and usability fixes* (collected here and done together in one pass; add to the list as
they come up):

| Where | Problem | Fix to consider |
|---|---|---|
| Settings › Network | After saving a new WiFi password, people don't know they have to restart the board: Restart now is usually below the fold | After a save that needs a restart, prompt for it (Restart now / Later), or scroll the Restart now button into view and highlight it |
| Settings (phone) | The section pills scroll sideways with no sign there are more: Advanced is off screen at 375 px | **Done 2026-11-27:** a list of sections, then one section with ‹ All settings (the legacy look keeps the pills) |
| Long actions (Apply tension, Find anchors, Retract, Extend, reading or uploading a file, Home Z, Remove tool) | Nothing on screen shows the action is still running, so a 30 s Apply tension or a slow file read looks stuck | An activity indicator in the top bar (and the phone's mode bar) while an operation runs: a spinner when the length is unknown, a progress bar when it's known (upload bytes, Find anchors point n of total once the firmware reports it). It follows the machine's state, so every connection sees it (added 2026-11-27) |

**3. Build and ship** (**done on `firmwareRewrite`, 2026-11-27**):

- `npm run build:board` (`webui/scripts/gzip-board.mjs`) gzips the Vite build into `dist/index.html.gz` and
  fails over 145 KB. It's in `firmware/FluidNC/data/`, the release asset and the `web-assets/` mirror.
- `release.yml` and `flash-maslow.sh build --fs` build `webui/` instead of `gulp package`.
  `compile-on-review.yml` builds `webui/` when the PR has it and the old UI otherwise (the workflow runs
  from the default branch's copy, so it also builds older PRs). It only switches once it's on `Maslow-Main`.
- **The old UI doesn't ship** (decided 2026-11-27): not in the data folder, not as a release asset. To go
  back, build it (`cd ESP3D-WEBUI && npm run build:en`) and upload `dist/index.html.gz`, or install an
  older release.
- **Firmware update** is Settings › Device (part 2 above). It writes the release's page as well as its firmware.
- `?device=machine` isn't needed on the board: a production build served over http uses the machine
  (`app.jsx`), and talks to the socket at `/` and to `/upload`, `/command`, `/files` at the page's own address.
- **Putting it on a machine that has the old page** (a firmware update leaves the page alone):
  1. Rename `index.html.gz` to `index-old.html.gz` (flash has room).
  2. Upload the new UI as `index.html.gz`, or install with `--fs` (which replaces the whole filesystem).
  3. To go back, rename the new one out of the way first (`index-new.html.gz`), then
     `index-old.html.gz` back to `index.html.gz`; a rename onto a taken name may fail.
  4. If the new page is too broken to use, `/?forcefallback=yes` opens FluidNC's built-in file
     manager, or the rename works as a plain address:
     `/files?path=/&action=rename&filename=index.html.gz&newname=index-new.html.gz`.
  5. **Once the new UI is at `maslow.local` on the Mini:** send a file from the real Abundance (#1166, PR #1172). Only
     the simulator's stand-in has been tried so far. Check that it lands on the SD card, is chosen as the job file
     and isn't run, that the header says "Received … from Abundance", and that it's refused mid-job.

  An update from Settings › Device replaces `index.html.gz` too (the release's page, as the old UI's update did); uploading a filesystem image (`--fs`) replaces them all.
  A second page isn't possible without a firmware change (the board downloads any file other than
  `index.html`), and isn't needed for now.

**4. New actions in `machine.js`** (**done 2026-11-18**: `park`, `setWorkZero`, `clearAlarm`, `homeSpindleZ`,
`removeTool`, `spindleOn` / `spindleOff`, `testMotors`; kept as a record) (shared with the current UI; each with a bun test; who writes
them is to agree with Tristan). No firmware change needed:

| Action | Sends |
|---|---|
| Set home XY, Set Z | `G10 L20 P0 X0 Y0`, `G10 L20 P0 Z0` |
| Park | a `moveTo()` to the park position, or its own action |
| Clear error | `$X` |
| Check for a tool, remove the tool, re-home Z | `$Spindle/Home`, `$Spindle/RemoveTool`; the screens follow the report's `TS` field |
| Reboot | `[ESP444]RESTART` (HTTP), if it belongs in `machine.js` |

**5. In the firmware** (status report changes are reviewed by Tristan or BarbourSmith, and a field
sent only while it applies, like `STOPPED`, is preferred):

| Need | For | Who |
|---|---|---|
| Tool state and spindle-board state (Z homing, needs homing, recovering from a fault) | The Mini's tool steps and spindle fault screens; refusing jobs while Z can't move (issue 23) | mo. **Done** (2026-11-20, builds; not yet on the machine, needs review from Tristan or BarbourSmith): `TS:n` in the report on machines with a spindle board, from the spindle board's new `Z:` status field; `machine.state.tool`. Refusing jobs while Z needs homing is still open |
| Which machine this is (Maslow Mini or 4.1) | Picking the setup flow | To decide |
| Find anchors progress, e.g. point n of total | The 11-minute Find anchors step | Tristan |
| Why an operation failed, and the alarm reason | The message in the top bar and the Error screen | Tristan |
| Done extending | Belts pay out until the user says they're attached | Tristan |
| Set Z from the measured stick-out | The Mini's Set Z helper (seat to work set to 31 mm, 2026-11-27, ISSUES-LOG 25) | mo |

## Open questions

- **Telling the Mini from the 4.1: answered (2026-11-13).** The firmware identifies its board at boot
  from a resistor divider (`firmware/FluidNC/src/Maslow/BoardId.cpp`), and `[ESP800]`, which the UI
  already sends when it connects, reports it as `board:maslow41`, `board:unified` (the Unified Control
  Board, the Mini's), `board:unified36v` or `board:unknown`. The test Mini reports `board:unified`.
  Still open: whether the Unified board will only ever be in a Mini (if not, the spindle board's link is
  a second sign), and what the 4.1's setup flow is, step by step, in place of the Mini's tool steps.
- **Anchors:** should the firmware record that Find anchors succeeded, so setup can tell measured
  anchors from defaults and skip Find anchors safely?
- **Park after a successful job:** started by the UI or the firmware? It must turn the spindle off
  first.
- **Renaming the machine** (hostname and hotspot name): does it need a restart, and how does the
  page find the machine once `maslow.local` changes?
- **Settings:** which take effect at once, which need a restart, and which to lock during a job.
- **Languages and browsers:** keep the translations? What's the oldest phone we support?
- **Upstream ESP3D-WebUI:** keep any connection, or treat this as our own code?
- **Not in the map yet:** recovering after a power loss, saved anchor setups for each table, and a
  job history.
- **Job history:** builds on the job records in `webui/src/device/jobrecord.js` (file, start, end, outcome,
  per-second currents; format documented there), kept per browser for now.
