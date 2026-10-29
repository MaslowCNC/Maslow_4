# Bug backlog

Shared list of known bugs and loose ends, worked on later. Newest priority at the top of each
section. When something is fixed, delete it here and mention it in the commit.

Format: **title** — what goes wrong, where, and how we found it.

## High: UI can silently lose sync with the machine

- [ ] **Two tabs in one browser kick each other off forever.** Each new WebSocket closes
  every other socket with the same `sessionId` cookie (`WSChannel.cpp` WS_EVT_CONNECT,
  `closeSessionChannels`), and a cookie is shared by all tabs in a browser. The closed tab
  auto-reconnects every 3 s (`socket.js` RECONNECT_INTERVAL_MS), so they take turns. Every
  HTTP command then 500s ("WebSocket dead") because its `PAGEID` points at a closed socket,
  and the UI shows a blank/stale state with no error. Seen 2026-10-26: connection ID reached
  835 about 40 min after boot. Fixed by closing the other tab.
- [ ] **Session IDs are weak and can collide across browsers.** `get_random_string`
  (`WebUIServer.cpp`) uses `rand() % ('A' - 'Z')` (modulo −25, wrong charset) and reseeds
  with `srand(time(NULL))` on every call, where `time()` is seconds since boot. Two browsers
  that first load in the same second get the same session and hit the bug above.
- [ ] **UI ignores failed commands sent over HTTP.** `SendPrinterCommand` replaces the
  error handler with `noop` for non-`[ESP]` commands (`printercmd.js`), so a 500 is never
  shown. Commands sent through `wscommands.js` (jogs, STOP, calibration buttons) now report
  `error:N`; the rest goes away as commands move there (see Medium: connection protocol).
- [ ] **State is sent once and never resent.** The UI only learns the Maslow state from the
  one `Current state: N` line logged after a state change (`Calibration.cpp`
  `printCurrentState`). It isn't in `MINFO` or the status report, and the UI doesn't send
  `$GSTATE` on WebSocket reconnect (`tablet.js` `onWSOpenCallback`), only on page load. A
  missed line leaves the UI wrong until reload.
- [ ] **WebSocket frames are dropped silently while moving.** In a motion state a full send
  queue drops frames (`WSChannel.cpp` `send_frame`); outside motion a stalled client is
  closed after 250 ms. Only a debug line on USB serial records it. This can lose state lines,
  warnings and errors.

## Medium: stopping

- [ ] **STOP is only fast for jogs.** STOP sends 0x85 then `$STOP`; 0x85 only acts in Jog
  state. During a G-code run, belt retraction or calibration, `$STOP` still waits behind
  the current line. Planned next: per-context stop (feed hold for runs, interrupt retract,
  pause calibration).
- [ ] **`!` sent while Idle enters Hold** (`Protocol.cpp` `protocol_do_feedhold`), a race
  if the jog finishes just before the stop arrives.

## Medium: connection protocol (plan: all commands over the WebSocket)

- [ ] **Steps 3-5 of the plan.** Move home, park, `$TKSLK`, start/pause/resume, startup
  queries and config writes from HTTP `/command` to `wscommands.js`; send `$GSTATE` on every
  reconnect; drop `PAGEID` from command sending; later, the same layer over Web Serial (USB).
- [ ] **Replies can be credited to the wrong command** until then: commands sent over HTTP
  run on the same socket, so their `ok`/`error` can be matched to a pending WebSocket line
  (errors say "probably").
- [ ] **Jog-cancel flush can cut a line in half.** If 0x85 arrives while the polling task
  is partway through assembling a line, `flushQueuedLines` drops the rest and the fragment
  joins the next line (rejected with `error:`, no unintended motion). Fixing it means
  clearing the reader's line buffer from another task.
- [ ] **Dead heartbeat check.** `maslow.js` tracks `[MSG:INFO: Heartbeat]`, which the
  firmware never sends.

## Low: state machine and storage

- [ ] **CALIBRATION_COMPUTING (9) is unreachable.** Nothing requests it.
- [ ] **`$CALRESET` from READY_TO_CUT is silently rejected.**
- [ ] **NVS state isn't tied to the config file.** Belt positions, Z and G54 are shared
  across `.yaml` setups, so switching setups keeps the old belts/Z.
- [ ] **`loadBeltPositions` can apply only some arms** (each arm is checked on its own).
- [ ] **Boot restore bypasses state-change guards** by setting `calibration.currentState`
  directly.

## Tooling

- [ ] **Native unit tests don't build on Linux** (missing `Queue.h` in X86TestSupport).
- [ ] **One UI test fails: `maslowInfoMsgHandling is not defined`** (`maslow.test.ts:167`).
  The test calls it as a global, but `maslow.js:327` declares it as a module `const` and
  never puts it on `globalThis`. Predates the 2026-10-26 work (42/43 pass with bun 1.4.4).
- [ ] **`firmware/install_scripts/*/install-wifi_s3.sh` is the plain-ESP32 script** (wrong
  chip, bootloader at 0x1000).
- [ ] **`/flash-maslow` didn't load as a command** in the session that created it; check it
  appears in a fresh session.
