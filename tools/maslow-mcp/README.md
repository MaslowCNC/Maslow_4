# Maslow MCP server

An [MCP](https://modelcontextprotocol.io) server that lets Claude Code debug a Maslow machine
from the computer. It runs on the computer, not on the machine. It connects to the XY board over
telnet (commands and logs) and HTTP (files and firmware updates). It reaches the spindle board
through the shared serial hub (`spindle/tools/serial_hub.py`). No firmware changes are needed.

It is registered in the repo's `.mcp.json`, so Claude Code offers it when you open this repo. The
first start creates `tools/maslow-mcp/.venv` and installs `mcp` and `requests`, which needs
Python 3.10 or newer.

## Tools

| Tool | What it does | Gate |
|---|---|---|
| `machine_status` | Connection info plus the parsed `?` report: state, position and pins. | none |
| `send_command` | Sends one line and returns the reply up to `ok`/`error`. | depends on the command (see below) |
| `read_log` | Timestamped output from our telnet channel, including `[MSG:…]` lines caused by anyone. Filter with a regex or a time window. | none |
| `status_history` | The `?` reports the server polls in the background (every 0.5 s while running, 2 s otherwise): state, position, feed and spindle speed over time. Shows the last N seconds, or a window around a time taken from `read_log`. | none |
| `get_settings` / `get_config` | `$S` and `$CD`, optionally filtered by a regex. | machine idle |
| `firmware_info` | `[ESP800]` version string, which includes the branch and commit. | none |
| `list_files` / `download_file` | Reads the board's flash filesystem. Downloads go to `logs/downloads/`. | none |
| `upload_file` | Uploads to the flash filesystem. | `--allow-motion`, machine idle |
| `flash_firmware` | Runs `pio run -e <env>`, POSTs the result to `/updatefw`, waits for the reboot and reports the new version. | `--allow-motion`, machine idle |
| `flash_spindle` | Builds the spindle's OTA env and sends `[ESP810]` (`$Spindle/EnableOTA`). It waits for the XY log to report the spindle's WiFi address, then pushes the firmware with espota. A failed transfer makes the spindle reboot and offer OTA again, so the upload is retried once at the new address. If WiFi fails, it falls back to USB with `spindle/tools/flash.sh`. | `--allow-motion`, machine idle |
| `feed_hold` / `soft_reset` | `!` and Ctrl-X. These stop the whole machine, including your job. | none |
| `cycle_start` | `~` | `--allow-motion` |
| `spindle_command` | Sends a line to the spindle board through the hub and returns its output. | read-only commands are free, the rest need `--allow-motion` |
| `spindle_log` | Tail of the newest `spindle/logs/hub-*.log`, optionally filtered by a regex. | none |
| `spindle_trip` | Diagnoses a spindle over-current or driver-fault stop from the spindle's trip recording, adding the XY messages and status reports from around the trip. `fetch` asks the spindle to send the recording again; `test` records a test one, with no motion. `source="hub"` reads the recording from the USB hub log instead. | `fetch`/`test`: machine idle |

## Spindle trip recordings

The spindle board keeps the last ~2 s of motor state in RAM: per-phase currents, the filtered
protection current, voltage limit, load-sense Iq and lag, FOC loop rate, nFAULT retries and Z
phase. When an over-current or driver fault stops the motors, it freezes that window. It prints
the window on USB and also sends it over the inter-board link, one line every 20 ms (~20 s in all).
The XY board saves it to its flash as `/spindle_trip.csv`. It only saves while the machine is not
moving, which is always true after a trip because the fault raises an alarm. `$Spindle/Trip`
(`[ESP813]`) asks the spindle to send its recording again. `$Spindle/Trip=TEST` re-arms the
recorder and records a test trip 2 s later. The spindle refuses the test while it holds a real
trip, so a test cannot discard one.

`spindle_trip` downloads the file and reports whether the current climbed (a real overload) or
jumped (a commutation step, a bad reading or a jam). It also reports whether Iq fell to the
pull-out edge, whether the FOC loop stalled, whether the driver's own protection fired and whether
the Z was moving. The XY context only covers the time this server was running.

## Using it while someone drives the machine

FluidNC gives every connection its own channel, so the server can run alongside the WebUI.
Replies to `?` and `ok`/`error` go only to whoever sent the command. Log messages go to everyone.
FluidNC does mix lines from all channels into one G-code stream, so the server only sends
commands when that is safe:

- **Light reads** are allowed in any state, even mid-job: `$G`, `$I`, `$A`, `$E`, `$Heap`,
  `$T`, `$CI`, `$Maslow/gstate`, `$Maslow/getInfo`, `[ESP800]`, `[ESP420]` and `[ESP111]`.
- **Heavy reads** are only sent when the machine is Idle, Alarm or Sleep, so a big dump never
  competes with a running job: `$CD`, `$S`, `$SC`, `$$`, `$L`, `$CMD`, `$SS`, `$#`, `$N`,
  `$GD`, `$Limits`, `$V`, `[ESP400]` and `$` (help).
- **Everything else** counts as a write. That includes G-code, `$X`, `$H`, `$Maslow/*` actions,
  setting writes, uploads and flashing. Writes are refused unless the server was started with
  `--allow-motion`. Even then they are only sent when the machine is Idle or Alarm.

Maslow belt operations (retract, extend, calibrate) put FluidNC in `Home`, so the server treats
the machine as busy while they run.

## Allowing motion

Motion is off by default. To turn it on, start Claude Code with the variable set:

```bash
MASLOW_ALLOW_MOTION=1 claude
```

Alternatively, change `MASLOW_ALLOW_MOTION` in `.mcp.json`. Then restart the server from
Claude Code's MCP menu (`/mcp`). `machine_status` reports `motion_allowed`, so you can check
the setting.

## Configuration

These are environment variables, set in `.mcp.json` or your shell:

| Variable | Default |
|---|---|
| `MASLOW_HOST` | `maslow.local` (or an IP address) |
| `MASLOW_TELNET_PORT` | `23` (telnet must be enabled: `$Telnet/Enable=ON`, which is the default) |
| `MASLOW_HTTP_PORT` | `80` |
| `SPINDLE_HUB` | `127.0.0.1:4567` |

The XY log for each server run goes to `logs/xy-YYMMDD-HHMMSS.log`. Commands the server sends
appear in it as `>> CMD`. The polled status reports go to `logs/status-YYMMDD-HHMMSS.log`.

## Testing without the machine

```bash
tools/maslow-mcp/.venv/bin/python tools/maslow-mcp/tests/test_fake.py
```

This starts `tests/fake_board.py`, a stand-in FluidNC with telnet and HTTP. It then runs the
server over stdio and calls every tool, including the busy and motion gates.
`tests/fake_pio.sh` and `tests/fake_flash.sh` stand in for PlatformIO and the USB flash script.
They let the test cover `flash_spindle`'s WiFi path, its retry and its USB fallback. The test
points the server at them with `MASLOW_PIO` and `SPINDLE_FLASH_SH`.
