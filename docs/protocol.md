# Maslow connection protocol (draft)

How a client talks to a Maslow 4 / 4.1: the browser UI over WiFi today, and later a USB-connected
browser and an ESP-NOW pendant. This is a first pass that describes what the firmware does now
(branch `exp1`, commit `0d9585929`), not a finished spec. Where behaviour looks wrong it is noted,
not changed.

Firmware behaviour and what our browser UI does with it are described separately: the parts
headed "Our UI" describe the UI on `exp1` after the change that moves every line command onto the
WebSocket.

## 1. Connections

The firmware is FluidNC with a Maslow layer. Every connection is a FluidNC `Channel`, and every
channel speaks the same line protocol (section 3):

| Connection | Firmware class | Notes |
|---|---|---|
| WebSocket, one per browser tab | `WSChannel` | `ws://<host>/` (path `/`), subprotocol `arduino` |
| USB serial | `UartChannel` | the port `scripts/maslow_serial.py` uses; open with DTR/RTS low or the board resets |
| Telnet | `TelnetClient` | port 23 when enabled |
| ESP-NOW (planned) | none yet | would be one more `Channel` |

HTTP is separate: it is request/response and does not speak the line protocol. Section 2 lists what
is only reachable over HTTP.

Our UI sends every line command (G-code, jogs, `$` commands, realtime bytes) over its WebSocket and
uses HTTP only for true request/response: `[ESP…]` commands, a few reads that need the reply in the
HTTP body, files, firmware update and login (section 3.1).

## 2. HTTP-only features

These need the web server, so they are not available over USB or ESP-NOW today. Routes are
registered in `WebUI_Server::begin()` (`WebUI/WebUIServer.cpp`).

### Endpoints our UI uses

What our UI keeps using once every line command goes over the WebSocket:

| Endpoint | Method | What it does | What the UI uses it for |
|---|---|---|---|
| `/` | GET | serves `index.html.gz` (the UI) | loading the UI |
| `/<path>` | GET | any other path downloads that file from the flash filesystem, e.g. `/maslow.yaml`. A path starting `/SD/` reads from the SD card instead | config and preference files; G-code files for the preview (`SD/<file>` in `tablet.js`) |
| `/files` | GET | flash filesystem: list; `action=delete\|deletedir\|createdir\|rename` with `filename=` | the flash file manager |
| `/files` | POST multipart | upload to the flash filesystem | config files, `index.html.gz` |
| `/upload` | GET / POST | the same for the SD card | G-code files |
| `/updatefw` | POST multipart | firmware update (OTA); reboots when done | firmware update |
| `/login` | GET / POST | authentication. Off in our build, where it only answers that no login is needed | checked when the UI connects |
| `/command?commandText=…` | GET | a line starting with `[ESP` or `$/` runs inside the web server, reply in the HTTP body. **Any other line is injected into this tab's WebSocket channel** (found by the `PAGEID` parameter, else the session cookie): the reply arrives on the socket, the HTTP body is empty, 500 "WebSocket dead" if there is no socket | `[ESP…]` commands only |
| `/command?plain=…` | GET | runs a `$…` or `[ESP…]` command inside the web server, reply in the HTTP body. Not for G-code: the line is parsed as a setting or command name | Maslow config reads (`$/…`), `$CO`, setting writes |

### Endpoints our UI doesn't use

The firmware serves these, but our UI never calls them. Other clients (desktop tools, bookmarks,
the upstream FluidNC UIs) may.

| Endpoint | Method | What it does |
|---|---|---|
| `/command?cmd=…` | GET | same as `commandText=` (the upstream 3.x UI uses this name) |
| `/command_silent` | GET | `/command` without echoing the command |
| `/flash/…`, `/sd/…` | WebDAV | file access for desktop clients (mount the flash or the SD card as a network drive) |
| `/feedhold_reload`, `/cyclestart_reload`, `/restart_reload`, `/did_restart` | GET | feed hold / cycle start / reset, then redirect (for bookmarks) |
| `/trace?msg=` | GET | prints a browser debug message on USB serial |
| `/generate_204`, `/fwlink/`, … | GET | captive-portal checks, only in access-point mode; they serve the UI |

**Commands that run inside the web server are not ordered with any channel.** Each one gets a
temporary channel and runs on the web server's background task, outside the main loop. Nothing
orders it against lines sent on a WebSocket or USB, and its reply never appears on a stream. If
`$HTTP/BlockDuringMotion` is on, they are refused with 503 "Try again when not moving" while the
machine moves (`[ESP800]` excepted).

`PAGEID` only matters for lines injected into a WebSocket. Our UI no longer sends it: the only
lines it still sends with `commandText=` are `[ESP…]`, which run in the web server.

### `[ESP…]` commands are not HTTP-only

Every `[ESPnnn]` command also has a `$` name and works on any channel. `[ESP420]` over HTTP is the
same as `$System/Stats` over USB. The full ESP → `$` list is in the FluidNC wiki,
[Commands and Settings](http://wiki.fluidnc.com/en/features/commands_and_settings). Our firmware
registers them in `WebUI/WebCommands.cpp`.

The ones our UI sends: `[ESP800]` (firmware info, on connect), `[ESP400]`/`[ESP401]` (list and
set settings, WiFi ones included), `[ESP410]` (WiFi scan), `[ESP420]` (system stats),
`[ESP444]RESTART` and `[ESP200]` (SD status).

So what is really HTTP-only is **moving file contents** (upload, download), **firmware update** and
**login**. Listing, deleting and running files already work over the stream.

## 3. Line protocol (all channels)

### Lines and replies

- The client sends text lines ending in `\n`.
- The firmware answers **every line with exactly one `ok` or `error:N`**, in the order the lines
  were sent, on the channel that sent them. Other output (status reports, logs) can come in between.
  Two firmware limits (below) break this today, so a client must pace its lines and give up on a
  reply after a timeout.
- Codes are listed by `$E` (`$Errors/List`). The common ones:

| Code | Name | Typical cause |
|---|---|---|
| 3 | InvalidStatement | unknown `$` command |
| 8 | IdleError | `$J=` when not Idle or Jog |
| 9 | SystemGcLock | G-code in Alarm or Jog; Maslow motion while motion is blocked |
| 20 | GcodeUnsupportedCommand | unknown G/M code |
| 120 | AnotherInterfaceBusy | a job is running on another channel (below) |
| 130 | JogCancelled | |
| 152 | ConfigurationInvalid | Maslow command while running the default config |

### Pacing: when a line gets no reply

The "one reply per line" rule only holds if the client doesn't send too fast. One firmware limit
is still open, and a second one is fixed in newer firmware:

- **A full input queue drops whole lines with no reply.** Each channel holds at most
  `4 × maxLine` = 1020 bytes of unread input (`Channel::queue_push`). A line that arrives when the
  queue is full is discarded through its newline: no `ok`, no `error`, only a debug-level log line
  ("Input queue full on … dropping lines") that the UI doesn't show by default. Realtime bytes are
  not queued, so they always get through. This is the WebSocket path; USB has its own receive
  buffer in front of the queue, but the same rule applies.
- **Fixed after `exp1` commit `0d9585929`: WebSocket replies could be lost under bursts of
  output.** The output task (`WSChannel::write`) and the polling task (`WSChannel::pollLine` /
  `flush_output`) both touched the channel's pending output buffer (`_output_line`) without a
  shared lock. On a Maslow 4.1, 23 `$/` reads sent back-to-back lost about one reply in three;
  sent one at a time, none were lost. The fix puts a per-channel lock around that buffer. Older
  firmware still has the race, so a client that may meet it should pace as below.

So a client should still:

- **Send one line at a time:** send the next line only after the previous one's `ok` or
  `error:N`. GRBL-style character counting also avoids the input-queue limit: keep the bytes of
  unanswered lines under the free receive space. `Bf:blocks,bytes` in the status report (with
  `$Report/Status=2`) shows that space, capped at 256. On older firmware it does not avoid the
  output race.
- **Use a reply timeout.** A reply can still be lost, and a client that waits for it forever
  stalls. Most `$` commands, including all Maslow ones, wait for queued motion to finish before
  they answer (below), so the timeout should not count time while the machine is moving.

### When a line runs

| Kind | Runs | Notes |
|---|---|---|
| G-code | queued, in order | goes to the motion planner |
| `$J=…` jog | queued | does not wait for earlier motion to finish |
| "Report" `$` commands (`$I`, `$G`, `$SS`, `$CD`, `$Report/Interval`, …) | right away | read-only, never wait |
| Other `$` commands, including **all Maslow commands** | queued, and **wait until the planner is empty** | so `$CAL` after G-code waits for that G-code to finish |
| `[ESP…]` | queued, except the read-only status ones | |

**`ok` from a Maslow motion command means "started", not "finished".** `$ALL`, `$EXT`, `$TKSLK`,
`$CMP` and `$CAL` set the state machine going and return at once. The client learns that the
operation finished from the Maslow state changing (section 5.3) and log lines.

**A running job locks out the other channels.** While a file runs from one channel, any other
channel can only run the "right away" commands. Anything else gets `error:120`. Realtime bytes
still work from every channel.

### Realtime bytes

Single bytes, acted on the moment they arrive, even mid-line. **They get no reply.** Over a
WebSocket they are sent as a text frame. Bytes 0x80 and above arrive UTF-8 encoded (`C2 85`), and
the firmware decodes them.

| Byte | Name | Effect |
|---|---|---|
| `?` (0x3F) | StatusReport | send one status report (section 5.1) to this channel |
| `!` (0x21) | FeedHold | decelerate and hold (also when Idle, see the backlog) |
| `~` (0x7E) | CycleStart | resume from hold |
| 0x18 (Ctrl-X) | Reset | soft reset: stops at once, aborts any job, clears queues. **Maslow change:** stopped during a move or a hold it goes Idle with no alarm, where upstream raises `ALARM:3` (below) |
| 0x84 | SafetyDoor | |
| 0x85 | JogCancel | only in Jog state: stop the current jog and **drop this channel's queued lines** (Maslow change) |
| 0x87–0x8A | Macro0–3 | |
| 0x90–0x9E | feed / rapid / spindle overrides | |
| 0xA0, 0xA1 | coolant toggles | |

**Soft reset drops unread lines on every channel.** When the main loop handles the reset it
discards every channel's unread input, so those lines never get an `ok` or `error`. That includes
lines sent just after 0x18 that arrive before the reset is handled. The reset ends with the start
message (`Grbl … [FluidNC …]` by default, `$Start/Message`) on all channels. A client should
forget its pending lines when it sends or sees a reset, and wait for that message before sending
more.

One exception, from reading the code (not tested): a line the main loop has already taken can
still answer. A `$` command that is waiting for the planner to empty stops waiting when the reset
starts, then runs its action and sends its reply, before the reset finishes. So up to one stray
`ok`/`error:N` can follow a reset. It arrives before the start message, so a client that waits
for that message is not confused by it.

### Soft reset is Maslow's fast stop

Changed after `exp1` commit `0d9585929` (`protocol_do_rt_reset` in `Protocol.cpp`). What 0x18
does depends on the state when it arrives:

| State | Upstream FluidNC | Maslow |
|---|---|---|
| Run, Jog, or a feed hold (including a paused job) | stops stepping at once, then `ALARM:3` "Abort during cycle" (a stepper machine may have lost position) | stops stepping at once, makes the belt step counts match the encoders and resets the belt PIDs (`Maslow_::syncBeltsToEncoders()`), then goes **Idle, no alarm**: the encoders still know where the belts are |
| Home (Maslow retract, extend, calibrate run in this state) | homing fails: `ALARM:6` "Homing fail reset" | the same; motors stop |
| any other state | as upstream (Idle stays Idle, Alarm stays Alarm, Sleep alarms) | the same |

In every state the rest of the reset is as upstream: the job is aborted, spindle and coolant off,
belt motors off, every channel's unread input discarded, and the start message sent to all
channels.

### How a client should stop the machine

1. Send **0x18**. It is realtime, so it acts the moment it arrives, even behind queued lines or
   a running job, and it works from every channel.
2. Wait for the start message (`Grbl …`), with a timeout.
3. Send **`$STOP`**. It raises Z to 2 mm if lower and saves positions. Sending it before the
   restart would be useless: the reset discards unread input, so `$STOP` would be dropped
   unanswered.

`$STOP` alone is not a fast stop: it is queued like other `$` commands, so it waits behind
running motion. 0x85 stops only a jog (`!` holds, it doesn't stop). Our UI still uses 0x85 for
releasing a hold-to-jog button, where only the jog should end.

This is built but not yet tested on a machine.

### Our UI: what goes where, and reply matching

All senders in the UI (`SendPrinterCommand` in `printercmd.js`, `SendRealtimeCmd` in `grbl.js`, the
command box in `commands.js`) split a command by kind:

| Kind | Sent over | Reply |
|---|---|---|
| `[ESP…]` | HTTP `/command?commandText=` | HTTP body |
| one realtime byte (`?`, `!`, `~`, 0x18, 0x80 and above) | WebSocket, `wsCommands.sendRealtime` | none expected |
| every other line: G-code, `$J=`, `$H…`, Maslow `$` commands, `$/path` reads and `$/path=value` writes, `$G`, `$SS`, `$MINFO`, `$GSTATE`, `$Report/Interval`, `$SD/Run`, … | WebSocket, `wsCommands.send` | that line's `ok` / `error:N` on the socket; a read's value (e.g. `$/path=value`) arrives as a line before the `ok` |

Still over HTTP on purpose, because the caller needs the reply in the HTTP body: `[ESP…]` (WiFi
settings, `[ESP400]`/`[ESP401]`, `[ESP420]`, `[ESP444]RESTART`, `[ESP800]`, `[ESP200]`), and
`plain=` requests: the Maslow config reads in `maslow.js` (`loadConfigValues`), `$CO` and setting
writes in `settings.js`. Plus files, firmware update and login. These run in the web server, so
they are not ordered with the socket's lines (section 2).

**Pacing and reply matching.** `wscommands.js` is the only sender of lines and the only matcher.
It keeps **one line on the wire** at a time. A line sent while another is waiting goes into a
queue in the UI and goes out when the previous one's `ok` or `error:N` arrives, which runs that
line's success or error callback. Other lines (status reports, logs, `MINFO`) never count as
replies. Every line on the tab's channel goes through it, so each reply is matched to its own
line. Realtime bytes are never queued: they go out at once, even while a line is waiting.

- **Reply timeout:** 5 s (`REPLY_TIMEOUT_MS`). If no reply arrives, the UI reports "No reply from
  the machine to '…'; carrying on" and sends the next line. The clock pauses while status reports
  show Run, Jog, Home, Hold or Door, because `$` commands wait for queued motion before
  replying.
- **Jog cancel** (0x85, in Jog state) drops the lines queued in the UI, as the firmware drops that
  channel's unread lines. **Soft reset** (0x18) drops the line on the wire and everything queued
  (see above).
- **Stop button** (`wsCommands.stop()`): sends 0x18, waits up to 1.5 s for a line starting
  `Grbl ` (the restart banner), then sends `$STOP` ahead of anything queued. If the banner doesn't
  come in time (or `$Start/Message` was changed so it doesn't start with `Grbl `), it sends
  `$STOP` anyway when the wait ends. This replaces the old 0x85 + `$STOP` for jogs and
  `!` + `$STOP` for running jobs.
- **Reconnect:** the queue is emptied (a new socket is a new channel). If the socket isn't open,
  nothing is sent: the UI shows "Not sent: '…'" and the error callback runs. Lines are never held
  over a disconnect, because a jog replayed after a reconnect would be dangerous.
- `?` is also sent every second as a heartbeat; the UI shows the connection as quiet when nothing
  arrives for a while (section 5.1).

## 4. Commands

### 4.1 Standard FluidNC / GRBL commands

These work as on any FluidNC machine. The useful ones for a Maslow client:

| Command | What it does |
|---|---|
| G-code lines | motion (G0/G1/G2/G3), units, offsets, … |
| `$J=G91 G21 F1000 X10` | jog (relative jog shown) |
| `$H`, `$HX`, `$HY`, `$HZ`, `$HA` | home (the UI uses `$HX`/`$HY`/`$HZ`/`$HA`) |
| `$X` | clear the alarm lock |
| `$G` | modal state, reply `[GC:G0 G54 G17 G21 G90 …]` |
| `$#` | offsets, `[G54:…]` lines |
| `$I` | build info, includes the firmware version and commit |
| `$SS` | replay the startup log |
| `$CD` | dump the running config as YAML |
| `$/path` / `$/path=value` | read / set a config value, e.g. `$/kinematics/MaslowKinematics/fixedZ` |
| `$Report/Interval=ms` | set this channel's status auto-report interval (0 = off, minimum 50; a WebSocket starts at 200, USB off) |
| `$Report/Status=n` | 1 = report machine position (MPos), 0 = work position (WPos); the default is 1 |
| `$SD/Run=file` / `$LocalFS/Run=file` | run a G-code file |
| `$MD` / `$ME` | disable / enable motors |
| `$CMD`, `$S`, `$E`, `$A` | list commands, settings, errors, alarms |

### 4.2 Maslow commands

All Maslow commands are registered in one block at the end of `make_user_commands()` in
[`ProcessSettings.cpp`][reg], which maps each short name (`$ALL`) and long name
(`$Maslow/retract`) to a handler. Each command below links to its handler. Most handlers are a few
lines that call into `Maslow.cpp` or `Calibration.cpp`.

All are registered as allowed in any state, then refuse inside the handler. Most return
`error:152` when the machine runs the default config. The motion ones also refuse with `error:9`
plus a log line `Motion is disabled: <reason>` when Maslow has blocked motion.

**Belts and calibration (these move the arms):**

| Command | Name | What it does | UI sends it |
|---|---|---|---|
| [`$ALL`][ALL] | `Maslow/retract` | retract all belts (state 1 → 2) | yes |
| [`$EXT`][EXT] | `Maslow/extend` | extend all belts (3 → 4) | yes |
| [`$TKSLK`][TKSLK] | `Maslow/takeSlack` | take up slack and find the frame (5) | yes |
| [`$CMP`][CMP] | `Maslow/comply` | release tension (8) | yes |
| [`$CAL`][CAL] | `Maslow/calibrate` | run calibration (6) | yes |
| [`$TLI`][TLI] [`$TRI`][TRI] [`$BLI`][BLI] [`$BRI`][BRI] | `Maslow/calibrateXXI` | pull that arm's belt in at full power for 200 ms (clears a jam) | no |
| [`$TLO`][TLO] [`$TRO`][TRO] [`$BLO`][BLO] [`$BRO`][BRO] | `Maslow/calibrateXXO` | push that arm's belt out for 200 ms | no |
| [`$SETZSTOP`][SETZSTOP] | `Maslow/setZStop` | make the current Z the Z stop | yes |

**Stopping:**

| Command | Name | What it does | UI sends it |
|---|---|---|---|
| [`$STOP`][STOP] | `Maslow/stop` | stop belts, spindle and any job; sync positions; raise Z to 2 mm if lower; save state. Queued like other `$` commands, so it waits behind running motion (backlog). The UI sends 0x18 first and `$STOP` after the restart (section 3, how to stop) | yes |
| [`$ESTOP`][ESTOP] | `Maslow/estop` | emergency stop: Alarm state, motors and spindle off; the machine ignores further commands until power-cycled | no |

**State and diagnostics (read-only):**

| Command | Name | What it does | UI sends it |
|---|---|---|---|
| [`$MINFO`][MINFO] | `Maslow/getInfo` | reply `MINFO: {json}` (section 5.2), sent to **every** channel | yes |
| [`$GSTATE`][GSTATE] | `Maslow/gstate` | log `Current state: N` (section 5.3) | no (the state is in every status report) |
| [`$MBOARD`][MBOARD] | `Maslow/board` | log the board name and version | no |
| [`$MDIAG`][MDIAG], [`$MADC`][MADC], [`$TEST`][TEST] | `Maslow/diag`, `Maslow/adc`, `Maslow/test` | diagnostics as log lines | `$TEST` |

**Settings and storage:**

| Command | Name | What it does | UI sends it |
|---|---|---|---|
| [`$CO`][CO] | `Config/Overwrite` | write the running config to the config file (`maslow.yaml`) | yes |
| [`$CALRESET`][CALRESET] | `Maslow/resetCalibration` | forget calibration, go to state 4 (refused from READY_TO_CUT, see the backlog) | no |
| [`$SFON`][SFON] / [`$SFOFF`][SFOFF] | `Maslow/safetyON` / `safetyOFF` | turn Maslow's `safety_control()` checks (run on every update) on / off | no |
| [`$TELEM[=0\|1]`][TELEM], [`$TELEMDUMP[=file]`][TELEMDUMP] | `Maslow/setTelemetry`, `Maslow/telemetryDump` | record / dump arm telemetry | no |

[reg]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1622-L1656
[MDIAG]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1164
[MADC]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1184
[ALL]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1210
[EXT]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1222
[STOP]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1234
[TELEMDUMP]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1263
[TELEM]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1271
[GSTATE]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1283
[CMP]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1289
[CAL]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1298
[CO]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1371
[TLO]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1383
[TRO]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1394
[BLO]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1405
[BRO]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1416
[TLI]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1427
[TRI]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1438
[BLI]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1449
[BRI]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1460
[SFOFF]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1472
[SFON]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1480
[TEST]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1488
[TKSLK]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1495
[CALRESET]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1507
[ESTOP]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1516
[SETZSTOP]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1523
[MBOARD]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1531
[MINFO]: https://github.com/MaslowCNC/Maslow_4/blob/3448d81fdf596451218280f18a7ec3e971985f5e/firmware/FluidNC/src/ProcessSettings.cpp#L1537

## 5. Messages from the machine

| Message | When | Sent to |
|---|---|---|
| `ok` / `error:N` | one per line received | the channel that sent the line |
| `<…>` status report | on `?`, or by auto-report | the channel that asked or subscribed |
| `[GC:…]` | on `$G`, and by auto-report when the modal state changes | that channel |
| `MINFO: {…}` | on `$MINFO` | **all channels** |
| `[MSG:INFO: …]`, `[MSG:WARN: …]`, `[MSG:ERR: …]` | events | **all channels** (each channel filters by its own message level) |
| `ALARM:N` | on alarm | all channels |
| bare `PING` | every 10 s on WebSockets | each WebSocket |

### 5.1 Status report

```
<State|N:seq|MPos:x,y,z[|Bf:blocks,bytes][|Ln:n]|FS:feed,spindle|MS:n[|Pn:pins][|WCO:x,y,z][|Ov:f,r,s[|A:SCFM]]|BL:tl,tr,bl,br|BE:tl,tr,bl,br|MC:tl,tr,bl,br[|SD:pct,file[,line]]>
```

Example while jogging, a report that carries the belts:

```
<Jog|N:1234|MPos:120.000,85.500,0.000|FS:1000,0|MS:7|BL:1800.25,1801.50,1900.75,1899.00|BE:0.012,-0.034,0.000,0.500|MC:412,398,120,131>
```

| Field | Always? | Meaning |
|---|---|---|
| State | yes | `Idle`, `Run`, `Hold:0` (held) / `Hold:1` (stopping), `Jog`, `Home`, `Alarm`, `Check`, `Door:0..3`, `Sleep`, `Starting` |
| `MPos:x,y,z` | yes, unless `$Report/Status=0` | machine position. Maslow always reports the position worked out from the belt lengths, even in Home state, where upstream would report raw motor positions |
| `WPos:x,y,z` | instead of MPos when `$Report/Status=0` | work position |
| `FS:feed,spindle` | yes | current feed rate (mm/min, or in/min with `$Report/Inches`) and spindle speed |
| `Bf:blocks,bytes` | only with `$Report/Status=2` or `3` | free planner blocks, free receive buffer bytes |
| `Ln:n` | only with line numbers on | line number of the running block |
| `Pn:XYZPDHRS` | when a pin is active | limit / probe / control pins |
| `WCO:x,y,z` | every 10th report when idle, every 30th when busy, or when it changes | work offset; WPos = MPos − WCO |
| `Ov:feed,rapid,spindle` | every 10th / 20th report | overrides in % |
| `A:SCFM` | with Ov, when on | S/C spindle CW/CCW, F flood, M mist |
| `MS:n` | yes (Maslow) | Maslow calibration state, 0–9 (section 5.3) |
| `N:seq` | yes (Maslow, temporary) | this channel's report sequence number, +1 per report; a jump means reports were dropped. For measuring delivery (`scripts/report_monitor.mjs`) |
| `BL:tl,tr,bl,br` | yes (Maslow) | belt lengths, mm, 2 decimals; `?` for a belt whose length is unknown (not restored at boot and not retracted since) |
| `BE:tl,tr,bl,br` | yes (Maslow) | each belt's position error (encoder position minus target), mm, 3 decimals |
| `MC:tl,tr,bl,br` | yes (Maslow) | belt motor currents, raw ADC 0–4095 averaged over the last 10 samples (the UI converts: mA = 1000 × 3.3 × ADC / 4095 / 1.5) |
| `SD:pct,file[,line]` | while a job runs | progress %, file name, and (Maslow addition) the file line of the running block |

**Maslow fields** (added after `exp1` commit `0d9585929`, on branch `machine-api`; not yet on a
machine):

- `MS` is in every report, and a change of Maslow state triggers an auto-report at once, so an
  idle machine reports a new state without being asked.
- `BL`, `BE` and `MC` list the arms in the firmware's order **TL, TR, BL, BR**. Careful: `MINFO`
  uses `tl, tr, br, bl` (section 5.2).
- `BL`, `BE` and `MC` are in every report, so belt lengths keep up with the position. Every
  client gets the same fields; a per-channel choice of fields can come later if a client (e.g.
  an ESP-NOW pendant) needs smaller reports.

**Clients must ignore fields they don't know.** That is the GRBL convention (the UI's parser in
`grbl.js` does), and it is what lets us add fields later.

### When reports are sent

- `?` sends one report right away, in any state.
- With auto-report on (`$Report/Interval=ms`), the firmware sends that channel a report:
  - every `ms` milliseconds **while moving** (Run, Home or Jog), and
  - otherwise **only when something changes**: the state, the Maslow state (`MS`), an override,
    the work offset, a pin, or a job starting or ending.

  So an idle machine with auto-report on goes quiet. A client that needs a heartbeat has to send `?`
  itself.
- The setting lasts as long as that channel and is not saved. Every new connection is a new
  channel with the default: **a WebSocket starts with auto-report on at 200 ms** (set in the
  `WSChannel` constructor). **USB and Telnet start with it off**, so those clients must turn it on
  after each (re)connect.

**Our UI** sends these each time its WebSocket opens, the first time and on every reconnect:
`$Report/Interval=50` (faster than the 200 ms default), `?`, `$G`, `$MINFO`, and the corner,
`fixedZ` and park setting reads. `$SS` (replay the startup log) is sent only on the first open. (Before this change they were sent only at page load, so a reconnected socket stayed at
200 ms and the Maslow state wasn't re-read.) After that, `wscommands.js` sends `?` every second
as a heartbeat.

### 5.2 `MINFO`

```
MINFO: { "homed": true, "calibrationInProgress": false, "tl": 1823.4, "tr": 1822.9, "br": 1901.2, "bl": 1900.7,
         "etl": 0.01, "etr": -0.02, "ebr": 0.00, "ebl": 0.01, "extended": true }
```

`tl`/`tr`/`br`/`bl` are belt lengths in mm. `e…` is each arm's position error (encoder position
minus target) in mm. Only sent in reply to `$MINFO`, and to every channel.

### 5.3 Maslow state

The calibration state machine (`MaslowEnums.h`):

| N | State | N | State |
|---|---|---|---|
| 0 | UNKNOWN | 5 | TAKING_SLACK |
| 1 | RETRACTING | 6 | CALIBRATION_IN_PROGRESS |
| 2 | RETRACTED | 7 | READY_TO_CUT |
| 3 | EXTENDING | 8 | RELEASE_TENSION |
| 4 | EXTENDEDOUT | 9 | CALIBRATION_COMPUTING (never reached) |

Clients learn it from the **`MS:n` field of the status report** (section 5.1). It is in every
report, and a state change sends an auto-report at once, so a client with auto-report on sees each
change, and any client can ask with `?`.

The log line `[MSG:INFO: Current state: N]` is still printed (to all channels when the state
changes, and on `$GSTATE`) for people reading the log. Our UI no longer reads it: it takes the
state from `MS` only. Older firmware (before `MS`) has only this line.

## 6. Gaps for USB and ESP-NOW

- **Logs and `MINFO` go to every channel.** That's fine for a browser, but too much for ESP-NOW.
  A pendant channel should default to warnings and errors only, and `MINFO` should reply only to
  the channel that asked.
- **Success or failure of a Maslow operation is only in log text.** `ok` means "started"; `MS`
  in the status report now shows when the state changes, but not whether the operation succeeded
  (e.g. a calibration that failed and why).
- **The Maslow fields cost bytes.** `N`, `BL`, `BE` and `MC` add about 100 bytes to every report
  (roughly 60 → 160 bytes). Frames, not bytes, are what the ESP32 is short of, and the count
  doesn't change; `scripts/report_monitor.mjs` measures size, drops and gaps. An ESP-NOW pendant
  may want fewer fields, which would need a per-channel setting.
- **Moving file contents and firmware updates need HTTP.** Over USB, either add stream commands for
  them or show a reduced UI.
- **Several controllers at once.** Commands from all channels share one queue. A job locks other
  channels out (`error:120`). 0x85 only drops the sender's queued lines. We need to decide whether
  that's the policy or whether one client holds control.
- **Deadman timer.** Nothing stops a jog if its client goes silent.
- **Lines can go unanswered** (section 3, pacing). The input queue drops whole lines silently
  when full (the WebSocket output race that also lost replies is fixed). Every client (USB, a
  pendant) must send one line at a time and use a reply timeout until the firmware at least
  reports a dropped line. Over ESP-NOW, a lost radio packet adds another way
  to lose a line or reply, so the pendant needs the timeout anyway.
- **Two tabs in one browser kick each other off.** They share a session cookie, and the newest
  socket for a session closes the older ones, which then reconnect, forever. Opening the socket
  with `?independent_session` (supported by our firmware) keys the session on IP:port instead.
  Now that the UI routes no line through `/command` by cookie, that is safe to turn on.
- **Reconnect has no backoff.** The UI retries every 3 s forever.
- **Some reads still use HTTP.** The Maslow config reads in `loadConfigValues`, `$CO` and the
  setting writes in `settings.js` want the reply in one piece, so they use `plain=`. Multi-line
  reads like `$CD` would need the same. Moving them to the socket needs a "collect lines until
  `ok`" helper with a timeout. Until then they run outside the channel's ordering (section 2).
- **A reply can follow a reset** (section 3, soft reset). A client that sends a new line right
  after 0x18 could match that stray reply to it. Waiting for the start message avoids it.
