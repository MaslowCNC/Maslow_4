# Maslow 4.1 ESP-NOW remote

Firmware for a handheld remote built on ELEGOO's ESP32 touch displays:

| Board | Build env |
| --- | --- |
| 2.8" ESP32-2432S028R "Cheap Yellow Display" (240x320 ILI9341, XPT2046) | `cyd` (default) |
| 4.0" ESP32-32E / E32R40T (320x480 ST7796, XPT2046 on the panel bus) | `cyd4` |
| 4.0" ESP32-S3 Hosyond / lcdwiki ES3C40P (320x480 ST7796S, FT6336U touch, native USB) | `es3c40p` |

All of them use the same layout, drawn for 240x320 and scaled to the panel, with
larger fonts on the 4" boards. It talks to the Maslow over ESP-NOW using
the FluidNC `ESPNowChannel` (`firmware/FluidNC/esp32/ESPNow`), so it needs
no WiFi network and keeps working when the WebUI is closed.

To put the firmware on a board without building it, see [Installing](#installing):
a browser installer, or a Full-Install package that needs nothing installed.

## Screens

The top bar always shows the machine state, the Maslow state, the link
status and a **STOP** button. STOP fires as soon as you touch it and stops
the way the WebUI does: it sends 0x18 (stops at once and ends any job),
waits for the restart banner, then sends `$STOP`, which saves positions and
leaves Z where it is. 0x18 is repeated every 500 ms until the banner
arrives, and `$STOP` is retried if it gets no reply, since ESP-NOW has no
retransmission of its own.

The remote reads the Maslow state from the `MS` field of the status report
(older firmware: the `Current state` log line), and the stopped job from
`STOPPED` (see `docs/protocol.md`).

| Tab | What it does |
| --- | --- |
| **Jog** | Work and machine position. Step jogging in X/Y (8 directions) and Z. The center button goes to XY home. Step sizes and mm/in are on the bottom row. **Home** opens: define XY home here, set XY home position, set Z home here or to a value, go to XY home, go to Z home, and Park. Tap an axis row to set that work coordinate. XY moves need the Ready to Cut state, and moves that leave machine Z outside 0–72 mm ask first, as in the WebUI (not with `fixedZ`). A Z home is mm above the z-stop; on a Maslow 4 one outside 0–72 mm is refused (the Mini, found with `$MBOARD`, has no limit). |
| **Job** | SD card file list. Start (with confirmation; shows Starting... until the job moves, so it can't be sent twice), Pause/Resume, Delete, progress bar and feed override. After a job stops early, Start becomes **Resume line N** (`$SD/RunFrom`, the line can be changed) and Delete becomes **Discard** (`$SD/Discard`); picking another file discards it. |
| **Setup** | Maslow state, Retract All, Extend All, Apply Tension, Release Tension, Find Anchors, Set Z-Stop, Test, Park, Unlock and **Settings** (work area (0 = auto), park position, scale, thicknesses, tension limits, retraction force, WiFi SSID/password; numeric changes are saved to maslow.yaml with `$CO`). In the Tensioned state (Apply Tension couldn't match the saved anchors) Find Anchors is offered; in Unknown, Apply Tension is offered when some belt lengths are known. While Find Anchors runs, this tab shows its progress messages. |
| **Log** | Machine messages, a keyboard to send any command, and a list of quick commands. |
| **More** | Unlock, spindle off, feed/rapid/spindle overrides, config macros, jog speeds, brightness, touch calibration, link info, pair or forget the machine. |

Warnings that pop up in the WebUI (Apply Tension deviation and retraction
warnings, Apply Tension failed with a Find Anchors button, Z home reset, Zm
invalid, calibration complete) pop up here too.
The RGB LED is green when connected, red in Alarm or when disconnected,
and blinks blue while pairing.

The WebUI's G-code preview, job bounds check, file upload, full config
tree editing and firmware update are not on the remote.

## Installing

Users install the remote firmware without building it and without
installing anything on their computer. There are two ways.

### In the browser (Chrome or Edge)

Open **<https://documentation.maslowcnc.com/remote-installer/>** in Chrome or
Edge on a computer (Firefox and Safari can't reach USB serial ports).

1. Unplug the Maslow's USB cable, then plug in the display with a USB data cable.
2. Click **Install** next to your board, pick the display's port and click
   Connect.
3. Choose **Erase** when asked. It takes about a minute.

The page uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and the
images the release workflow copies to `web-assets/remote/latest/`. The page
source is `docs/remote-installer/index.html`.

### With the Full-Install package

Every release has a zip for each operating system:
`maslow-remote-<version>-windows.zip`, `-macos.zip` and `-linux.zip`. Each one
holds every board's firmware, a Full-Install script, and Espressif's
stand-alone `esptool` program for that system, so Python isn't needed.

1. Download the zip for your computer from the
   [latest release](https://github.com/MaslowCNC/Maslow_4/releases/latest)
   and unpack the whole thing.
2. Unplug the Maslow's USB cable, then plug in the display with a USB data cable.
3. Run the installer:

   | System | How |
   | --- | --- |
   | Windows | Double-click `Full-Install.bat` |
   | macOS | Double-click `Full-Install.command`. If macOS says it can't check the file, open System Settings > Privacy & Security, click **Open Anyway**, and double-click it again. Or run `sh Full-Install.sh` in Terminal. |
   | Linux | `sh Full-Install.sh` (your user needs serial port access: `sudo usermod -aG dialout $USER`, then log in again) |

4. The installer finds the board and shows its port, chip and MAC. The
   ESP32-S3 board is recognised by itself. For an ESP32 board it asks which
   display you have (2.8" or 4.0" ESP32-32E). Type `y` to install.

Full-Install erases the whole board first, as the Maslow's Full-Install
does, so the remote forgets its pairing and touch calibration.

After either way of installing, the remote restarts and offers touch
calibration for 6 seconds (it can be done later under More > Calibrate
touch). Then pair it (below).

If the board isn't found: use a cable that carries data, close other programs
that have the port open, and on Windows let it install the CH340 driver for the
2.8" and ESP32-32E boards (Windows 10 and 11 do it themselves the first time).
As a last resort, hold the board's BOOT button while plugging it in.

The images are merged (bootloader, partition table and app), so they can also
be written by hand at offset 0:
`esptool --chip esp32s3 write_flash 0x0 es3c40p.bin` (`--chip esp32` for
`cyd.bin` and `cyd4.bin`).

### Making the packages

The release workflow (`.github/workflows/release.yml`, job `build-remote`)
builds the three boards and runs `tools/package_release.py`. The scripts and
instructions that go into the zips are in `install/`. To build the packages
locally:

```sh
cd remote
pio run -e cyd -e cyd4 -e es3c40p
python3 tools/package_release.py --version 1.2.3 --dist-dir dist --web-dir dist/web
```

## Pairing

1. Turn the Maslow off and on again.
2. Within 2 minutes, tap **Pair** on the remote (shown at first boot, and
   under More). Tapping Pair first and then turning the Maslow on works too.

No computer or WiFi login is needed. Every power-on opens the Maslow's
pairing window for 2 minutes. The window closes once a remote pairs, and
`$EC` closes it early. From the WebUI, `$EP` opens a 60 second window, the
old way.

The remote finds the Maslow's WiFi channel by itself and remembers the
pairing. `$EL` on the Maslow lists paired remotes; `$EU=<n>` removes one.

## Building and flashing (development)

```sh
cd remote
pio run -e cyd -t upload     # 2.8"
pio run -e cyd4 -t upload    # 4.0" ESP32-32E
pio run -e es3c40p -t upload # 4.0" ES3C40P (ESP32-S3)
```

The first boot asks you to touch the screen to calibrate touch (it skips
after 6 seconds). To recalibrate later, use More > Calibrate touch, or
hold the top bar left of STOP for 4 seconds.

## Development tools

Opening the CYD's serial port resets the ESP32, so `tools/serial_hub.py`
owns the port and shares it on localhost:4568, logging to `logs/`:

```sh
nohup ~/.platformio/penv/bin/python tools/serial_hub.py > logs/hub.out 2>&1 &
tools/flash.sh -e cyd4              # pauses the hub while flashing
~/.platformio/penv/bin/python tools/remote_cli.py shot screen.png
~/.platformio/penv/bin/python tools/remote_cli.py cmd "tap 120 200"
```

Serial console commands (coordinates in panel pixels): `shot`, `tap x y`, `hold x y [ms]`, `tab n`,
`send <line>`, `rt <hex>`, `inject <line>` (handle a line as if the machine sent it, for testing
without a Maslow), `link`, `state`, `pair`, `forget`, `heap`, `id`, `sleep`.

## Code

- `src/Link.*`: ESP-NOW peripheral side: pairing, session keepalives,
  anti-replay, fragmented lines, realtime bytes. The pairing crypto is
  `ESPNowCrypto.cpp`, built straight from the firmware tree.
- `src/Machine.*`: status parsing, Maslow state, command queue, log, files,
  settings, alerts.
- `src/Ui.*`: touch handling, buttons, top and tab bars, dialogs.
- `src/Views.*`: the screens.
