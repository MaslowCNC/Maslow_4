# Issues log: current WebUI and firmware

Things that fail or behave badly in the current WebUI and firmware, found while testing on the machine.
Two uses:

- **Fix the current (legacy) WebUI and firmware,** so the old UI keeps working while the rewrite happens.
- **Make sure the new UI handles each one,** whether or not the firmware is fixed.

Each entry says what happened, the evidence, the cause if known, the fix for the current UI or firmware,
and what the new UI must do. Design decisions and planned firmware features live in `REWRITE.md`; this
file is only for things that go wrong.

Status: **open**, **fixed in** (commit or PR), or **recheck** (seen on older firmware).

## Session 2026-11-06, firmware Maslow-Mini 3448d81f

| # | Area | Status |
|---|---|---|
| 1 | WebUI: a button press never reaches the board | open |
| 2 | Firmware and WebUI: heartbeat format mismatch, page drops every 20 to 30 s | open, cause found |
| 3 | WebUI: a lost connection isn't obvious | open |
| 4 | WebUI: reconnect loop never succeeds | open |
| 5 | Firmware: boot restored "retracted" with the belts about 680 mm out | open, needs a repeat |
| 6 | Firmware: OTA update failed at 39% | open, needs a repeat |
| 7 | Firmware: belt lengths only arrive every 20 s or so | open |
| 8 | Firmware: Extend overshoots the target by 12 to 23 mm | open, minor |
| 9 | WebUI: Apply tension's warning never shown, because the page had disconnected | open |
| 10 | Firmware and UI: Extend leaves more slack than Apply tension allows | open |
| 11 | WebUI: a failed Apply tension looks like it worked | open |
| 12 | Firmware: the board reset during Find anchors | open, cause not known |
| 13 | WebUI: an `ERR` line that isn't a failure | open, minor |
| 14 | WebUI: startup commands fail with HTTP 500 before the page has an ID | open |
| 15 | Firmware: Find anchors reports "saving configuration failed" when the save worked | open |
| 16 | Firmware: Find anchors gives no total, so progress can't be shown | open |
| 17 | Spindle: over-current 1 s after spin-up, before cutting | open, cause not known |
| 18 | Firmware: belts kept moving about 3.4 s after the job was stopped | open |
| 19 | Firmware: the `SD:` percentage is how much of the file was read, not progress | open |
| 20 | WebUI: Trace Boundary disappears | open, needs details |

### 1. A button press never reaches the board

- **What happened:** Extend was pressed in a page that had been open since the failed firmware update.
  Nothing happened. A newly opened page's Extend worked at once (13:17:19).
- **Evidence:** no `Extending all belts` or `WebUI: Request` line in the board log between 13:16 and
  13:17:00; the board answered telnet the whole time.
- **Cause:** the old page had most likely lost its WebSocket without showing it (see 2 to 4).
- **Fix in the current UI:** don't let a button look like it worked when the page is disconnected. Grey
  out action buttons while `http_communication_locked` or the socket isn't open.
- **New UI must:** show Connecting or Not responding, and disable actions, whenever the connection is
  down. A pressed button should show it's waiting, and say so if no reply comes.

### 2. Heartbeat lost during belt motion

- **What happened:** about 30 to 50 s into an Extend, the page stopped hearing the board's PING. After
  20 s the page logged `No heart beat for more than 20s` and disabled itself.
- **Evidence:** the page's `MINFO` requests came at 13:17:28 and 13:17:51, then stopped. The board kept
  running and logged `All belts extended to 1200.000mm` at 13:18:47, which the page never got.
- **Cause, confirmed:** the heartbeat format changed under the page. The upstream FluidNC v3.9.9 merge
  (16a2b4923 on Maslow-Main, 2026-09-01; into `Maslow-Mini` with 6e0bb9c56 on 2026-10-26) replaced
  `PING:<id>` with plain `PING\n` in `WSChannels::sendPing()`. `socket.js` only counts `PING:<id>`, so on
  this firmware every page disconnects itself 20 to 30 s after connecting, even with the board idle. The
  console showed it at 13:38: `Connected`, the state arrived, then `No heart beat for more than 20s` and
  `Disconnected` with no motion. Reconnects then succeed or fail by chance, more often failing while
  the belts move (see 4).
- **Fix:** either make `socket.js` count any message starting with `PING` (one line, fixes the legacy UI
  at once), or send `PING:<id>` from the firmware again, redoing the page ID fix from draft PR
  MaslowCNC/Maslow_4#1129 on the rewritten `WSChannel.cpp`. Doing both is safest.
- **Earlier suspects, kept for reference:**
  - **The heartbeat's format.** `WSChannels::sendPing()` sends `PING\n` every 10 s
    (`WebUIServer.cpp`), but `socket.js` only counts a message as a heartbeat when it splits on `:` into
    two or more parts (`PING:<id>`, around line 282). The only `PING:` the board sends is its reply to a
    `PING:` from the page, which the current page doesn't seem to send. If the heartbeat is never
    counted, the 20 s timer runs out whenever nothing else resets it.
  - **Dropped messages during motion.** In a motion state (Extend and Apply tension run as Homing),
    `WSChannel::send_frame()` drops messages when the client's queue is full, logging only at debug
    level (`Websocket queue full… dropping`). With status reports every 50 ms, the queue fills while the
    belts move, and PING goes out through the same server.
- **Fix:** confirm which one by watching the page's console and the board's debug log during an Extend.
  Then either send the heartbeat in the format the page expects (or make the page count `PING` and any
  incoming message as a heartbeat), and never drop the heartbeat when the queue is full.
- **New UI must:** treat a missing heartbeat as Not responding, keep the last known state visible but
  marked out of date, and ask for the state again (`$GSTATE`, `$MINFO`) as soon as it reconnects.

### 3. A lost connection isn't obvious

- **What happened:** after the heartbeat was lost, the page still showed "Extending" while the machine
  had finished and gone to Belts extended.
- **Evidence:** `Disable_interface(true)` ran (console: `No heart beat for more than 20s`, then
  `Disconnected`); it should show the disconnect dialog and add "(Disabled)" to the tab title, but the
  user didn't notice either.
- **Fix in the current UI:** check that the disconnect dialog actually shows over the tablet UI, and make
  it hard to miss.
- **New UI must:** the Not responding state replaces the whole top bar, so stale state can't be mistaken
  for live state.

### 4. Reconnect loop never succeeds

- **What happened:** the disconnected page tried to reconnect about 109 times over about 5½ minutes,
  and every try failed. Reloading the page connected at once (13:25:22).
- **Evidence:** console: `WebSocket connection to 'ws://maslow.local/' failed: WebSocket is closed before
  the connection is established`, repeated. The board logged no request during that time.
- **Cause:** `Disable_interface()` starts `setInterval(startSocket, 3000)`, and `startSocket()` closes the
  previous socket first. So each attempt is closed by the next one after 3 s, before it can finish. When
  connecting from the open page takes longer than 3 s (the `.local` lookup can be slow), it can never
  succeed. `onclose` also schedules its own `setTimeout(startSocket, 3000)`, so two retry paths can
  overlap.
- **Fix in the current UI (`www/js/socket.js`):** start a new attempt only after the previous one has
  failed or timed out (for example 10 s), with backoff, and use one retry path instead of two.
- **New UI must:** retry the same way, and offer a Reload button if it can't reconnect after a while.

### 5. Boot restored "retracted" with the belts about 680 mm out

- **What happened:** after a restart the board came back as `RETRACTED` with belt lengths near 0. The
  next Retract then wound in 669 to 695 mm of belt on every arm.
- **Evidence:** `Current state: 2` with `tl: -0.495` at 13:01:17, then at 13:04:38 `Belts pulled tight with
  offsets TL:-680.997 TR:-695.054 BL:-689.196 BR:-669.349`.
- **Cause:** not known. Maybe the saved positions were wrong, for example from an earlier false retract
  under tension, or the boot restore misread them.
- **To do:** repeat on purpose: note the belts' real state, restart, and compare what the board restores.
- **Repeat at 14:32, not reproduced:** power cycled from Ready to cut with TL 838.745, TR 700.058,
  BL 717.599, BR 515.428. The board came back as `EXTENDEDOUT` (state 4, never Ready to cut) with every
  length within 0.02 mm. So the restore works when the board was shut down from a known state; this
  morning's bad restore probably came from what was saved before then, for example after a false
  retract or a reset in the middle of a step. Keep an eye out for it.
- **New UI must:** never trust "belt lengths known" blindly after a reboot. The check by eye after a
  retract stays, and Confirm the work area should show the restored lengths.

### 6. OTA update failed at 39%

- **What happened:** a firmware upload through the WebUI stopped with `Update failed` at 39% (13:04:15).
- **Evidence:** during the upload, `Encoder I2C error` and `Bad connection on Top Right encoder` /
  `Bottom Right encoder` warnings, and `Slow Maslow.update took 10x ms` several times.
- **Cause:** not known. The upload may compete with the encoder and update loop.
- **To do:** retry an upload and see whether it fails at the same point.
- **New UI must:** show upload progress, say clearly when it fails, and not leave the page in a broken
  state afterwards (the page used for this upload is the one that went stale in 1).

### 7. Belt lengths only arrive every 20 s or so

- **What happened:** during Extend, the only belt lengths were in `MINFO`, which the page asks for about
  every 23 s. BL went from 322 mm to 1223 mm with nothing in between.
- **Fix:** the planned live report (see `REWRITE.md`, Device details tabs).
- **New UI must:** not show belt lengths as live unless they are.

### 8. Extend overshoots the target by 12 to 23 mm

- **What happened:** after Extend finished at 1200 mm, `MINFO` showed 1213 to 1223 mm, with `etl` to
  `ebr` between 12 and 23 mm.
- **Cause:** probably the belts paying out a little more after reaching the target, as the user kept
  pulling.
- **Note:** goes away with the planned Done extending change, which drops the fixed target.

### 9. Apply tension's warning never shown, because the page had disconnected

- **What happened:** Apply tension stopped after 10 s with the 300 mm warning, and no dialog appeared.
- **Evidence:** the board logged `Maslow Apply Tension retraction warning: Belt Top Right retracted
  300.140mm…` at 13:28:11 and went back to `Current state: 4`. The page's last check-in was at
  13:28:08; it stopped polling during the belt motion, as in 2.
- **Cause:** the same lost heartbeat as 2. `maslow.js` would have shown an "Apply Tension Warning"
  dialog with Cancel and Continue (`$TKSLK`), but the page never got the message.
- **Fix:** same as 2. Also, a warning that changes what the user should do shouldn't exist only as a
  one-time log line: the board should report the last Apply tension result so a reconnecting page can
  still show it.
- **New UI must:** after reconnecting, ask for the state and the last step's result, and show the
  warning then.

### 10. Extend leaves more slack than Apply tension allows

- **What happened:** with all belts extended to the 1200 mm target (plus the overshoot in 8), Apply
  tension wound in more than 300 mm on the top right belt and stopped, as if the belt weren't attached.
- **Cause:** Extend pays every belt out to the work area's target length, which can be far past where
  the anchor actually is. Apply tension then has more slack to take up than its 300 mm limit.
- **Fix:** the planned Done extending change, so belts are only as long as the anchors need. Until
  then, the current UI's Continue button lets the user keep going.
- **New UI must:** treat this result separately from "the position doesn't fit the anchors". It means
  "check every belt is attached", then Continue applying tension or Retract, never Find anchors.

### 11. A failed Apply tension looks like it worked

- **What happened:** the second Apply tension wound the belts tight, so from the machine it looked
  finished. After a reload the page showed Belts extended, as if the state had jumped back.
- **Evidence:** at 13:32:53 the board logged `[MSG:ERR: Apply Tension failed: the measured belt lengths
  (TL: 814.061 TR: 807.633) do not fit the anchor locations, maybe try running Find Anchors again?]` and
  went back to `Current state: 4`. This is the `20493b1b` fallback working as intended.
- **Cause:** the current UI only shows this as a line in the log, with no dialog or prompt.
- **Fix in the current UI:** show the error as a dialog with a Find anchors button, like the retraction
  warning has.
- **New UI must:** show "Couldn't confirm the position" and move the user to Find anchors (the map's
  "Position confirmed? no" branch), separately from the 300 mm "belt may not be attached" result in 10.
  A firmware report of the last Apply tension result would make this reliable across reloads.

### 12. The board reset during Find anchors

- **What happened:** about a minute into Find anchors, after the first measured point, the board reset.
  It came back in `UNKNOWN` (state 0) with every belt length at 0, so the page correctly showed Belt
  lengths unknown. Find anchors didn't finish.
- **Evidence:** the last status was `<Home|…>` at 13:37:35, then this server's telnet connection was
  reset by the board at 13:37:45, and the board was back in `<Idle|…>` at 13:37:51 with `Current state: 0`,
  `homed: false`. The boot messages, including the reset reason, were missed while reconnecting.
- **Cause:** not known yet. Candidates: a crash or watchdog reset, or a brownout from the motors while
  measuring. It wasn't the Maslow emergency stop, which goes to Alarm instead of resetting.
- **To do:** get the reset reason (from the serial console, or `[ESP420]` if it reports it), and try
  Find anchors again to see whether it resets at the same point.
- **New UI must:** handle a reset in the middle of a step: show Not responding, then Connecting, then
  the state the board comes back in (here Belt lengths unknown), with a message that the step didn't
  finish.

### 13. An `ERR` line that isn't a failure

- **What happened:** Find anchors logged `[MSG:ERR: Center point off by over 100.000mm]`, then
  `Frame size successfully adjusted to: 1178.300 x 1178.300` and carried on measuring.
- **Why it matters:** a UI that treats every `[MSG:ERR:` as "the step failed" would stop or alarm here.
- **New UI must:** decide success or failure from the state and from known result messages, not from
  the `ERR` prefix. The firmware could log this one as a warning.

### 14. Startup commands fail with HTTP 500 before the page has an ID

- **What happened:** on every page load, the first commands go over HTTP with an empty page ID and the
  board answers `500 (Internal Server Error)`: `$Report/Interval=50`, `?`, `$SS`, `$MINFO`, `$G` and
  `$GSTATE`. The page recovers once its WebSocket opens and the board sends `CURRENT_ID`, and then asks
  for the state again.
- **Evidence:** console on the 13:45 load: `GET http://maslow.local/command?commandText=…&PAGEID= 500`
  for each of those, then `Connected`, `connection id = 3`, and the state.
- **Cause:** the page sends commands before `CURRENT_ID` has set its page ID, and the board rejects a
  command with no page ID. (Since the heartbeat no longer carries an ID, `CURRENT_ID` is the only way
  the page learns it.)
- **Fix in the current UI:** queue commands until the WebSocket is open and the page ID is known, or
  send these first commands over the WebSocket instead of HTTP.
- **New UI must:** stay in Connecting until it has a connection and an ID, and only then ask for the
  state.

### 15. Find anchors reports "saving configuration failed" when the save worked

- **What happened:** Find anchors finished at 14:03:14 and logged `[MSG:ERR: Find Anchors completed, but
  saving configuration failed: 115]`, then went to Ready to cut.
- **Evidence:** `maslow.yaml` on the board grew from 6,189 to 7,093 bytes and holds the new anchors
  (`tlX: -4.042199`, `trX: 1395.804688`, …) and `Maslow_Extend_Dist: 790.206421`, matching the final fit.
- **Cause:** error 115 is `Error::Deferred` ("handed to cmd_queue, ack comes later", `Error.h`), added
  with the FluidNC 3.9.9 merge. `Calibration.cpp` runs `$CO` and treats anything but `Error::Ok` as a
  failure (around line 1200), so a deferred save is reported as failed.
- **Fix:** treat `Error::Deferred` as success there, or wait for the deferred result before logging.
- **New UI must:** not tell the user the anchors weren't saved because of this line.

### 16. Find anchors gives no total, so progress can't be shown

- **What happened:** Find anchors took about 10.7 minutes (13:52:31 to 14:03:14) for 33 points, about
  15 s each, plus three re-fits. The board logs `Measured waypoint N` and the coordinates, but never
  how many points there will be.
- **What it measures:** 6 starting points, the center, then rings of the grid (8 and 16 points for a
  5×5), plus a closing point. It re-fits after the starting points and each ring, logging the fit
  quality (`rms=0.437mm max=1.160mm`). The grid size (3×3 to 9×9) is chosen from the frame size.
- **Fix:** report the total with each point (`Measured waypoint 18 of 33`), and the latest fit quality.
- **New UI must:** show progress for Find anchors: point N of total, time left, and which part of the
  grid is done.

### 17. Spindle over-current 1 s after spin-up, before cutting

- **What happened:** the first job (`rect_80mm_40passes.gcode`) turned the spindle on at 13,000 rpm,
  and about 1 s later the spindle board stopped it with a driver over-current.
- **Evidence:** 14:20:55.4 `state: Idle - Tool Loaded -> Spindle On`; 14:20:56.4 `driver over-current
  (nFAULT retries) - spindle stopped; re-home the Z, then send a new speed`, `fault 2`. The bit was
  still above the work: Z was at −5.36 machine, with work Z zero at −9.00. A trip recording was saved to
  `/spindle_trip.csv` (322 lines).
- **Cause:** not known. Not a cut, since the bit hadn't reached the work. From the trip recording
  (saved as `tools/maslow-mcp/logs/downloads/spindle_trip.csv`, 318 samples from −1011 to +146 ms):
  - Both spindle motors were ramping together, reaching about 4,830 rpm of the 13,000 asked for when
    the board stopped them at t = 0.
  - Their voltage limit (`vlim`) climbed to about 5.97 V and flattened out for the last 30 ms, while
    the phase lag grew from about 10° to 41° and `iq` kept rising. The motors look voltage-limited,
    falling behind the ramp.
  - Phase current samples repeatedly read about 11.6 A on both motors from the start of the ramp,
    which looks like the current measurement's ceiling rather than a real reading.
  - Only motor 1's driver flagged faults: `nFAULT` edges start 467 ms before the trip, in 27 samples.
    Motor 2's never did. The trip was the board's `nFAULT retries` limit on motor 1.
  - So: a spin-up that outran the available voltage, with motor 1's driver tripping. Worth checking
    the spin-up ramp and voltage limit settings, and whether motor 1 or its driver differs from motor 2.
    This is a reading of the data, not a confirmed cause.
- **What went right:** the recovery followed our design: the job ended, Z re-homed, and the board
  logged `work Z zero restored (-9.000 mm) - ready to cut`, all within about 8 s.

### 18. Belts kept moving about 3.4 s after the job was stopped

- **What happened:** after the spindle fault, the board logged `ending the job ($STOP)` at 14:20:56.8,
  but the status stayed `Run` and the sled moved from X 60.3 to X 18.3 at 1,200 mm/min until 14:21:00.2.
- **Cause:** probably moves already queued in the planner finishing after Stop. This is the same thing
  `haltMotion()` is meant to prevent for the Stop button.
- **Why it matters:** Stop has to stop the belts at once, especially after a fault.
- **To do:** press Stop during a job and time how long the belts keep moving.

### 19. The `SD:` percentage is how much of the file was read, not progress

- **What happened:** 2 s into the job, the status said `SD:63.79,/sd/rect_80mm_40passes.gcode,217`, so
  63.79% at line 217 of a 40-pass job.
- **Cause:** the firmware reads the file ahead of the motion, and the percentage counts bytes read.
- **New UI must:** not show that number as job progress. It needs the line being executed, or a
  firmware report of real progress.

### 20. Trace Boundary disappears

- **What happened:** the Trace Boundary button disappeared while the machine was in Ready to cut.
- **Cause, likely:** `updateJobBoundsDisplay()` (`toolpath-displayer.js`) only shows it when the page
  has state Ready to cut, a G-code file loaded with a name, and a bounding box it could compute. Any of
  those missing hides it, with no message.
- **To do:** note whether a file was loaded and showing a toolpath when it disappeared.
- **New UI must:** keep Trace boundary next to Run, available whenever Run is.

Also seen while starting the job: `Slow command took 128 ms: $sd/run=…`, `Current T:-1 Selected T:1`, and
`20 (Unsupported GCode command) … at line 215`, probably a tool change the firmware doesn't support.
Worth checking what line 215 is.

## Still to test

Held back on 2026-11-06 so we didn't lose a good calibration before trying a cut:

- **Stop in the middle of a step,** for example during Apply tension: which state Stop leaves the board
  in, and whether the belt lengths stay known.
- ~~**A reboot while tensioned**~~ — done at 14:32 (power cycle): came back as Belts extended, lengths
  within 0.02 mm, and the page reconnected and showed Belts extended by itself.
- **Find anchors again,** to see whether the reset in issue 12 happens at the same point.
- **An OTA upload,** to see whether issue 6 repeats.

## From earlier testing (older firmware, recheck on 3448d81f)

| Issue | Where it's described |
|---|---|
| Stale state after a reconnect: the page doesn't ask for the state again | Draft PR MaslowCNC/Maslow_4#1129 |
| PING page ID sent as a raw byte | `REWRITE.md`, Later firmware changes |
| The UI treats an HTTP "Error" reply as success | `REWRITE.md` |
| Retract under tension reports success with the belts still out | `REWRITE.md`, When a Setup step goes wrong |
| Partial belt restore at startup (only TL restored) | `REWRITE.md`, Later firmware changes |
| `ovf` printed for a waypoint coordinate | `REWRITE.md`, Later firmware changes |
| Stop in Alarm started a Z raise and stalled the update loop | `REWRITE.md`, Later firmware changes |
