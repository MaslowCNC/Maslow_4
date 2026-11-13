# WebUI rewrite

Why we're rewriting the WebUI, what we decided, and what's left. Updated 2026-11-11, after the
machine API in [MaslowCNC/Maslow_4#1132](https://github.com/MaslowCNC/Maslow_4/pull/1132).

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
- **The machine API (#1132)** is on `Maslow-Main` and merged into `firmwareRewrite`:
  - every command goes over the WebSocket, one line at a time, through `wscommands.js`;
  - the machine's state comes from the status report;
  - `machine.js` gives named actions and one state store.
- **The new UI** is a working site in `webui/`, built with Preact and Vite into one self-contained
  page (about 30 KB gzipped). Every screen and path works on desktop and phone. It runs against a
  simulator that asks the tester what the machine reported.
- **Next:** connect the same screens to a real machine through `machine.js` ("What we still need").

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
- **A size budget** (proposed: 300 KB gzipped) that the build checks. Every dependency justifies
  its size. three.js, for a 3D preview, is under consideration and would be the biggest.

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
   secure, final check. The machine can't store these, so a second browser or a reload asks
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

**Phone: layout B:**
- the mode and connection in the header;
- a small frame drawing;
- sections that fold open (Next or Setup, Details, Jog, Files);
- Run and Stop fixed at the bottom.

**Rules:**

- The mode and Stop are always in view.
- **Prompts are completed where they're asked.** If the top bar asks for something, it can be done
  in the top bar: the stick-out, the stock thickness, the final check's four boxes. Bigger tasks
  open a pop-up from the bar. On a phone, the header and the Setup section play this part.
- **The setup bar expands** with an arrow on its bottom edge: a big step graphic beside fuller
  numbered instructions. It's closed by default. The big graphic is where the line art for
  first-time users goes (on hold).
- **The Status, Position and File cards:** the File card shows the progress bar, line and percent
  during a job, otherwise the chosen file's size, depth and line count (from the G-code reader). On a
  phone they're in the Details section, with Belts (live lengths, `BL`) and Motors (currents, `MC`)
  as tabs. Values go grey with "out of date" when the machine stops answering.
- **Settings pop-up** (⚙ at the end of the top bar; draft):

| Section | Holds |
|---|---|
| State | The mode and every machine action in a fixed order, greyed out with a reason when unavailable |
| Device | Name, firmware version and update, reboot |
| Network | WiFi network, hotspot (placeholder) |
| Frame and belts | Extend distance (0 = until Done extending), spoil board thickness, last stock thickness, work area (automatic), anchors |
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
6. Measure the tool
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
- **Spoil board and stock thickness** raise the sled above the frame, so the firmware adds both to
  every anchor's height when it works out belt lengths. A wrong value costs accuracy, most near the
  anchors. The spoil board is set in Settings › Frame and belts; the stock thickness is asked at
  Secure the work piece, with the last value filled in.
- **The tool and Z steps.** Once the position is confirmed, the machine checks for a tool by
  itself: the spindle board raises the spindle through its top-of-travel light beam.
  - **A bit is found:** "Have you set Z for this tool?" Yes goes on; No leads to Measure the tool.
  - **No bit:** measure one, or continue without a tool.
  - **Measure the tool:** Remove tool raises the spindle until the bit clears the beam; the user
    takes it out, enters how far it sticks out of the socket, and puts it back. The machine
    detects the bit when it breaks the beam and sets Z from the measurement. No "loaded" button.

  These steps are the Maslow Mini's (its spindle board). They need the tool and spindle-board state,
  which mo is adding. The 4.1 sets Z with its own steppers; its flow is still to define.
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
- **Final check:** four boxes in the top bar (file looks right, material thickness, anchors
  secure, spoil board in place). Start unlocks once all are checked.
- **Job: Cutting** while a file runs. Progress is the report's running line ÷ the file's line count,
  counted from the file the preview loads; the report's `SD` percent is bytes read, not progress.
- **Stop** (`machine.stop()`) sends 0x18, which halts at once and keeps the belt positions, then
  `$STOP`, which raises Z 2 mm and saves. The job leaves `STOPPED:line,file` in the report, so
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
| The spindle tripped | The spindle faulted and the machine ended the job and re-homed Z (simulator now; the real machine once the spindle field exists) | The tool check, then Job stopped |
| The machine is holding | Held with no job running | Cleared with Stop |

- **Spindle fault:** the machine holds, ends the job, raises and re-homes Z, and reports
  `READY_TO_CUT` again; the job leaves `STOPPED`. The UI shows **Job: Spindle fault: recovering**,
  with no Run, until recovery finishes, then the tool check. It needs the spindle-board state
  mo is adding, because during recovery the machine already looks ready. (Maslow Mini only.)
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

## What we still need

To replace the current UI on the board. The simulator isn't on this list: it stays as the demo.

**1. Connect the screens to the machine** (ours, in `webui/`):

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

**2. Everything else the current UI does** (goal 6):

| Current UI | Where it is today | In the new UI |
|---|---|---|
| G-code preview | `simple-toolpath.js`, `toolpath-displayer.js` | Placeholder drawing. Reuse it, or a 3D view if three.js comes in |
| SD card files | `files.js` | List, choose and upload done. Delete, rename and folders to add (`machine.files` has them) |
| Flash files (`maslow.yaml`, `index.html.gz`) | `SPIFFSdlg.js` | Settings › Advanced, not designed |
| Config editor | `configtab.js` | Settings › Advanced, not designed |
| FluidNC and ESP settings | `settingstab.html`, `settings.js` | Settings › Advanced, not designed |
| WiFi setup and scan | `setupdlg.js`, `scanwifidlg.js`, `wizard.js` | Settings › Network and the first-boot screen are placeholders |
| Firmware update | `updatedlg.js`, `checkupdatesdlg.js` | Settings › Device has the button, not the flow |
| Command box and full log | `commandpanel.html`, `commands.js`, `debugging.js` | Log panel only; no command box |
| Macros | `macrodlg.js` | Not designed |
| Camera | `camera.js` | Decide whether to keep it |
| System status (`[ESP420]`), restart | `statusdlg.js`, `restartdlg.js` | Settings › Device |
| Languages | `translate.js`, `language/` | English only. Decide whether to keep translations |
| Belt motor test, diagnostics | `maslow.js`, `tablet.js` | Settings › Advanced, not designed |

**3. Build and ship:**

- Gzip the Vite build into `index.html.gz` for `firmware/FluidNC/data/`.
- Add the new UI to the CI build (`compile-on-review.yml` and `release.yml` run `gulp package`).
- The current UI stays the release default until part 2 is done.
- **Testing (decided):** on the test machines, the new UI replaces the UI file. It's a small team,
  and firmware changes along the way are fine.
  1. Rename `index.html.gz` to `index-old.html.gz` (flash has room).
  2. Upload the new UI as `index.html.gz`.
  3. To go back, rename the new one out of the way first (`index-new.html.gz`), then
     `index-old.html.gz` back to `index.html.gz`; a rename onto a taken name may fail.
  4. If the new page is too broken to use, `/?forcefallback=yes` opens FluidNC's built-in file
     manager, or the rename works as a plain address:
     `/files?path=/&action=rename&filename=index.html.gz&newname=index-new.html.gz`.

  A firmware update leaves these files alone; uploading a filesystem image (`--fs`) replaces both.
  A second page isn't possible without a firmware change (the board downloads any file other than
  `index.html`), and isn't needed for now.

**4. New actions in `machine.js`** (shared with the current UI; each with a bun test; who writes
them is to agree with Tristan). No firmware change needed:

| Action | Sends |
|---|---|
| Set home XY, Set Z | `G10 L20 P0 X0 Y0`, `G10 L20 P0 Z0` |
| Park | a `moveTo()` to the park position, or its own action |
| Clear error | `$X` |
| Check for a tool, remove the tool, re-home Z | `$Spindle/Home`, `$Spindle/RemoveTool`, finished from Tristan's new tool field |
| Reboot | `[ESP444]RESTART` (HTTP), if it belongs in `machine.js` |

**5. In the firmware** (status report changes are reviewed by Tristan or BarbourSmith, and a field
sent only while it applies, like `STOPPED`, is preferred):

| Need | For | Who |
|---|---|---|
| Tool state and spindle-board state (Z homing, needs homing, recovering from a fault) | The Mini's tool steps and spindle fault screens; refusing jobs while Z can't move (issue 23) | mo |
| Which machine this is (Maslow Mini or 4.1) | Picking the setup flow | To decide |
| Find anchors progress, e.g. point n of total | The 11-minute Find anchors step | Tristan |
| Why an operation failed, and the alarm reason | The message in the top bar and the Error screen | Tristan |
| Done extending | Belts pay out until the user says they're attached | Tristan |
| Set Z from the measured stick-out | The Mini's Measure the tool | mo |

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
- **Stock thickness timing:** Apply tension uses it, but it's entered later, at Secure the work
  piece. Does a new value need the position recomputed, or should it be asked earlier?
- **Renaming the machine** (hostname and hotspot name): does it need a restart, and how does the
  page find the machine once `maslow.local` changes?
- **Settings:** which take effect at once, which need a restart, and which to lock during a job.
- **Size budget:** is 300 KB gzipped right, with three.js under consideration?
- **Languages and browsers:** keep the translations? What's the oldest phone we support?
- **Upstream ESP3D-WebUI:** keep any connection, or treat this as our own code?
- **Not in the map yet:** recovering after a power loss, saved anchor setups for each table, and a
  job history.
