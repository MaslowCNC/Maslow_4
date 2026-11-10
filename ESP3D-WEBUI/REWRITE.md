# WebUI rewrite: goals and design principles

This is a working draft. It records why we are rewriting the WebUI and the rules the new code
should follow. Change it as decisions are made. The "Open questions" section at the end lists
what is still undecided.

## Where we are starting from

- The UI is plain HTML, CSS and JavaScript in `www/`: `index.html`, about 30 HTML fragments in
  `www/sub/`, and about 55 script files in `www/js/` (about 16,800 lines). The scripts share
  global variables and update the page directly.
- Gulp (`gulpfile.js`) inlines everything, minifies it and gzips it into one file,
  `index.html.gz` (about 145 KB). That file is copied to `firmware/FluidNC/data/`.
- FluidNC serves the file from the XY board's flash. The flash filesystem is 2 MB, and about
  300 KB of it was in use on the tested board.
- Once loaded, the page talks to the board over HTTP (commands, files, firmware upload) and a
  WebSocket (`ws://<host>/ws`, or the port that `[ESP800]` reports) for live status and
  messages.
- `fluidnc-web-sim.py` stands in for the board during development. `www/js/tests/` has a few
  Bun tests.

## Goals

1. **Easier to change.** Each part of the screen should be a component with clear inputs. It
   should not reach into global variables or other panels' DOM. A new feature, like the
   spindle panel, should mean adding a component, not editing ten files.
2. **One source of truth for machine state.** Position, machine state, alarms, spindle state
   and settings should live in one store. That store is fed by the WebSocket and HTTP
   replies, and components read from it. Nothing keeps its own copy.
3. **Works well on a phone at the machine.** People run the machine from a phone, often on
   the board's own hotspot. Controls need to be usable by touch, readable at arm's length, and
   fast to load on a slow link.
4. **Safer to use.** Actions that move the machine or stop a job should be hard to trigger by
   accident and always clear about what they will do. Stop must always be reachable.
5. **Testable without a machine.** The simulator and automated tests should cover the main
   flows: connect, jog, run a file, calibrate, handle an alarm or a spindle fault.
6. **No loss of features.** Everything the current UI does keeps working throughout the
   rewrite, including calibration, the toolpath preview, files, settings, macros, firmware
   update and the spindle controls.

## Design principles

### Fits on the board

- **Everything is bundled.** The page must work with no internet connection, so there are no
  CDN scripts, web fonts or remote images. Every dependency is built into `index.html.gz`.
- **One file, served gzipped.** The build output stays a single self-contained
  `index.html.gz`, so the firmware's web server and the update process don't change.
- **A size budget the build enforces.** The build fails if `index.html.gz` grows past the
  budget (proposed: 300 KB). Raising the budget is a deliberate decision, not something that
  happens by accident.
- **Every dependency has to justify its size.** Prefer small libraries. Check a library's
  gzipped size before adding it. Large component libraries (MUI, Ant Design) are out unless
  we decide otherwise.

### The machine is the authority

- **The UI never assumes a command worked.** It shows what the board reports, not what it
  asked for. A button press doesn't change displayed state until the board confirms it.
- **The connection can always drop.** WiFi drops, the board reboots, and firmware updates
  restart it. The UI must show when it's disconnected, reconnect by itself, and never leave
  controls looking live while the board can't hear them.
- **Other people may be driving.** Another browser, telnet or the MCP server may be connected
  at the same time. The UI must reflect state changes it didn't cause.
- **No firmware changes required.** The new UI uses the same HTTP endpoints, WebSocket messages
  and `$`/`[ESP…]` commands as today. If we want a firmware change, it's a separate decision
  that's written down and versioned.

### Safety

- **Stop is always visible and never blocked** by dialogs, loading states or
  scrolling.
- **Motion and destructive actions need intent.** Belt retract and extend, calibration,
  homing, jogging large distances, deleting files and flashing firmware need clear labels.
  Where a mistake is costly, they need a confirmation step.
- **Errors and alarms are shown, not hidden.** Alarms, spindle faults and `error:` replies are
  shown prominently and say what to do next. They don't only appear in the console.

### Code

- **Small, plain components.** Each component does one job and gets its data from props or
  the store. Keep logic out of components when it can be a plain function.
- **Keep the Maslow logic framework-free.** Calibration math, G-code parsing and the toolpath
  preview stay as plain modules with their own tests, so they outlive any framework choice.
- **Migrate piece by piece.** The new build must work alongside the existing scripts. Replace
  one panel at a time, and ship a working UI after every step. Don't do a big-bang rewrite.
- **Plain, consistent styling.** Use a small set of design tokens (colors, spacing, type
  sizes). Styles live with their components.
- **Dark only.** The UI follows the dark tool style of the Abundance design guideline: a graphite
  `#3f4243` page, slate `#232832` panels with lilac `#c4a3d5` titles and accents, `#e0e5ef` text, and
  JetBrains Mono for numbers and technical labels. There's no light theme, which keeps the UI simpler
  and smaller. One could be added later if people ask for it.
- **Accessible by default.** Use real buttons and labels, make everything keyboard-reachable,
  use readable contrast, and use touch targets of at least 44 px.
- **Pinned dependencies.** Use exact versions in `package.json`, not `latest`, so builds can
  be repeated.

## Layout

### Key panels

| Panel | What it holds | When it's used |
|---|---|---|
| Machine state | State, position, alarms, connection, **Run / Stop** | Always |
| Machine visuals | Frame, belts, sled position, toolpath preview | Always |
| Home compass | Jogging, go home, set zero | Setup and between jobs |
| G-code log / serial | Console output and command input | While cutting, and for debugging |
| Machine config | Settings, calibration, belt operations | Setup |
| File management | Upload, choose, run and delete files | Before a job |

Still to place: spindle and Z controls (the Mini's spindle board) and job progress (file, line,
time left). These could live in the machine state bar or as their own panel.

### Prompts are completed where they're asked

If the top bar asks for something, the user can complete it in the top bar. Small inputs sit beside the
instruction, such as the stick-out at Measure the tool and the stock thickness at Secure the work piece.
The final check's four boxes sit along the bottom of the top bar, beside Start. Bigger tasks open a
pop-up from the bar, such as the Work area pop-up. The Device state panel shows status and doesn't hold
step inputs. On a phone, the header plays the top bar's part: the final check sits there, and setup inputs
sit with the step in the Setup view.

### Expanding the setup bar

During setup, an arrow centered on the bottom edge of the setup bar opens a larger view of the
current step: a big graphic beside numbered, fuller instructions. It's closed by default, so the bar stays
short for people who know the steps. The big graphic is where moving line art goes, to show first-time users
how to retract, extend, attach the belts and so on. The line art itself is on hold (see
`tools/line-art-prototype/`); the mockup shows the static step graphic with a placeholder caption.

### The working site (`webui/`)

The mockup is now a working site in `webui/` (Preact and Vite, built to one self-contained `index.html`):
desktop layout 6 and phone layout B, with every button and path working. It isn't connected to a machine. A
simulated device stands behind one interface (`getState`, `subscribe`, `send`), and whenever the firmware would
report something, a docked "Simulated device" panel asks the tester what happened, including the failures found on
the machine. A firmware adapter will replace the simulator without changing the screens; `webui/src/device/commands.js`
lists the command each action will send and which ones need the planned firmware changes. See `webui/README.md`.

The screen comes from the device's state plus the user's answers on the page (`derive()` in
`webui/src/flow/screens.js`), as described in "Two states the UI has to combine".

**Two positions, both always shown.** The machine position is measured from the anchors: X 0, Y 0 is where Apply
tension leaves the sled, and Z 0 is where the bit is seated. Set home XY and Set Z don't move it; they set the work
offset, so the work position becomes 0 there. Device details and the jog panel show the work position with the
machine position under it (for example work X 0, Y 0 at machine X 10, Y 30), and the frame drawing marks the user's
home. These are FluidNC's `MPos`, `WCO` and `WPos = MPos − WCO`; the status report already sends `MPos` and, now
and then, `WCO`.

### Walkthrough mode in the mockup

The mockup has a Walkthrough control for clicking through a flow the way the device would run it. The
device screen's own buttons move it to the next state, moving steps finish on their own after a few
seconds, and a bar above the screen suggests what to press. Starting points: first boot through the
hotspot, first-time setup, returning after a reboot, running a job, Find anchors failing, an emergency
stop during a job, a spindle fault during a job, and a lost connection. The transitions live in `FLOW`,
`AUTO` and `STOP_TO` in `ui-layouts.html`, and follow the UI modes table and the map. Timings are only
there to make it feel real, not measured.

### Device details tabs

Below the status, position and work area cards, the Device details panel has three tabs:

| Tab | Shows | Source |
|---|---|---|
| Job stats (default) | During a job: progress, line, time running and left, feed. Otherwise the chosen file's size, time and depth | `?` report and the file |
| Belts | Each belt's length, live while the belts move | Needs the new firmware report. Today only `$MINFO` sends lengths |
| Motors | Each motor's current, TL, TR, BL, BR and Z | Needs the new firmware report, if it can send them. Today currents only go to the debug log and telemetry file |

Instructions, the final check and error steps stay above the tabs, so a tab never hides what the user
has to do. Values go grey with "out of date" when the device stops answering.

### Work area pop-up

Setting a new work area happens in its own pop-up, not in the Device state panel. It asks for width,
height and spoil board thickness, warns that saving clears the anchors, and has Cancel and Save. It opens
from Set new work area at the first setup step, and from Change work area in the Settings pop-up.

### Settings pop-up

Settings that aren't part of a setup step live in a pop-up, opened from a ⚙ button at the end of the top
bar (on a phone, in the header). It's a first draft in the mockup, with seven sections:

| Section | Holds |
|---|---|
| State (opens first) | The current mode, subheader, message and connection, then every device action as a button, always in the same order: Retract belts, Extend belts, Apply tension, Release tension, Find anchors, Check the tool, Continue, Run from the start, Clear error, Reboot device, Stop. The ones the device can't do right now are greyed out. A small ? beside each says, on hover, what it does and, if it's greyed out, why |
| Device | Name (hostname and hotspot name), firmware version and Update firmware, Reboot device |
| Network | WiFi network and Change network, hotspot on or off (a placeholder until the current dialog is documented) |
| Work area | Size and spoil board thickness (read only, with Change work area), the last stock thickness, saved anchors, Find anchors and Clear anchors |
| Tool and Z | Last stick-out, default bit, how far Z lifts after Stop (+2 mm, fixed in firmware) |
| Display | Dark or light panels, units |
| Advanced | Raw FluidNC and Maslow settings, such as the 300 mm belt retraction limit, and `maslow.yaml` |

While a job runs or anything moves, and while the device isn't connected, the actions that move or restart
the device (Update firmware, Reboot, Find anchors, Clear anchors) are unavailable, with a note saying why.
Still to check: which settings the firmware can change without a restart, and whether Clear anchors and
the measured date need firmware work (they do today).

### Layout rules

- **Machine state can go anywhere, but it is always in view.** It doesn't have to be at the
  top. It must never scroll off screen or be hidden behind a tab.
- **Stop is always on screen.** If it doesn't live in the state panel, it gets a
  fixed spot of their own.
- **Machine visuals get the largest area.** They're what people watch during a cut.
- **The home compass sits beside the visuals, on the right.** That's where a thumb reaches on a
  tablet, and it lets you jog while watching the sled move.
- **Setup panels (config, files) take secondary space.** They can share one area as tabs,
  because they're rarely needed together and almost never during a cut.
- **The log sits next to the visuals.** It's narrow and can collapse when you don't need it.
- **On a phone, panels stack in priority order:** visuals, compass, log, files, config. The
  state and Stop stay pinned (top or bottom of the screen).

### Desktop layout

Eight layouts were sketched and mocked up in `tools/ui-mockups/ui-layouts.html`, and **layout 6, Tall
compass,** is the one we're going with (decided 2026-11-04): large visuals on the left, the Machine state
panel over a tall compass on the right, and Machine config below the visuals with File management beside
it. Layout 8 (compass on the left) was the other one shortlisted, and has been dropped from the mockup.

The serial log sits inside the visuals as a collapsible overlay.
A setup guide runs along the top: the current setup step with a small graphic, progress
such as "Setup: 6/9 done, 3 skipped", one instruction and the step's action. Where Machine config used to
be, a Machine state panel holds the mode and subheader, the explanation, X, Y and Z, Run,
Stop, and the job stats. File management stays a panel. Outside setup, the top bar shows the mode with its
actions: in Ready to cut that's Trace boundary, Run and Stop. Trace boundary is optional, so it's an outline
button beside Run.

The mockup also has three phone layouts, each in portrait and landscape: **A** tabs, **B** a small visual
with collapsible sections, and **C** the visuals with a sheet over them. In all of them the state stays
pinned at the top and Run and Stop sit at the bottom (in landscape A, down the right side). The phone layout
hasn't been chosen yet. The mockups say "device" instead of "machine"; the rest of this file
hasn't changed its wording.

### Setup steps

Setup has nine steps, in this order:

1. Confirm the work area
2. Retract belts
3. Extend belts
4. Apply tension
5. Find anchors
6. Check the tool
7. Measure the tool
8. Secure the work piece
9. Choose a job file

**Confirming the work area comes first,** before anything moves, because it decides which steps run. If
the user keeps a work area that has saved belt lengths and anchors, setup skips Retract, Extend and Find
anchors, and goes on to Apply tension, which checks the saved anchors. A new work area should clear the
anchors, so every step runs. Current firmware doesn't clear them yet (see Later firmware changes). Find anchors also runs when Apply tension can't confirm the position. Skipped steps
show as skipped, not done.

**After a reboot, Apply tension always runs.** At boot the firmware rebuilds the belt lengths from the
encoders and comes back as `RETRACTED` or `EXTENDEDOUT`, never `READY_TO_CUT` (`Maslow.cpp`, the boot
restore). So even when the belt lengths are known, the user has to apply tension again. The only time
setup can skip straight to the tool check is a page reload or reconnect without a reboot, when the firmware
still reports `READY_TO_CUT`.

**Extending ends when the user says the belts are attached** (a planned firmware change). Today, Extend
finishes when the belts reach their target length. Instead, it will keep going until the user presses
**Done extending**, meaning all four belts are attached to their anchors, and then go to Belts extended.
**Stop extending** ends it early: the belt lengths stay known, so the user can extend again or retract from
where the belts are, without starting over.

**Stopping a retract loses the belt lengths.** Starting a retract resets every arm, so the lengths are
gone from the moment it starts. Stop halts the motors but doesn't change the Maslow state, so the board
may still report `RETRACTING` afterwards. The UI treats a stopped retract as Belt lengths unknown, and the
user has to retract again. What the board reports after a stopped retract hasn't been tested.

**Spoil board and stock thickness** (`Maslow_spoilboardThickness`, `Maslow_workThickness` in
`maslow.yaml`, 0 to 50 mm) raise the sled above the frame, so the firmware adds both to every anchor's
height when it works out belt lengths: on every move, in Apply tension and in Find anchors. They don't
affect Z depth. A wrong value costs accuracy, most near the anchors: 20 mm off changes a belt by about
2 mm at 1,000 mm from an anchor, and about 7 mm at 300 mm. The spoil board thickness is set with the work
area, since it rarely changes. The stock thickness is asked at Secure the work piece for every job, with
the last value filled in. Both also show in the Settings pop-up's Work area section.

**The tool and Z steps.** Once the position is confirmed, the device checks for a tool on its own: the
spindle board raises the spindle through its top-of-travel light beam. Then:

- **A bit is found:** the UI asks "Have you set Z for this tool?". Yes moves on to Secure the work piece.
  No leads to Measure the tool.
- **No bit is found:** the user can measure a tool, which leads to Measure the tool, or continue without
  a tool.
- **Measure the tool:** press **Remove tool**, which raises the spindle until the bit clears the beam
  (`$Spindle/RemoveTool`), and take the bit out. Measure how far it sticks out of the spindle socket (from
  the socket face to the tip) and enter that number. Then load the bit back in at that stick-out. There's
  no "loaded" button: the device detects the bit when it breaks the beam, and sets Z from the measurement.

The spindle board already has the beam and two commands that use it: `$Spindle/Home` (raise until the
beam breaks) and `$Spindle/RemoveTool` (raise until the tool clears the beam). A check that reports
whether a tool is present, and setting Z from a typed-in stick-out, are new firmware work.

The tip "Belts too tight? Release tension" belongs to Secure
the work piece: Release tension, secure the work piece, then Apply tension again. Releasing keeps the belt
lengths, so nothing else needs to run again.

**Ready to cut only shows once all nine steps are done or skipped:** Z is set, the work piece is secure
and a file is chosen. **Jogging unlocks as soon as the anchors and position are confirmed** (steps 6 to 9),
and stays locked while anything moves, while the belts are slack, and in Error.

This needs something the firmware can't tell us yet: whether the saved anchors belong to this work area
and were actually measured. Saved anchors always have values (see Setup checks in the workflow map), so
the UI can't decide on its own whether Find anchors can be skipped. Either the firmware records that Find
anchors finished, and clears it when the work area changes, or the UI stores that itself. See Later
firmware changes.

## Machine status

These notes come from reading the UI and the firmware code on `Maslow-Mini` at `3448d81f`, unless
they say otherwise. Machine tests so far ran on older firmware, so anything marked as tested needs
rechecking.

### Two states the UI has to combine

The board reports two separate states, and the UI needs both:

- **FluidNC machine state** (Idle, Run, Jog, Homing, Hold, Alarm). The status report writes Homing as `Home`, and Hold as `Hold:1` while slowing down and `Hold:0` once stopped: what the machine is doing right
  now. It comes from the `?` status report.
- **Maslow calibration state** (`UNKNOWN`, `READY_TO_CUT` and so on): how far through setup the
  machine is. It's the firmware's `currentState`. The board sends it as a
  `[MSG:INFO: Current state: N]` message after each state change request, and `$GSTATE` asks for it.

Idle alone doesn't mean ready: a machine in `UNKNOWN` and one in `READY_TO_CUT` both show Idle.

### UI modes

The UI has four main modes: **Setup**, **Ready to cut**, **Job** and **Error**. Job covers
everything from Start until the job ends. Anything else that isn't Error or Ready to cut falls under
Setup. Each mode can have a subheader for the process that's actually happening.

| Maslow state | No. | FluidNC state | UI mode | Subheader | Next action | Trigger |
|---|---|---|---|---|---|---|
| `UNKNOWN` | 0 | Alarm after a reset, otherwise Idle | Setup | Belt lengths unknown | Retract | Startup with no usable saved belt positions, Release tension finishing from this state, or a stopped retract (see below) |
| `RETRACTING` | 1 | Homing | Setup | Retracting belts | Wait, or Stop. Stopping loses the belt lengths | User presses Retract (`$ALL`) |
| `RETRACTED` | 2 | Idle | Setup | Belts retracted | Check each arm by eye, then press Extend belts. Extending never starts on its own | Belts finish retracting, or startup with saved belts at zero. Can be false (see below) |
| `EXTENDING` | 3 | Homing | Setup | Extending belts | Pull out the belts and attach them to the anchors, then Done extending, or Stop extending | User presses Extend (`$EXT`), or Extend again after a stop |
| Not decided (planned firmware change) | | Idle | Setup | Extending stopped | Extend again, or Retract from where the belts are | User presses Stop extending. The belt lengths stay known |
| `EXTENDEDOUT` | 4 | Idle | Setup | Belts extended | Apply tension or calibrate (see below) | User presses Done extending (planned; today, belts finish extending), Release tension finishes, Apply tension or calibration fails, or startup with saved belts extended |
| `TAKING_SLACK` | 5 | Homing | Setup | Applying tension | Wait, or Stop | User presses Apply tension (`$TKSLK`) |
| `TAKING_SLACK` | 5 | Idle, stuck, on firmware before `20493b1b` | Setup | Couldn't confirm position | Retract, as Find anchors is refused from here | Apply tension ends with no result |
| `CALIBRATION_IN_PROGRESS` | 6 | Homing | Setup | Finding anchor points | Wait, or Stop | User presses Find anchors (`$CAL`) |
| `READY_TO_CUT` | 7 | Idle, Z moving (planned firmware change) | Setup | Checking for a tool | Wait, or Stop | Apply tension or calibration finishes, a page loads, the tool changes, the user re-homes Z, or spindle recovery finishes |
| `READY_TO_CUT` | 7 | Idle, a tool was found | Setup | Tool found | Say whether Z is set for this tool | The tool check finds a bit |
| `READY_TO_CUT` | 7 | Idle, no tool found | Setup | No tool loaded | Measure a tool, or continue without one | The tool check reaches the top of travel |
| `READY_TO_CUT` | 7 | Idle, then Z moving for Remove tool | Setup | Remove the tool, Removing the tool, Measure the tool | Press Remove tool, take the bit out, enter its stick-out | Z isn't set for this tool, or the user chose to measure one |
| `READY_TO_CUT` | 7 | Idle, waiting for the beam (planned firmware change) | Setup | Load the tool | Load the bit back in; the device detects it | User saves the measurement |
| `READY_TO_CUT` | 7 | Idle, Z moving (planned firmware change) | Setup | Setting Z | Wait | User loads the measured tool |
| `READY_TO_CUT` | 7 | Idle, Z set, work piece not confirmed | Setup | Secure the work piece | Confirm it's secure, Release tension, jog, or Stop | User sets Z |
| `READY_TO_CUT` | 7 | Idle, work piece confirmed, no file chosen | Setup | Choose a job file | Upload or choose a file, jog, or Stop | User confirms the work piece is secure |
| `READY_TO_CUT` | 7 | Idle, all setup steps done | Ready to cut | None | Trace boundary (optional), Run a job, jog, or Park | User chooses a file, the Job stopped message clears, or Park finishes |
| `READY_TO_CUT` | 7 | Idle, after Stop | Ready to cut | Job stopped (message) | Run again, choose a file, or jog | User presses Stop during a job, shown by the stop message |
| `READY_TO_CUT` | 7 | Jog | Ready to cut | Moving | Stop | User jogs |
| `READY_TO_CUT` | 7 | Run with no `SD:`, no job finishing and no Park sent by this browser | Ready to cut | Moving | Stop | Manual gcode is sent, or user presses Trace boundary (the mockup's subheader is Tracing the boundary) |
| `READY_TO_CUT` | 7 | Run, after this browser sent Park | Ready to cut | Parking | Stop | Job succeeds, or user presses Park |
| `READY_TO_CUT` | 7 | Run, with `SD:` | Job | Cutting | Stop | User presses START |
| `READY_TO_CUT` | 7 | Hold or Idle, during a spindle fault | Job | Spindle fault: recovering | Stop stays visible, no Run | Spindle board reports a fault |
| `READY_TO_CUT` | 7 | Idle, after a finished job | Job | Success | Park starts (shows as Parking), then Ready to cut | `SD: <name>: Sent`, then Run returns to Idle with no Alarm (see below) |
| `RELEASE_TENSION` | 8 | Homing | Setup | Releasing tension | Wait, or Stop | User presses Release tension (`$CMP`) |
| `CALIBRATION_COMPUTING` | 9 | Not checked | Setup | Finding anchor points | None: nothing requests this state any more | None: nothing requests this state any more |
| `EXTENDEDOUT` | 4 | Idle, after Find anchors fails | Error | Can't find anchors | Check the belts, then Find anchors again | Calibration fails |
| Any | | Alarm | Error | The reason, if the board gives one | Depends on the fault, often `$X` | Emergency stop, panic, stalled update loop, or a FluidNC alarm |
| Any, before the first step | | Any | Setup | Confirm the work area | Keep the work area, or set a new one | A page loads or reconnects and setup isn't complete |
| Not known yet | | Not known yet | Connecting | None | Wait | A page loads or reconnects |
| Not known | | Not known | Connecting | WiFi setup (placeholder) | Name the device, then join a network or turn the hotspot off | The page is opened through the Maslow hotspot, as on a first boot with no WiFi set. The current dialog isn't documented yet |
| Not known | | Not known | Error | Not responding | Reboot from the UI if the board's web server answers, otherwise power cycle | The board stops answering, or doesn't answer in time while Connecting |

Subheader wording is a first draft. The FluidNC state column is the usual one for each row. Jogging
while in Setup hasn't been traced.

The UI only shows Ready to cut once the user has set Z, confirmed the work piece is secure and chosen a
file, even if the firmware already reports `READY_TO_CUT`. The firmware state only covers belts and
anchors. So the UI mode comes from the Maslow state plus the user's answers, and the firmware state still
decides what the machine can do. These last Setup steps are the one Setup case where the machine can move,
so they have to allow jogging, Z moves and Stop.

| Event | Run the tool check again? |
|---|---|
| Power cycle or new page load | Yes |
| Tool removed or loaded | Yes |
| Z re-homed by the user | Yes |
| Spindle fault recovery | Yes, to confirm the bit is still OK |
| Job finished | No |
| Job stopped | No |
| Tension released and applied again | No |

Z is set from the bit's measured stick-out. Changing the stock doesn't affect it, because the machine sits on
top of the stock, so Z can be set before securing the work piece.

After the tool and Z, the UI asks "Is your work piece set up and secure?" instead of assuming it isn't,
because the user may have calibrated with the machine on top of a large enough work piece. If the
answer is no, the securing step shows a suggested prompt rather than asking another question: if your
belts are too tight, release tension.

The machine can't store these answers today, so they live in the browser. A second browser or a
reloaded page asks again, which is safe. A small firmware value could fix that later.

Moving covers jogging and manual gcode, including moves where only Z changes, and Parking is
separate. Z homing by the spindle board doesn't appear to count, because the XY board stays Idle
during it. Jogging reports Jog, so it's easy to
detect. A feed hold cancels a jog instead of pausing it, so the machine doesn't enter Hold. Park is two normal gcode moves sent by the UI, and
it's only offered at `READY_TO_CUT`, so it never appears in Setup. It isn't a firmware state, so the
machine should report Run, the same as cutting. The `SD:` field in the status report separates them:
a file job has it, and manual moves don't. After the file has been read the field becomes `SD: <name>: Sent`,
and moves may still be running, so the UI stays in Job until Run returns to Idle. Only the browser that sent
Park can show Parking. Another browser can't tell Park from other manual gcode, so it shows Moving.
A firmware Park command could report Parking to every browser later.

A job success automatically triggers Park. Stop, spindle faults and errors don't. Today Park is only a
button, so this is new work, and these points need handling:

- **Who sends it:** if the UI sends Park, every connected browser could send it, and none will if no
  browser is open. Having the firmware start Park when a job finishes would fix both.
- **The spindle:** Park only sends Z and XY moves, so it doesn't turn the spindle off. It depends on
  the job's gcode having done that, so check this before moving.

Stopped is a message on Ready to cut, not a mode. After Stop, the job is over and the machine really
is ready to cut, but the user should see that the job ended early, the spindle is off and Z may
have lifted. The message clears when they dismiss it or start something else.

The states alone can't show how a job ended: stopped, finished and never started all look like Idle +
`READY_TO_CUT`. Current firmware says less about this than older versions did:

| Event | What the firmware reports |
|---|---|
| Paused | `Paused file job at line: N (X% complete) - Last motion command: ...` |
| File read completely | `SD: <name>: Sent` in the status report while the job is active, and a debug-level `<name> job sent` log line |
| Stopped | No message found. The job is aborted silently |
| File or G-code error | An error log line naming the file and line |

- **Older firmware was different.** `file job succeeded` and `Reset during file job` no longer exist on
  current `Maslow-Mini`, so don't build on them.
- **Finished jobs:** `Sent` means the last line has been read, not that cutting has ended, because
  moves can still be queued. The current UI's `SD` parser also misreads this form, because it expects a
  percentage. For now, Success can be inferred from `Sent` and then Run returning to Idle with no Alarm.
- **Stopped jobs:** only the browser that pressed Stop knows. Another browser can see that `SD:` went
  away, but can't tell why.
- **The job result is needed from the firmware.** A last-job result (finished, stopped or failed), set once
  motion has really ended, is the only reliable way to show Success and Job stopped in every browser
  and after a reload. Until then, treat these subheaders as best effort.
- **Continuing after Stop (decided, needs firmware):** there's no Pause. Stop is the only way to halt a
  job, and it should save the job's progress, the file and the last line, so Job stopped can offer
  **Continue from line N** as well as **Run from the start**. Today Stop clears the job, so it can't
  continue. Continuing from a line needs the right Z and spindle state first, so the firmware should
  check those before it carries on.

Connecting is what the UI shows when a page first loads, or after it reconnects, until it has both the
FluidNC and Maslow states. It's a short wait, not a fifth working mode, and no machine actions are
offered. If the board doesn't answer in time, the UI moves to Not responding. The time limit
is still to be decided.

Every connection, first or repeat, requests the full state and passes through Connecting. The firmware
only announces the Maslow state when it changes, so a change made while disconnected or during a
restart is otherwise missed, and the page shows a stale state until it's reloaded.

Not responding is different from the other errors, because the board can't report anything. The UI
detects it itself from the connection, and it can't know either state.

The machine can also start in Alarm instead of Idle, so setup can't assume Idle is the only resting
state.

### Setup checks in the workflow map

The map's machine checks come from the Maslow state, with no firmware change:

| Maslow state | Belt lengths known? | Belts extended? | Position confirmed? |
|---|---|---|---|
| `UNKNOWN` | No | Not known | No |
| `RETRACTED` | Yes (0), but can be false | No | No |
| `EXTENDEDOUT` | Yes | Yes | No, run Apply tension |
| `READY_TO_CUT` | Yes | Yes | Yes |

The map doesn't ask "Anchor points known?" or "Belts tensioned?", because the machine can't answer them.
Saved anchors always have values, and nothing records whether calibration has run. Extended belts go to
Apply tension, which checks the saved anchors. If it can't confirm the position, the flow goes to Find
anchors. A first-time user, or someone who has moved the anchors, can go straight to Find anchors. Whether
a tool is loaded isn't covered by these states.

The setup steps (see Setup steps) want to skip Find anchors when the work area is kept, which needs the
machine, or the UI, to remember that the anchors were measured for this work area. Until one of them does,
the map routes everyone through Apply tension, which catches anchors that don't fit.

### Stop (no Pause)

**Decided: the UI has no Pause, only Stop.** Stop should save the job's progress so the user can continue
from where it stopped, which needs a firmware change (see Later firmware changes).

| | Stop |
|---|---|
| **UI sends** | `$STOP`, retried every 300 ms for up to about 10 s |
| **Belts** | Stop immediately, and queued moves are discarded |
| **Job** | Ended today. Planned: progress saved, so it can continue from the line where it stopped |
| **Spindle** | Stopped |
| **Z** | Raised to work Z +2 mm if lower |
| **State after** | Idle, shown as Ready to cut: Job stopped, with Continue and Run from the start |

- **Emergency stop isn't something the user presses.** It's triggered only by the emergency button on
  the machine or by an error the machine detects, such as a large position error or a stalled update
  loop. The firmware's `eStop()` puts the machine in Alarm and blocks motion until `$X` or a restart. The
  UI has no emergency stop button and never sends `$ESTOP`; its Stop button is the normal Stop above.
- **Why no Pause:** feed hold (`!`) didn't work during setup steps, which run as Homing; in Hold the main
  loop reads no new commands; the spindle seemed to keep running; and the current button switching
  between Start, Pause and Resume was found unclear in the meeting. One Stop that saves progress is
  simpler.
- **A spindle fault still causes a hold inside the firmware,** then ends the job and re-homes Z. The UI
  shows it as Job: Spindle fault: recovering and offers no Run.

To test on a machine:

- How long Stop takes to halt the belts during a cut. It's a normal command, not an immediate one.
- Whether the bit drags or stalls, since the spindle is told to stop before Z lifts.
- Whether pressing Stop after an emergency stop makes the machine look ready while motion is still
  blocked.

### Spindle faults

A spindle fault has no state of its own. The machine goes from Run to Hold and back to Idle, and the
Maslow state appears to stay `READY_TO_CUT`. It deliberately avoids Alarm, which would leave the belts
running and drag the stalled tool through the work. The job is lost, as with Stop, but the machine
recovers by itself and doesn't need a power cycle.

| Step | What the firmware does | FluidNC state | UI |
|---|---|---|---|
| 1 | The spindle board reports a fault in its status line | Run | Job: Cutting |
| 2 | Triggers a feed hold, and the belts slow to a stop | Hold | Job: Spindle fault: recovering |
| 3 | Runs `$STOP`, ending the job and raising Z | Idle | Job: Spindle fault: recovering |
| 4 | Waits for the machine to settle, then re-homes Z to the top of its travel | Idle | Job: Spindle fault: recovering |
| 5 | Restores the job's work Z zero and logs "ready to cut" | Idle | Setup: Checking for a tool, with a message |

In steps 3 and 4 the board reports Idle + `READY_TO_CUT`, the same as a machine that's ready. The UI
has to stay in Job until recovery finishes, so that Run isn't offered too early. Whether the
firmware rejects a job started then hasn't been checked.

Recovery shows as a subheader under Job, not as a mode of its own. It's short, and the user only has
to wait.

| Situation | UI mode | Subheader | Actions |
|---|---|---|---|
| Recovery running | Job | Spindle fault: recovering | Stop stays visible, Run unavailable |
| Recovery succeeded | Setup | Job stopped: spindle fault, then the tool check | Check the bit, then check for a tool again |
| Recovery failed or timed out | Error | Spindle recovery failed | Clear with `$X`, then re-home Z (to be tested) |
| Work Z couldn't be restored | Undecided: Error, or Ready to cut with a blocking warning | Set Z zero again before cutting | Set Z |

Until the firmware reports recovery directly (see the note below), the UI follows these log messages.
They should go on the list the FluidNC cleanup leaves alone. The UI also needs a fallback so that it
can't get stuck in "recovering" if the final message is lost.

| Meaning | Message contains |
|---|---|
| Started | `job paused because the spindle stopped` |
| Finished | `work Z zero restored` |
| Finished, but Z needs setting | `the work Z zero could not be restored` |
| Failed | `spindle fault recovery timed out`, or `not recovering from the spindle fault automatically` |

The firmware's failure messages tell the user to press Stop and re-home Z. The UI should show its own
wording and use `$X` instead, because Stop can hide an alarm by setting Idle.

**To test on current firmware:** whether `$X` recovers from each failed recovery case. It's a no-op unless
the machine is in Alarm, so it may not help after a timeout. If it doesn't work, fall back to a power
cycle.

The board also reports a fault code, which the XY board logs as a number. The UI should show the reason
in plain language. Code 1 is a driver hardware fault. The other codes still need to be listed.

### Position errors and emergency stop

A large belt position error during a job triggers the firmware's real emergency stop. There's no
automatic recovery: the job is lost, and motion stays blocked until `$X` or a restart. The check only runs while
the safety check is on, and `$SFOFF` turns it off.

| Belt position error | FluidNC state | Result |
|---|---|---|
| Over 1 mm | Run or Jog | Nothing visible, because the warning log is commented out |
| Over 15 mm | Run | Logs a warning each time it's checked |
| Over 15 mm, more than 5 checks in a row | Run | Emergency stop |
| Over 15 mm | Jog | No warning or stop from this check |

The emergency stop does the following:

1. Stops the belt motors and ends the job.
2. Stops the spindle.
3. Sets an internal `error` flag that stays set until `$X` or a restart.
4. Sets the machine to Alarm and disables the steppers.
5. Logs `Position error > 15mm while running. E-Stop triggered.` It also says the machine won't respond
   until it's turned off and on, which is out of date now that `$X` clears it.
6. Blinks the red LED, and `Maslow.update()` exits early until it's cleared. Retract and Extend are
   refused with a logged reason, such as `Motion is disabled: ...`, which the UI should show.

Z isn't raised, unlike with Stop, so the bit may still be in the material. The Maslow state appears
to stay `READY_TO_CUT`, so the machine reports Alarm + `READY_TO_CUT`, and Alarm has to win in the UI.

The same `error` flag has other causes, so this Error status can also appear during setup or at
power-on:

- `$ESTOP`.
- Two calibration failures, which call `eStop()`.
- Startup hardware problems, such as a missing encoder or motor.

What the UI needs to do:

- **Say what to do next:** the job has ended, the bit should be checked, and the user should fix the cause
  and then clear the error with `$X` or a power cycle. The firmware warns them to recheck tension and
  position before cutting, so the UI should send them back through Setup.
- **Allow for a hidden error:** `$STOP` sets Idle, but nothing found so far clears the `error` flag.
  Pressing Stop after an emergency stop could report Idle + `READY_TO_CUT` while the machine still
  ignores movement. This comes from reading the code and needs testing on a machine.
- **Allow for a missed reason:** the message is only logged once, and `$MINFO` doesn't include the
  flag. A page opened or reloaded afterward sees Alarm with no reason, or a machine that looks ready if
  Stop was pressed.

### Tested on current firmware (2026-11-06)

Tested on a Maslow Mini running `Maslow-Mini-3448d81f-dirty` (FluidNC v1.26.0), the code this file
describes. The WebUI was the stock one until 13:44, then the `ui-heartbeat-ping` build (heartbeat fix,
state request on reconnect). Everything that went wrong is in `ISSUES-LOG.md` with its evidence.

| Step | What happened | Time | Fits the new flow? |
|---|---|---|---|
| Boot | Restored `RETRACTED` (state 2) with lengths near 0, but the belts were really about 680 mm out (issue 5) | | Our check by eye after Retract stays essential |
| Retract | `Belts pulled tight with offsets …`, then state 2 | 16 s | Yes |
| Extend | Paid every belt out to `extendDist` (1200 mm here, 790 mm after Find anchors) and moved to state 4 by itself, 12 to 23 mm past the target | 87 to 90 s | Needs the Done extending change |
| Apply tension, too much slack | Stopped after 10 s: `retracted 300.140mm … (limit 300.000mm). A belt may not be anchored`, then state 4. The current UI's dialog offers Continue (`$TKSLK`) | 10 s | Needs its own result: "check the belts", not Find anchors |
| Apply tension, anchors don't fit | Tightened the belts, then `[MSG:ERR: Apply Tension failed: the measured belt lengths … do not fit the anchor locations…]`, then state 4 | About 45 s | Yes, the "Position confirmed? no → Find anchors" branch. The UI must show it (issue 11) |
| Apply tension, success | `Center point deviation within 12.000mm`, then state 7 | 28 to 29 s | Yes |
| Find anchors, first try | The board reset about a minute in, coming back in state 0 (issue 12) | | The UI must handle a reset mid-step |
| Find anchors, second try | 33 points (6 starting, center, rings of 8 and 16, closing point) with re-fits at 6, 15, 31 and 33. `rms=0.441mm`, then state 7. Logged a false `saving configuration failed: 115` (issue 15). It also set `Maslow_Extend_Dist` to 790 mm | 10.7 min | Needs a progress view (issue 16) |
| Release tension | State 8, then 4 | 1.3 s | Yes |
| Home Z (`$Spindle/Home`) | `Z homed: top-of-travel beam interrupted, tool loaded`, Z reset to 0 | Under 1 s | Yes, this is the tool check |
| Remove Tool (`$Spindle/RemoveTool`) | Raised Z to eject the bit, `Tool removed: beam clear for 500ms`, Z reset to 0 | 14 s | Yes |
| Loading the bit | Inserting it broke the beam, the board lowered Z until clear for 400 ms: `Tool loaded: beam clear for 400ms`, Z reset to 0 | 13 s | Yes, the automatic detection |
| Job start | Spindle over-current about 1 s into spin-up, before the bit reached the work (issue 17). Recovery: job ended, Z re-homed, `work Z zero restored (-9.000 mm) - ready to cut` | 8 s recovery | Yes for the recovery. The belts kept moving 3.4 s after the stop (issue 18) |
| Power cycle from Ready to cut | Came back as state 4, lengths within 0.02 mm. The page reconnected and showed Belts extended by itself | | Yes: a reboot never comes back tensioned |
| Apply tension after the reboot | Straight back to state 7 | 28 s | Yes |

**Not tested yet:** Stop in the middle of a step, Stop during a job, a finished job (Success and Park),
the emergency stop, and a full cut. See "Still to test" in `ISSUES-LOG.md`.

### Where the firmware and the UI meet

What each part of the new flow needs from the firmware, and what it has today on `3448d81f`:

| UI state or action | UI sends | UI learns the result from | Today | Firmware change needed |
|---|---|---|---|---|
| Connected, Not responding | (heartbeat) | `PING` every 10 s | Board sends bare `PING`, old page wanted `PING:<id>` (issue 2) | Fixed in the page; also send a format both understand |
| Connecting, page ID | | `CURRENT_ID` on connect | Commands before it fail with HTTP 500 (issue 14) | None, the UI must wait |
| Any Setup or Ready state | `$GSTATE`, `$MINFO` | `Current state: N`, `MINFO` JSON | Log text and on request only | A pushed status message with the Maslow state |
| Retract | `$ALL` | State 1, then 2 | Works | Stop a false retract under tension |
| Extend | `$EXT` | State 3, then 4 when every belt reaches `extendDist` | Ends by itself | Done extending command, keep paying out until then; Stop keeps lengths |
| Apply tension | `$TKSLK` | State 4 or 7, plus a `WARN` or `ERR` line for the reason | The reason is only log text | Report the result and the reason |
| Find anchors | `$CAL` | `Measured waypoint N`, fit lines, then state 7 | No total, false save error | Report point N of total; treat `Deferred` as saved |
| Release tension | `$CMP` | State 8, then 4 | Works | |
| Check for a tool | `$Spindle/Home` | Spindle board log: `tool loaded` or `no tool loaded` | Works, as log text | Tool state in the XY status report |
| Remove the tool | `$Spindle/RemoveTool` | `Tool removed: beam clear for 500ms` | Works, as log text | Same |
| Load the tool | (none, the user inserts the bit) | `Tool loading…`, then `Tool loaded…` | Works, as log text | Same |
| Set Z from stick-out | (new) | | Not built. The board seats the bit and sets Z to 0 itself, so how stick-out maps to cutting Z needs deciding | New command |
| Run | `$SD/Run=<file>` | `SD:` in the status report | `SD:` percent is bytes read, not progress (issue 19) | Report the line being run, or real progress |
| Stop | `$STOP` | State back to Idle | Belts kept moving 3.4 s after a stop (issue 18) | Halt queued motion at once; save progress |
| Spindle fault | | Spindle board log lines, then `ready to cut` | Recovery works | Report the recovery state |
| Device details: Belts | `$MINFO` | Belt lengths | Only on request | Push belt lengths live |
| Device details: Motors | | Debug log only | No live view | Push motor currents, if possible |

### Tested on the machine

Tested on a Maslow Mini with firmware `Maslow-Mini-59a7c2a3-dirty`, unless a row says otherwise. That
firmware is 1,344 commits behind `Maslow-Mini` as of commit `3448d81f`, which includes the FluidNC
v3.9.9 port, so these results need rechecking on current firmware.

| UI mode: subheader | Machine reported | Result |
|---|---|---|
| Setup: Belt lengths unknown | Alarm + `UNKNOWN` after a reset | Confirmed. Retract clears the Alarm |
| Setup: Retracting belts | Home + `RETRACTING` | Confirmed, about 20 s |
| Setup: Belts retracted | Idle + `RETRACTED` | Confirmed, but it can be false |
| Setup: Extending belts | Home + `EXTENDING` | Confirmed. Waits for the user with no time limit |
| Setup: Belts extended | Idle + `EXTENDEDOUT` | Confirmed |
| Setup: Applying tension | Home + `TAKING_SLACK` | Confirmed, 28 to 33 s |
| Setup: Releasing tension | Home + `RELEASE_TENSION` | Confirmed, about 1.3 s, then `EXTENDEDOUT` |
| Ready to cut | Idle + `READY_TO_CUT` | Confirmed |
| Ready to cut: Moving | Jog | Confirmed for jogging. Manual gcode not tested |
| Job: Spindle fault: recovering | Hold, then Idle | Confirmed from earlier logs, about 4 s |
| Error | Alarm | Confirmed for a stalled update loop |

Not tested yet: Finding anchor points, Parking, Job stopped by Stop, Cutting, Success, an
emergency stop from a position error, Stop during a Setup step, and Connecting by eye.

What came back after a restart depended on what was saved:

| Saved state at restart | Came back as |
|---|---|
| `RETRACTED` | Idle + `RETRACTED` |
| `EXTENDEDOUT` | Idle + `EXTENDEDOUT` |
| `EXTENDEDOUT`, with belts about 20 mm past their target | Alarm + `UNKNOWN` |
| Spindle just started, board reset by itself | Alarm + `UNKNOWN` |

Other findings:

- **Anchor points survive a restart.** They're stored in `maslow.yaml`, so `UNKNOWN` only means the
  belt lengths are unknown. Going through Retract, Extend and Apply tension doesn't recalibrate.
- **Nothing records that calibration has run.** The firmware has built-in anchor values, so a saved
  value can't be told apart from a default one.
- **Apply tension checks the saved anchors.** Up to 12 mm it reports the position as accurate. From
  12 to 25 mm it warns and still reaches `READY_TO_CUT`, so the UI must show that warning. Over 25 mm
  it reports an error and goes back to `EXTENDEDOUT` (the last two are from the code, not tested).
- **Position freezes during Setup steps.** `MPos` doesn't change during Home, then jumps at the end,
  so the visualizer shouldn't treat it as live.
- **`homed` doesn't mean belt lengths are known.** It seems to mean retracted since boot. Use the
  Maslow state instead.
- **Grey buttons aren't disabled.** The current UI only changes the color, and Retract ran while
  grey. The rewrite needs truly disabled buttons, and a way out of every state.
- **A watchdog stop leaves the board answering.** It reports Alarm but blocks motion, so the Not responding
  check doesn't fire. On the tested firmware only a power cycle cleared it. Current code also accepts
  `$X`. The UI has to say what's needed.

### When a Setup step goes wrong

**Apply tension must not hang.** On the tested firmware, with the anchors physically moved, Apply tension
ended at Idle + `TAKING_SLACK` with no result, and it stayed there. This is fixed upstream by `20493b1b`
(in `Maslow-Mini` since 2026-10-26, and not on the tested machine). It logs an error and goes back to
`EXTENDEDOUT`, so the user can run Find anchors directly. The UI should show:

> **Couldn't confirm the machine's position**
> The anchor points may have moved. Run Find anchors to measure them again.

No retract is needed. The belts are already out and the machine still knows their lengths, and retracting
under tension gives a false zero. On older firmware, the UI can detect Idle + `TAKING_SLACK`, but Find
anchors is refused from there.

On current `Maslow-Mini` (`3448d81f`), every way Apply tension can end has a result (read from
`takeSlackFunc` in `Calibration.cpp`, not tested on a machine):

| What happens | Result | Log |
|---|---|---|
| A belt winds in more than 300 mm, so it's probably not attached (`8088b9602`, on by default) | Belts extended | Warning: `Maslow Apply Tension retraction warning` |
| The measured belt lengths don't fit the anchors (`20493b1b`) | Belts extended | Error: `Apply Tension failed: the measured belt lengths … do not fit` |
| No Maslow kinematics | Belts extended | Error: `Apply Tension failed: no Maslow kinematics` |
| Position off by more than 25 mm | Belts extended | Error: `Center point deviation over 25mm` |
| Off by 12 to 25 mm | Ready to cut | Warning: `Maslow Apply Tension deviation warning` |
| Within 12 mm | Ready to cut | `Center point deviation within 12mm` |

So the UI can tell a failed Apply tension from a passed one by the Maslow state, and the reason from the
log line. Each failure should get its own message: the 300 mm case means "check the belts are attached",
and the others mean "run Find anchors".

**One case can still leave a stuck state.** If the measurements disagree by more than 2.5 mm nine times
in a row, the measurement gives up and sets the Maslow state to `EXTENDEDOUT`, with an error that wrongly
says it's stopping Find Anchors. But Apply tension's own flag (`takeSlack`) isn't cleared, so the FluidNC
state can stay in Homing instead of going back to Idle. The UI should treat `EXTENDEDOUT` plus Homing with
no motion as a failed Apply tension, and offer Stop to clear it. Not tested.

**Retract can report success with the belts still out.** Retract stops when a belt feels resistance.
Under tension it finished in under 2 s and set the lengths to zero, with belts about 680 mm out. Then
every length is wrong, and the check by eye after retracting is essential. A retract that finishes
within seconds should raise a warning. Small offsets don't prove anything, because after a false zero
they're measured from that wrong zero.

### Later firmware changes

These are separate from the UI work. Each is a small decision to write down and version, as described
under "The machine is the authority". None of them have been made yet. Draft PR #1129 needs merging
with current `Maslow-Mini` and retesting, because `WSChannel.cpp` has been rewritten since it was based.

Fixes:

- **Apply tension hang:** fixed upstream in `20493b1b`; every result path now ends in `READY_TO_CUT` or
  `EXTENDEDOUT`. Still to fix: when the measurement gives up after nine tries, clear `takeSlack` so the
  FluidNC state goes back to Idle, and fix the message, which says Find Anchors during Apply tension.
- **False retract:** stop a retract under tension from reporting success and setting the lengths to
  zero.
- **To recheck on current firmware:** the items below were found on older firmware and may have
  changed, as may frozen position during Setup (`6e36cd3e`) and motion while an error is latched
  (`d48d5941`).
- **Partial belt restore:** at startup, TL restores and the other three belts don't.
- **Unexplained reset:** the board reset about half a second after the spindle started, at 14:03 on
  2026-10-30. Check the power supply and connections as well as the code.
- **Update loop stall:** pressing Stop in Alarm started a Z raise and stalled the update loop for 2.7 s.
  Stop shouldn't command Z motion in that state.
- **`ovf`:** the waypoint coordinate message prints `ovf` for a number.
- **`PING`:** the raw-byte bug was found on older firmware. `sendPing` has been rewritten, and the board
  can also answer `PING:60000:60000`, so recheck this and the UI's page ID handling on current firmware.
- **Emergency stop message:** it still says only a power cycle works, though `$X` now clears it.

Reporting, so that the UI doesn't have to rely on log text or lose information on a reload:

- **Machine errors:** report the `error` and `watchdogFired` flags and the reason.
- **Spindle fault recovery:** report `_recovery`, and check that jobs are rejected during recovery.
- **Job result (needed):** report the last job as finished, stopped or failed, set once motion has really
  ended. Current firmware logs nothing for a stopped job.
- **Calibration (needed for skipping setup steps):** save that Find anchors succeeded, with the date and
  the work area it was measured for, so the UI knows when it can skip Find anchors.
- **Tool and Z confirmation:** a value on the board, so a second browser or a reload doesn't ask again.
- **Tool check (decided):** a command that raises the spindle through the top-of-travel beam and reports
  whether a tool is present, built on what `$Spindle/Home` and `$Spindle/RemoveTool` already do.
- **Z from stick-out (decided):** a command that takes the measured stick-out in mm and sets Z from it.
- **Detect a loaded tool (decided):** after a measurement, watch for the bit breaking the beam, report it,
  and set Z from the stick-out without the user pressing anything.
- **Z homing:** report it, because the XY board stays Idle during it.
- **`$MSTATUS`:** an optional single JSON status message, pushed on a timer and on state changes.

Behavior:

- **Extend ends on the user's confirmation (decided):** keep `EXTENDING` running until the UI sends
  Done extending, then go to `EXTENDEDOUT`, instead of ending when the belts reach their target. Stop
  extending should keep the belt lengths and leave a state from which both Extend and Retract are
  allowed. Pick that state, and the command for Done.
- **Device name:** naming the machine lives in the Settings pop-up (Device section), not in Device
  details. The hotspot setup, on a first boot or whenever WiFi isn't set, should prompt for it. The firmware already has the settings: `Hostname` (`$ESP112`, `NetSettings.cpp`), which
  gives the `<name>.local` address, and the hotspot name `AP/SSID` (`$ESP105`, `WifiConfig.cpp`). Both
  default to `maslow`. Hostnames accept letters, digits and `-` only, so the UI should check the name as
  it's typed. Still to check: whether a new name needs a restart, and how the UI finds the machine
  afterwards, since `maslow.local` stops working. A machine that's already on WiFi never sees this
  screen, so renaming should also be possible later.
- **Stop saves progress (decided):** on Stop, keep the file and the last line run, report them, and accept
  a command to continue from that line once Z and the spindle are back where they need to be. The UI has
  no Pause, so this is what makes stopping a job recoverable.
- **Park:** start it from the firmware when a job succeeds, and make sure the spindle is off first.
- **Work area:** 0 now means auto, and a set value limits the calibration grid (`bb0ebf46`). Changing it
  still doesn't clear the anchors. The setup steps assume a new work area does clear them, so every step
  runs; decide whether the firmware does that, or the UI prompts the user.
- **Failure messages:** the spindle recovery messages tell the user to press Stop, which can hide an
  alarm. Keep the phrases the UI matches if these are reworded.
- **`$SLP`:** remove or block it, since the UI doesn't use Sleep.

To check for the Settings pop-up:

- **Restarts:** which settings take effect straight away and which need a restart, starting with the
  hostname (`$ESP112`), the hotspot name (`$ESP105`) and the WiFi settings. The pop-up should say when a
  change needs a restart, and offer one.
- **Finding the device after a rename:** `<old name>.local` stops answering. Check how long mDNS takes to
  pick up the new name, and whether the page can move itself to the new address.
- **Clear anchors:** there's no command for it today. Decide what clearing means (built-in defaults, or
  a flag that marks the anchors as not measured) and add a command.
- **Anchors' measured date:** nothing records when Find anchors last succeeded, or for which work area.
  This is the same change as the Calibration item under Reporting.
- **Network options:** what the current WiFi dialog and settings offer (station, hotspot, both, hotspot
  only when WiFi fails), so the Network section offers what the firmware supports.
- **Settings during a job:** which settings the firmware refuses, or shouldn't accept, while a job runs or
  the belts move, so the UI locks the same ones.
- **Units and defaults:** whether units and the default bit are firmware settings or should live in
  the UI.
- **Stock thickness timing:** Apply tension uses both thicknesses to check the position and set the belt
  lengths. If the user enters a new stock thickness at Secure the work piece, after Apply tension, check
  whether the position needs recomputing, or whether the stock should be entered before Apply tension.

### Getting status to the UI

| Data | Source | Today |
|---|---|---|
| Machine state, position, feed, spindle speed | `?` report, or pushed with `$Report/Interval=<ms>` | Works. Position is commanded, not measured |
| Maslow state | `Current state` messages, `$GSTATE` | Works, but it's log text |
| Homed, extended, belt lengths and errors | `$MINFO` | Sent, but the UI only keeps `homed`, `extended` and `state` |
| Connection | WebSocket state, plus a ping every 10 s | Needs an indicator |
| Motor currents | Debug log, telemetry file | No live view. The Device details Motors tab is waiting on the new firmware report |
| Voltages | Not found on the XY board status | Needs firmware work |
| Spindle state, fault, load | Spindle board's `T:` line to the XY board | Only speed is in `?`. The rest isn't traced |

- **Log text can break silently.** The UI matches messages like `[MSG:INFO: Current state:`, so
  rewording a log line breaks it with no error. The FluidNC cleanup should leave these alone,
  including the paused message.
- **Optional later cleanup:** a `$MSTATUS` command that pushes one JSON message on a timer and
  on state changes, for example every 250 ms during motion and every 1 to 2 s when idle. The UI
  should mark values as stale if updates stop. This is a firmware change, so it needs the separate
  decision described under "The machine is the authority". The store should be able to switch data
  sources, so the UI can start on today's messages.
- **Later firmware change: report the emergency stop flag.** Adding the `error` flag and its reason
  to `$MINFO` or the status report would fix both the hidden error and the missed reason described
  under "Position errors and emergency stop".
- **Later firmware change: report spindle fault recovery.** After a spindle fault, the board
  reports Idle + `READY_TO_CUT` while Z is still re-homing, so it looks ready to cut. For now the
  UI can only follow the log messages, which a page opened or reloaded during recovery misses. The
  firmware already tracks this in `_recovery` in `SpindleBoard` (None, WaitForHold, WaitForIdle,
  Homing). Adding it to `$MINFO` or the status report would let any browser check at any time. At
  the same time, check that the firmware rejects jobs during recovery, since telnet or the MCP
  server could also start one.

### Proposals from comparing the meeting notes, this file and the map

These aren't decided yet:

- **Blocking during setup:** block running jobs and motion that isn't part of setup. Keep file
  upload available, and never block Stop.
- **Position readout:** show it in the state panel, and also on the compass in layouts where they're
  apart.
- **Log and error boxes:** collapse the log by default, and show errors in the state panel.
- **Status names:** use the map's names, plus an error status for when the board answers and
  reports a fault.

### Gaps in the workflow map

The original map's gaps are covered by the UI modes map in `tools/workflow-map/`, except:

- Power-loss recovery and restoring a known anchor setup, including saved setups for each table.
- Job history (from the meeting notes).

## Open questions

- **Framework:** Preact (about 4 KB) or React (about 45 KB)? Preact is the current leaning,
  for size.
- **Styling approach:** plain CSS modules, Tailwind, or a small component kit?
- **State store:** the framework's built-in context and signals, or a small library?
- **Size budget:** is 300 KB gzipped the right ceiling?
- **Languages:** the current UI has translation packs (`languages/`). Do we keep all of them?
- **Browser support:** what is the oldest phone or browser we must support?
- **Order of migration:** which panel goes first? Suggested: status bar or spindle panel,
  because they're self-contained.
- **Restarting a stopped job:** should the Job stopped message offer "Restart from the beginning"?
- **Confirming Stop:** Stop should stay instant. With progress saved, Continue makes it less costly
  to press by mistake.
- **Upstream:** do we keep any connection to upstream ESP3D-WebUI, or treat this as our own
  code from now on?
