# Maslow connection protocol (draft)

How a client talks to a Maslow 4 / 4.1: the browser UI over WiFi today, and later a USB-connected
browser and an ESP-NOW pendant. This is a first pass that describes what the firmware does now
(branch `exp1`, commit `0d9585929`), not a finished spec. Where behaviour looks wrong it is noted,
not changed.

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

## 2. HTTP-only features

These need the web server, so they are not available over USB or ESP-NOW today.

| Endpoint | Method | What it does | Used by the UI |
|---|---|---|---|
| `/` | GET | serves `index.html.gz` (the UI) | yes |
| `/<path>` | GET | any other path downloads that file from the flash filesystem, e.g. `/maslow.yaml` | yes |
| `/files` | GET | flash filesystem: list; `action=delete\|deletedir\|createdir\|rename` with `filename=` | yes |
| `/files` | POST multipart | upload to the flash filesystem (config files, `index.html.gz`) | yes |
| `/upload` | GET / POST | the same for the SD card (G-code files) | yes |
| `/flash/…`, `/sd/…` | WebDAV | file access for desktop clients | `/sd/config` only |
| `/updatefw` | POST multipart | firmware update (OTA); reboots when done | yes |
| `/login` | GET / POST | authentication (off in our build) | yes |
| `/command?commandText=…` | GET | sends one line into **this tab's WebSocket channel** (found by `PAGEID` or the session cookie); the reply arrives on the socket, the HTTP body is empty, 500 "WebSocket dead" if the socket is gone | yes, being phased out |
| `/command?cmd=[ESPnnn]…` or `$/…` | GET | runs the command inside the web server; the reply is in the HTTP body | yes |
| `/command?plain=…` | GET | same as above for any command | scripts |
| `/command_silent` | GET | `/command` without echo | no |
| `/feedhold_reload`, `/cyclestart_reload`, `/restart_reload`, `/did_restart` | GET | feed hold / cycle start / reset, then redirect (for bookmarks) | no |
| `/trace?msg=` | GET | prints a browser debug message on USB serial | debug |

### `[ESP…]` commands are not HTTP-only

Every `[ESPnnn]` command also has a `$` name and works on any channel. `[ESP420]` over HTTP is the
same as `$System/Stats` over USB. The full list:

| ESP | `$` name | ESP | `$` name |
|---|---|---|---|
| ESP0 | `$WebUI/Help` | ESP210 | `$SD/List` |
| ESP100 | `$Sta/SSID` | ESP215 | `$SD/Delete` |
| ESP101 | `$Sta/Password` | ESP220 | `$SD/Run` (run a G-code file) |
| ESP102 | `$Sta/IPMode` | ESP221 | `$SD/Show` |
| ESP103 | `$Sta/Setup` | ESP400 | `$WebUI/List` (UI settings as JSON) |
| ESP105 | `$AP/SSID` | ESP401 | `$WebUI/Set` |
| ESP106 | `$AP/Password` | ESP410 | `$WiFi/ListAPs` |
| ESP107 | `$AP/IP` | ESP420 | `$System/Stats` |
| ESP108 | `$AP/Channel` | ESP444 | `$System/Control` (`RESTART`) |
| ESP111 | `$System/IP` | ESP555 | `$WebUI/SetUserPassword` |
| ESP112 | `$Hostname` | ESP600 | `$Notification/Send` |
| ESP115 | `$Radio/State` | ESP610 | `$Notification/Setup` |
| ESP116 | `$WiFi/Mode` | ESP700 | `$LocalFS/Run` |
| ESP120 / 121 | `$HTTP/Enable` / `Port` | ESP710 | `$LocalFS/Format` |
| ESP130 / 131 | `$Telnet/Enable` / `Port` | ESP720 | `$LocalFS/Size` |
| ESP140 / 141 | `$Bluetooth/Name` / `Enable` | ESP800 | `$Firmware/Info` |
| ESP200 | `$SD/Status` | ESP810–813 | `$Spindle/…` (not used on Maslow) |

So what is really HTTP-only is **moving file contents** (upload, download), **firmware update** and
**login**. Listing, deleting and running files already work over the stream.

## 3. Line protocol (all channels)

### Lines and replies

- The client sends text lines ending in `\n`.
- The firmware answers **every line with exactly one `ok` or `error:N`**, in the order the lines
  were sent, on the channel that sent them. Other output (status reports, logs) can come in between.
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
| 0x18 (Ctrl-X) | Reset | soft reset: stops everything, clears queues |
| 0x84 | SafetyDoor | |
| 0x85 | JogCancel | only in Jog state: stop the current jog and **drop this channel's queued lines** (Maslow change) |
| 0x87–0x8A | Macro0–3 | |
| 0x90–0x9E | feed / rapid / spindle overrides | |
| 0xA0, 0xA1 | coolant toggles | |

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
| `$Report/Interval=ms` | turn status auto-reports on for this channel (0 = off, minimum 50) |
| `$Report/Status=n` | 1 = report machine position (MPos), 0 = work position (WPos); the default is 1 |
| `$SD/Run=file` / `$LocalFS/Run=file` | run a G-code file |
| `$MD` / `$ME` | disable / enable motors |
| `$CMD`, `$S`, `$E`, `$A` | list commands, settings, errors, alarms |

### 4.2 Maslow commands

All are registered as allowed in any state, then refuse inside the handler. Most return
`error:152` when the machine runs the default config. The motion ones also refuse with `error:9`
plus a log line `Motion is disabled: <reason>` when Maslow has blocked motion.

**Belts and calibration (these move the arms):**

| Command | Name | What it does | UI sends it |
|---|---|---|---|
| `$ALL` | `Maslow/retract` | retract all belts (state 1 → 2) | yes |
| `$EXT` | `Maslow/extend` | extend all belts (3 → 4) | yes |
| `$TKSLK` | `Maslow/takeSlack` | take up slack and find the frame (5) | yes |
| `$CMP` | `Maslow/comply` | release tension (8) | yes |
| `$CAL` | `Maslow/calibrate` | run calibration (6) | yes |
| `$TLI` `$TRI` `$BLI` `$BRI` | `Maslow/calibrateXXI` | pull that arm's belt in at full power for 200 ms (clears a jam) | no |
| `$TLO` `$TRO` `$BLO` `$BRO` | `Maslow/calibrateXXO` | push that arm's belt out for 200 ms | no |
| `$SETZSTOP` | `Maslow/setZStop` | make the current Z the Z stop | yes |

**Stopping:**

| Command | What it does | UI sends it |
|---|---|---|
| `$STOP` | stop belts, spindle and any job; sync positions; raise Z to 2 mm if lower; save state. The UI sends 0x85 first so jogs stop at once. Queued like other `$` commands, so it waits behind running motion (backlog) | yes |
| `$ESTOP` | emergency stop: Alarm state, motors and spindle off; the machine ignores further commands until power-cycled | no |

**State and diagnostics (read-only):**

| Command | What it does | UI sends it |
|---|---|---|
| `$MINFO` | reply `MINFO: {json}` (section 5.2), sent to **every** channel | yes |
| `$GSTATE` | log `Current state: N` (section 5.3) | yes |
| `$MBOARD` | log the board name and version | no |
| `$MDIAG`, `$MADC`, `$TEST` | diagnostics as log lines | `$TEST` |

**Settings and storage:**

| Command | What it does | UI sends it |
|---|---|---|
| `$CO` | write the running config to the config file (`maslow.yaml`) | yes |
| `$CALRESET` | forget calibration, go to state 4 (refused from READY_TO_CUT, see the backlog) | no |
| `$SFON` / `$SFOFF` | turn Maslow's `safety_control()` checks (run on every update) on / off | no |
| `$TELEM[=0\|1]`, `$TELEMDUMP[=file]` | record / dump arm telemetry | no |

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
<State|MPos:x,y,z|FS:feed,spindle[|Bf:blocks,bytes][|Ln:n][|Pn:pins][|WCO:x,y,z][|Ov:f,r,s[|A:SCFM]][|SD:pct,file[,line]]>
```

Example while jogging:

```
<Jog|MPos:120.000,85.500,0.000|FS:1000,0|WCO:0.000,0.000,0.000>
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
| `SD:pct,file[,line]` | while a job runs | progress %, file name, and (Maslow addition) the file line of the running block |

Nothing Maslow-specific is in the report yet: no Maslow state, no belt lengths.

**Clients must ignore fields they don't know.** That is the GRBL convention (the UI's parser in
`grbl.js` does), and it is what lets us add fields later.

### When reports are sent

- `?` sends one report right away, in any state.
- With `$Report/Interval=ms` set on a channel, the firmware sends that channel a report:
  - every `ms` milliseconds **while moving** (Run, Home or Jog), and
  - otherwise **only when something changes**: the state, an override, the work offset, a pin, or
    a job starting or ending.

  So an idle machine with auto-report on goes quiet. A client that needs a heartbeat has to send `?`
  itself (`wscommands.js` sends one every second).
- The UI turns auto-report on with `$Report/Interval=50` at page load. The setting lasts as long as
  that channel and is not saved.

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

The only way to learn it is the log line `[MSG:INFO: Current state: N]`. It is sent to all channels
when the state changes, and on `$GSTATE`. A client that misses the line has the wrong state until it
asks again. Moving this into the status report is the first planned change (section 6).

## 6. Gaps for USB and ESP-NOW

- **State isn't in the status report.** Clients that can't afford to parse log text (the pendant)
  have no reliable way to follow the Maslow state. Proposed: a `|MS:N` field, plus belt lengths at
  a lower rate.
- **Logs and `MINFO` go to every channel.** That's fine for a browser, but too much for ESP-NOW.
  A pendant channel should default to warnings and errors only, and `MINFO` should reply only to
  the channel that asked.
- **Completion of Maslow operations is only visible as log text** (because `ok` means "started").
- **Moving file contents and firmware updates need HTTP.** Over USB, either add stream commands for
  them or show a reduced UI.
- **Several controllers at once.** Commands from all channels share one queue. A job locks other
  channels out (`error:120`). 0x85 only drops the sender's queued lines. We need to decide whether
  that's the policy or whether one client holds control.
- **Deadman timer.** Nothing stops a jog if its client goes silent.
