# Bug backlog

Shared list of known bugs and loose ends, worked on later. Newest priority at the top of each
section. When something is fixed, delete it here and mention it in the commit.

Format: **title** — what goes wrong, where, and how we found it.

## High: UI can silently lose sync with the machine

- [ ] **A full input queue drops lines with no reply.** `Channel::queue_push` discards whole
  lines once a channel has `4 * maxLine` (1020) bytes unread, logging only at debug level.
  The sender never gets `ok`/`error` for them, which breaks reply matching. A client must pace
  its sends (`wscommands.js` sends one at a time); answering dropped lines with an `error:`
  would make it visible.
- [ ] **An HTTP request from the dev computer drops the browser's WebSocket.** With the UI
  open through `npm run dev` (localhost:8485 → m4), one `curl "http://m4.local/command?plain=$I"`
  from the same computer closed the tab's socket (it reconnected 3 s later). Seen 2026-11-02.
  Cause not found: `/command` doesn't call `closeSessionChannels`. Could be the dev server's
  proxy or a connection limit on the ESP32.
- [ ] **Two tabs in one browser kick each other off forever.** Each new WebSocket closes
  every other socket with the same `sessionId` cookie (`WSChannel.cpp` WS_EVT_CONNECT,
  `closeSessionChannels`), and a cookie is shared by all tabs in a browser. The closed tab
  auto-reconnects every 3 s (`socket.js` RECONNECT_INTERVAL_MS), so they take turns, and
  commands fail with "Not sent" while their socket is down. Possible fix: open the socket
  with `?independent_session` (supported by the firmware), now that no command relies on
  the session cookie or `PAGEID`. Seen 2026-10-26: connection ID reached
  835 about 40 min after boot. Fixed by closing the other tab.
- [ ] **Session IDs are weak and can collide across browsers.** `get_random_string`
  (`WebUIServer.cpp`) uses `rand() % ('A' - 'Z')` (modulo −25, wrong charset) and reseeds
  with `srand(time(NULL))` on every call, where `time()` is seconds since boot. Two browsers
  that first load in the same second get the same session and hit the bug above.
- [ ] **State is only sent as a log line.** The UI learns the Maslow state from the
  `Current state: N` line logged after a state change (`Calibration.cpp` `printCurrentState`).
  It isn't in `MINFO` or the status report. The UI now asks with `$GSTATE` on every socket
  open, but a line missed while connected leaves the UI wrong until the next change or
  reconnect. Planned fix: a Maslow state field in the status report.
- [ ] **WebSocket frames are dropped silently while moving.** In a motion state a full send
  queue drops frames (`WSChannel.cpp` `send_frame`); outside motion a stalled client is
  closed after 250 ms. Only a debug line on USB serial records it. This can lose state lines,
  warnings and errors.

## Medium: stopping

- [ ] **The 0x18 stop isn't tested on the machine yet.** STOP now sends 0x18 (soft reset),
  then `$STOP` once the restart banner arrives. In Run, Jog and Hold the reset path syncs the
  belts to the encoders and goes Idle instead of ALARM:3 (`protocol_do_rt_reset`). Check:
  the job ends at once, nothing lurches on the next move, Z rises.
- [ ] **0x18 during a Maslow belt operation alarms.** Retract, extend, take slack and
  calibrate run in Home state, where a reset goes through `Homing::fail` (ALARM "Homing fail
  reset"). The motors stop at once, and the `$STOP` that follows clears the alarm, but the
  ALARM line makes the UI save the serial log and show an error. Untested on the machine;
  check whether the calibration state machine resumes or stays stopped.
- [ ] **Feed hold decelerates over the rest of the move.** STOP used to send `!`. On
  2026-10-30, `!` during a long move to the first cut slowed the machine at once, but the
  hold only completed at the end of the move ("Stopping the paused job" logged then). At
  50 mm/s² and 2000 mm/min it should stop in about 11 mm. Pause (`!`) has the same problem.

## Medium: connection protocol (plan: all commands over the WebSocket)

- [ ] **Rest of the connection plan.** Every line command now goes over the WebSocket
  (`wscommands.js`). Left: multi-line reads still over HTTP `plain=` (`loadConfigValues`,
  `$CO` and setting writes in `settings.js`); reconnect backoff; at most one `?` outstanding;
  later, the same layer over Web Serial (USB).
- [ ] **Jog-cancel flush can cut a line in half.** If 0x85 arrives while the polling task
  is partway through assembling a line, `flushQueuedLines` drops the rest and the fragment
  joins the next line (rejected with `error:`, no unintended motion). Fixing it means
  clearing the reader's line buffer from another task.
- [ ] **Dead heartbeat check.** `maslow.js` tracks `[MSG:INFO: Heartbeat]`, which the
  firmware never sends.

## Low: state machine and storage

- [ ] **STOP during Apply Tension probably leaves the state at TAKING_SLACK.** `$STOP` sets
  Idle and stops the motors, but nothing changes the calibration state, so the machine reports
  state 5 with no motion and refuses `$TKSLK` (only allowed from EXTENDEDOUT, READY_TO_CUT or
  UNKNOWN); a retract gets out of it. Seen by reading the code (2026-11-04), not tested.
- [ ] **CALIBRATION_COMPUTING (9) is unreachable.** Nothing requests it.
- [ ] **`$CALRESET` from READY_TO_CUT is silently rejected.**
- [ ] **NVS state isn't tied to the config file.** Belt positions, Z and G54 are shared
  across `.yaml` setups, so switching setups keeps the old belts/Z.
- [ ] **Boot restore bypasses state-change guards** by setting `calibration.currentState`
  directly.

## Tooling

- [ ] **`flash-maslow.sh build` doesn't compile.** The `pio run` lines sit inside the
  `--skip-build --fs` wifi branch (around line 126), so `build` (and a flash without
  `--skip-build`) prints "Build complete" with whatever firmware.bin is already there, while
  `--skip-build --fs` over WiFi does build. Found 2026-11-04 when a build left firmware.bin at
  its old time. Workaround: `PLATFORMIO_DATA_DIR=firmware/.pio/fsdata pio run -e wifi_s3`.
- [ ] **New `$Hostname` didn't apply after the FluidNC tab's Restart button** (2026-10-29,
  Maslow 4.1 renamed to `m4`); a power cycle applied it. The button sends `[ESP444]RESTART`
  (`restartdlg.js`), which raises `fullResetEvent` (`WebCommands.cpp`, `Protocol.cpp`) and
  should reboot the chip, so either that restart didn't run or the old name was cached on
  the computer. Not diagnosed yet.
- [ ] **Every machine is `maslow.local`, so two on one network collide.** The default
  `$Hostname` is `maslow`, and mDNS answers go to whichever board replies first. Then
  `flash-maslow.sh wifi maslow.local`, `npm run dev` and the browser can each reach a different
  machine with no warning, and a WiFi flash can land on the wrong one. Seen 2026-10-29 with a
  Maslow 4.1 (192.168.1.253) and a Unified Control Board (192.168.1.243). Work around with the
  IP, or give each its own `$Hostname`. A default like `maslow-<last 4 of MAC>`, or a board
  check in the flash script, would prevent it.
- [ ] **`flash-maslow.sh` never builds firmware unless `--skip-build` is passed.** The
  `echo "== Building firmware"` / `pio run` lines sit inside the `elif` (skip-build + wifi
  + `--fs`) branch instead of the normal build path, so `build`, `wifi` and `usb` stop with
  "missing .../bootloader.bin (build first)" in a fresh checkout or worktree, and flash a
  stale `firmware.bin` otherwise. Found 2026-10-26 flashing from the ui-polish worktree.
- [ ] **Native unit tests don't build on Linux** (missing `Queue.h` in X86TestSupport).
- [ ] **One UI test fails: `maslowInfoMsgHandling is not defined`** (`maslow.test.ts:167`).
  The test calls it as a global, but `maslow.js:327` declares it as a module `const` and
  never puts it on `globalThis`. Predates the 2026-10-26 work (42/43 pass with bun 1.4.4).
- [ ] **`firmware/install_scripts/*/install-wifi_s3.sh` is the plain-ESP32 script** (wrong
  chip, bootloader at 0x1000).
- [ ] **`/flash-maslow` didn't load as a command** in the session that created it; check it
  appears in a fresh session.
