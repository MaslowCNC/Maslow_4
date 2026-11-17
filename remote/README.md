# Maslow ESP-NOW remote

Firmware for a handheld remote built on one display board: the **4.0" ESP32-S3
IPS touch display sold by Hosyond** (lcdwiki ES3C40P). It has an ESP32-S3 with
16 MB flash and 8 MB PSRAM, a 320x480 ST7796S panel, FT6336U capacitive touch,
a WS2812 LED and native USB-C. Pins are listed in `src/Display.h`.

The layout is drawn for 240x320 and scaled to the panel. It talks to the Maslow over ESP-NOW using
the FluidNC `ESPNowChannel` (`firmware/FluidNC/esp32/ESPNow`), so it needs
no WiFi network and keeps working when the WebUI is closed.

To put the firmware on a display without building it, see [Installing](#installing):
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
| **More** | Unlock, spindle off, feed/rapid/spindle overrides, config macros, jog speeds, display and sleep settings, link info, pair or forget the machine. |

Warnings that pop up in the WebUI (Apply Tension deviation and retraction
warnings, Apply Tension failed with a Find Anchors button, Z home reset, Zm
invalid, calibration complete) pop up here too.
The LED is green when connected, red in Alarm or when disconnected,
and blinks blue while pairing.

The WebUI's G-code preview, job bounds check, file upload, full config
tree editing and firmware update are not on the remote.

## Installing

Users install the remote firmware without building it and without
installing anything on their computer. There are two ways.

### In the browser (Chrome or Edge)

Open **<https://documentation.maslowcnc.com/remote-installer/>** in Chrome or
Edge on a computer (Firefox and Safari can't reach USB serial ports).

1. Plug in the display with a USB-C data cable.
2. Click **Install**, pick the display's port and click Connect.
3. Choose **Erase** when asked. It takes about a minute.

The page uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and
loads `downloads/manifest.json` and `downloads/maslow-remote.bin` from this
branch on GitHub. Its source, `docs/remote-installer/index.html`, is the only
file of the remote outside this folder: the docs site is the repository's only
HTTPS host.

### With the Full-Install package

Download the zip for your computer:

| System | Download |
| --- | --- |
| Windows | [maslow-remote-windows.zip](https://github.com/MaslowCNC/Maslow_4/raw/Maslow-Main/remote/downloads/maslow-remote-windows.zip) |
| macOS | [maslow-remote-macos.zip](https://github.com/MaslowCNC/Maslow_4/raw/Maslow-Main/remote/downloads/maslow-remote-macos.zip) |
| Linux | [maslow-remote-linux.zip](https://github.com/MaslowCNC/Maslow_4/raw/Maslow-Main/remote/downloads/maslow-remote-linux.zip) |

Each one holds the firmware, a Full-Install script, and Espressif's
stand-alone `esptool` program for that system, so Python isn't needed.
They are in [`downloads/`](downloads), with `VERSION.txt` saying which build
they are.

1. Download the zip and unpack the whole thing.
2. Plug in the display with a USB-C data cable.
3. Run the installer:

   | System | How |
   | --- | --- |
   | Windows | Double-click `Full-Install.bat` |
   | macOS | Double-click `Full-Install.command`. If macOS says it can't check the file, open System Settings > Privacy & Security, click **Open Anyway**, and double-click it again. Or run `sh Full-Install.sh` in Terminal. |
   | Linux | `sh Full-Install.sh` (your user needs serial port access: `sudo usermod -aG dialout $USER`, then log in again) |

4. The installer finds the board and shows its port, chip and MAC. It
   refuses anything that isn't an ESP32-S3. Type `y` to install.

Full-Install erases the whole board first, as the Maslow's Full-Install
does, so the remote forgets its pairing.

After either way of installing, the remote restarts by itself. Then pair it
(below).

If the board isn't found, use a cable that carries data and close other
programs that have the port open. As a last resort, hold the board's BOOT
button while plugging it in.

The image is merged (bootloader, partition table and app), so it can also be
written by hand at offset 0:
`esptool --chip esp32s3 write_flash 0x0 maslow-remote.bin`.

### Updating the downloads

The files in `downloads/` are built by hand and committed; nothing in the
repository's release workflow touches them. After changing the remote:

```sh
cd remote
python3 tools/make_downloads.py
git add downloads && git commit -m "Remote: update downloads"
```

`make_downloads.py` runs `pio run`, then writes the three zips, the
browser installer's `maslow-remote.bin` and `manifest.json`, and
`VERSION.txt` (date and commit, plus `-modified` if the remote's source had
uncommitted changes). The Full-Install scripts and instructions that go into
the zips are in `install/`. esptool v4.10.0 is downloaded once into
`.release-cache/`.

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
pio run -t upload
```

## Development tools

`tools/serial_hub.py` owns the display's USB serial port and shares it on
localhost:4568, logging to `logs/`, so several tools can use the console at
once:

```sh
nohup ~/.platformio/penv/bin/python tools/serial_hub.py > logs/hub.out 2>&1 &
tools/flash.sh                      # pauses the hub while flashing
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
