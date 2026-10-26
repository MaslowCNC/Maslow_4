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
- [ ] **UI ignores failed `$` commands.** `SendPrinterCommand` replaces the error handler
  with `noop` for non-`[ESP]` commands (`printercmd.js`), so a 500 is never shown.
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

- [ ] **`$STOP` is slow to take effect.** It's a text command, so it runs only after the
  current move is fully queued; Maslow splits XY moves into ≤5 mm pieces (up to 1000).
  Direction: send `!`/`0x85` before `$STOP` from the UI, or make the Maslow stop realtime.
- [ ] **Unverified: does `$STOP` empty the planner queue** on the new FluidNC base, or can
  motion resume afterwards?
- [ ] **`!` sent while Idle enters Hold** (`Protocol.cpp` `protocol_do_feedhold`), a race
  if the jog finishes just before the stop arrives.

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
- [ ] **`firmware/install_scripts/*/install-wifi_s3.sh` is the plain-ESP32 script** (wrong
  chip, bootloader at 0x1000).
- [ ] **`/flash-maslow` didn't load as a command** in the session that created it; check it
  appears in a fresh session.
