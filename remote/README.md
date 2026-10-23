# Maslow 4.1 ESP-NOW remote

Firmware for a handheld remote built on the ESP32-2432S028R "Cheap Yellow
Display" (sold by ELEGOO and others: ESP32-WROOM, 2.8" 240x320 ILI9341,
XPT2046 resistive touch, USB-C). It talks to the Maslow over ESP-NOW using
the FluidNC `ESPNowChannel` (`firmware/FluidNC/esp32/ESPNow`), so it needs
no WiFi network and keeps working when the WebUI is closed.

## Screens

The top bar always shows the machine state, the Maslow state, the link
status and a **STOP** button. STOP fires as soon as you touch it and sends
`$STOP`, retrying every 300 ms until the machine reports Idle or Alarm (the
same as the WebUI).

| Tab | What it does |
| --- | --- |
| **Jog** | Work and machine position. Step jogging in X/Y (8 directions) and Z. The center button goes to XY home. Step sizes and mm/in are on the bottom row. **Home** opens: define XY home here, set XY home position, set Z home here or to a value, go to XY home, go to Z home, and Park. Tap an axis row to set that work coordinate. XY moves need the Ready to Cut state, and moves that leave machine Z outside 0–72 mm ask first, as in the WebUI. |
| **Job** | SD card file list. Start (with confirmation), Pause/Resume, Delete, progress bar and feed override. |
| **Setup** | Maslow state, Retract All, Extend All, Apply Tension, Release Tension, Find Anchors, Set Z-Stop, Test, Park, Unlock and **Settings** (work area, park position, scale, thicknesses, tension limits, retraction force, WiFi SSID/password; numeric changes are saved to maslow.yaml with `$CO`). While Find Anchors runs, this tab shows its progress messages. |
| **Log** | Machine messages, a keyboard to send any command, and a list of quick commands. |
| **More** | Unlock, spindle off, feed/rapid/spindle overrides, config macros, jog speeds, brightness, touch calibration, link info, pair or forget the machine. |

Warnings that pop up in the WebUI (Apply Tension deviation and retraction
warnings, Z home reset, Zm invalid, calibration complete) pop up here too.
The RGB LED is green when connected, red in Alarm or when disconnected,
and blinks blue while pairing.

The WebUI's G-code preview, job bounds check, file upload, full config
tree editing and firmware update are not on the remote.

## Pairing

1. On the Maslow, open the pairing window: send `$EP` from the WebUI
   terminal (it stays open for 60 seconds).
2. On the remote, tap **Pair** (shown at first boot, and under More).

The remote finds the Maslow's WiFi channel by itself and remembers the
pairing. `$EL` on the Maslow lists paired remotes; `$EU=<n>` removes one.

## Building and flashing

```sh
cd remote
pio run -t upload
```

The first boot asks you to touch the screen to calibrate touch (it skips
after 6 seconds). To recalibrate later, use More > Calibrate touch, or
hold the top bar left of STOP for 4 seconds.

## Development tools

Opening the CYD's serial port resets the ESP32, so `tools/serial_hub.py`
owns the port and shares it on localhost:4568, logging to `logs/`:

```sh
nohup ~/.platformio/penv/bin/python tools/serial_hub.py > logs/hub.out 2>&1 &
tools/flash.sh                       # pauses the hub while flashing
~/.platformio/penv/bin/python tools/remote_cli.py shot screen.png
~/.platformio/penv/bin/python tools/remote_cli.py cmd "tap 120 200"
```

Serial console commands: `shot`, `tap x y`, `hold x y [ms]`, `tab n`,
`send <line>`, `rt <hex>`, `link`, `state`, `pair`, `forget`, `heap`.

## Code

- `src/Link.*`: ESP-NOW peripheral side: pairing, session keepalives,
  anti-replay, fragmented lines, realtime bytes. The pairing crypto is
  `ESPNowCrypto.cpp`, built straight from the firmware tree.
- `src/Machine.*`: status parsing, Maslow state, command queue, log, files,
  settings, alerts.
- `src/Ui.*`: touch handling, buttons, top and tab bars, dialogs.
- `src/Views.*`: the screens.
