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
- [ ] **Session IDs are weak and can collide across browsers.** `get_random_string`
  (`WebUIServer.cpp`) uses `rand() % ('A' - 'Z')` (modulo −25, wrong charset) and reseeds
  with `srand(time(NULL))` on every call, where `time()` is seconds since boot. Two browsers
  that first load in the same second get the same session, so connecting in one closes the other
  (each then shows the "another tab" notice, code 4001).
- [ ] **WebSocket frames are dropped silently while moving.** In a motion state a full send
  queue drops frames (`WSChannel.cpp` `send_frame`); outside motion a stalled client is
  closed after 250 ms. Only a debug line on USB serial records it. This can lose state lines,
  warnings and errors.

## Medium: stopping

- [ ] **0x18 during a Maslow belt operation alarms.** Retract, extend, take slack and
  calibrate run in Home state, where a reset goes through `Homing::fail` (ALARM "Homing fail
  reset"). The motors stop at once, and the `$STOP` that follows clears the alarm, but the
  ALARM line makes the UI save the serial log and show an error. Untested on the machine;
  check whether the calibration state machine resumes or stays stopped.
- [ ] **Feed hold decelerates over the rest of the move.** STOP used to send `!`. On
  2026-10-30, `!` during a long move to the first cut slowed the machine at once, but the
  hold only completed at the end of the move ("Stopping the paused job" logged then). At
  50 mm/s² and 2000 mm/min it should stop in about 11 mm. Pause (`!`) has the same problem.
  Parked 2026-11-04: the plan is STOP + resume from the stopped line, and no Pause button. If
  that falls short, a fast hold (fix this, and park Z with the spindle off) is the fallback.

## Medium: connection protocol (plan: all commands over the WebSocket)

- [ ] **About 60 UI calls still send commands directly instead of through `machine.js`.**
  `sendCommand` / `SendPrinterCommand` calls remain in `tablet.js` (36), `grbl.js` (7),
  `controls.js` (6), `grblpanel.js` (4), `maslow.js` (4), `files.js` (2) and `restartdlg.js` (1),
  counted 2026-11-04. The agent rules (`.github/copilot-instructions.md`) say to move a feature
  over when it is changed. The rest could be done as a planned migration.
- [ ] **Rest of the connection plan.** Every line command now goes over the WebSocket
  (`wscommands.js`). Left: multi-line reads still over HTTP `plain=` (`loadConfigValues`,
  `$CO` and setting writes in `settings.js`); reconnect backoff; at most one `?` outstanding;
  later, the same layer over Web Serial (USB).
- [ ] **Jog-cancel flush can cut a line in half.** If 0x85 arrives while the polling task
  is partway through assembling a line, `flushQueuedLines` drops the rest and the fragment
  joins the next line (rejected with `error:`, no unintended motion). Fixing it means
  clearing the reader's line buffer from another task.

## Low: state machine and storage

- [ ] **STOP during other belt operations leaves the in-progress state.** STOP during
  Extend, Find Anchors or Release Tension leaves state 3, 6 or 8 with no motion, and Extend,
  Find Anchors and Release Tension are each refused from their own state, so only a retract
  gets out of it. Apply Tension (5) is handled (`Calibration::stopTakingSlack`). Seen by reading
  the code (2026-11-04), not tested.
- [ ] **CALIBRATION_COMPUTING (9) is unreachable.** Nothing requests it.
- [ ] **`$CALRESET` from READY_TO_CUT is silently rejected.**
- [ ] **NVS state isn't tied to the config file.** Belt positions, Z and G54 are shared
  across `.yaml` setups, so switching setups keeps the old belts/Z.
- [ ] **Boot restore bypasses state-change guards** by setting `calibration.currentState`
  directly.

## Low: web UI

- [ ] **A config alarm doesn't say what's wrong or how to fix it.** Any unknown key in
  `maslow.yaml` logs "Ignored key …" and sets ConfigAlarm (`log_config_error`, `Logging.h`).
  The status report just says `Alarm`, and the UI's `$X` then fails with error 152, whose text
  says the file "may be corrupt" and suggests a power cycle, which doesn't help. Seen 2026-11-07
  after moving a machine from the Belt-stretch-compensation firmware (keys
  `beltAxialStiffness`, `sledWeight`, `calibrationPullForce`); fixed by hand-editing the file.
  Keeping the alarm for unknown keys is fine. Wanted: the UI says which keys or errors caused it
  (from a status-report or `$` field, not by parsing boot log lines) and offers a fix, e.g.
  remove the unknown keys and restart, with the original kept as a backup.
- [ ] **File sizes always show in bytes.** `formatFileSize` (`files.js`) loops with
  `for...in` over the unit array, so it gets indices, not units, and never matches a unit.
  Found reading the code (2026-11-04).
- [ ] **`buildHttpCmd` encodes every parameter.** It checks `[noEncKeys].includes(key)` (an
  array inside an array), which is never true, so the "don't encode" list does nothing
  (`httpCmdBuilders.js`). Harmless for the keys used now. Found reading the code (2026-11-04).

## Tooling

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
- [ ] **A failed WiFi firmware update doesn't say why, and space is getting tight.** On
  2026-11-07 a machine on v1.23.0 (Belt-stretch-compensation, 192.168.0.103) refused `/updatefw`
  with "Upload rejected, not enough space" from a failed `Update.begin()`, although its spare slot
  (2.94 MB) was bigger than our image (2.77 MB). It answered with the status line `HTTP/1.1 1`,
  which curl can't parse, so `flash-maslow.sh` only printed "Firmware upload failed (curl)", and
  the web UI's update page would hit the same refusal. USB worked. To do: show the machine's
  reason (script and UI), find why `Update.begin()` failed, and check the assumption behind WiFi
  updates that two copies of the firmware fit at once: each app slot in `max_littlefs.csv` is
  3,080,192 bytes and the firmware already uses 94.4% of it, so about 170 KB of growth is left
  before WiFi updates stop working. A size check at build time would catch it early.
- [ ] **`flash-maslow.sh usb` is broken with esptool 5.** `pio pkg exec` runs PlatformIO's
  `tool-esptoolpy` 5.3.0 with a Python that lacks `rich_click`, and esptool 5 removed
  `--verify` (it always verifies) and renamed the options (`write-flash`, `--flash-mode`, …).
  Running `~/.platformio/penv/bin/python …/esptool.py` with the new names worked (2026-11-07).
- [ ] **Native unit tests don't build on Linux** (missing `Queue.h` in X86TestSupport).
- [ ] **One UI test fails: `maslowInfoMsgHandling is not defined`** (`maslow.test.ts:167`).
  The test calls it as a global, but `maslow.js:327` declares it as a module `const` and
  never puts it on `globalThis`. Predates the 2026-10-26 work (107/108 pass on 2026-11-04).
- [ ] **`firmware/install_scripts/*/install-wifi_s3.sh` is the plain-ESP32 script** (wrong
  chip, bootloader at 0x1000).
- [ ] **`/flash-maslow` doesn't load as a skill.** It is in `.claude/skills/flash-maslow`, but
  it was missing from the skill list in the session that created it and again on 2026-11-04.
  Flashing has been done by running `scripts/flash-maslow.sh` directly.
