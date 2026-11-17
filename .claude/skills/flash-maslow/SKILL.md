---
name: flash-maslow
description: Build and flash firmware (and optionally the filesystem / web UI) onto a Maslow 4 / 4.1 machine, over WiFi by default with a USB fallback, then verify it booted.
disable-model-invocation: true
argument-hint: "[wifi [host] | usb [port]] [--fs] [--erase] [--reset-config]"
---

# Flash a Maslow

The user started this on purpose. Flashing changes a physical machine, so follow the
confirmation points below. Never decide to flash from some other task. If flashing seems
needed in other work, suggest that the user run `/flash-maslow`.

The mechanics live in `scripts/flash-maslow.sh` (run it with no arguments for help). Use the script;
don't hand-assemble esptool or pio commands unless the script fails and you are debugging it.

## 1. Settle what to flash

Get these from the arguments, or ask if they're missing:

- **Transport:** WiFi is the default and preferred path.
  - `maslow.local` only resolves when the machine is joined to the user's network.
  - When the machine hosts its own `maslow` access point, the computer has to join that
    network, and the address is `192.168.0.1`.
- **Firmware only, or `--fs` too?**
  - `--fs` rebuilds the web UI and updates the machine's files.
  - **Over WiFi the calibration is kept.** The script uploads the data files one at a time
    (`index.html.gz`, `favicon.ico`, ...) and skips every `*.yaml`. It still downloads the
    `*.yaml` files to `.maslow-backups/<host>-<time>/` first, as a safety copy.
    `--reset-config` also uploads the repo default `maslow.yaml` (calibration lost): only
    when the user asks for it.
  - **Over USB the whole filesystem image is written, so the `*.yaml` files are replaced.**
    If the machine is on the network, run `scripts/flash-maslow.sh backup HOST` first and
    `restore HOST DIR` once it is back on the network. Otherwise say plainly that calibration
    will be lost and get a yes.
- **`--erase`** (USB only) wipes all flash, including NVS: WiFi credentials, saved belt
  positions and Z position. Use it only for an explicit factory reset.

## 2. Safety check before anything is written

Ask the user to confirm the belts are retracted, or that the sled is otherwise safe. On boot
the firmware spins each motor briefly at full power as a self-test. A firmware update always
reboots the machine, and the belt positions saved in NVS are restored (e.g. "State: Extended").

## 3. Run it

```bash
scripts/flash-maslow.sh wifi [HOST] [--fs] [--reset-config]
scripts/flash-maslow.sh usb [PORT] [--fs] [--erase]
scripts/flash-maslow.sh backup [HOST]          # just save the *.yaml files
scripts/flash-maslow.sh restore [HOST] DIR     # upload them again and restart
```

How WiFi works: firmware goes to `POST /updatefw` and files to `POST /files`, the same
endpoints the web UI's update and file pages use. Each uploaded file is read back and
compared. Exit codes: 3 not reachable, 4 firmware upload or reboot failed (the old firmware
keeps running: a failed update only writes the spare slot), 6 a file upload failed. Backups
in `.maslow-backups/` are never deleted by the script. Don't delete them.

- Run it in the background:
  - The first build after a fresh PlatformIO install takes about 7 minutes (toolchain download).
  - Rebuilds take about 2 minutes. An erase takes about 15 s and a USB write about 20 s.
  - A WiFi firmware upload plus reboot takes about 40 s.
  - Don't pipe the script into `tail` or `grep`: that hides its exit code.
- If PlatformIO is missing, the script prints the install command
  (a venv at `~/.platformio-venv`). Ask before installing it.

**If WiFi fails** (the script exits with code 3, "not reachable"): stop. Tell the user that
USB needs them to plug in the USB-C cable, and wait for them to say it's connected. Then check
with `ls /dev/ttyACM*` (or `lsusb | grep 303a`, Espressif USB-Serial/JTAG) and re-run with
`usb`. Never switch transports without the user's say-so.

## 4. Verify

- **USB:** `scripts/maslow_serial.py --port /dev/ttyACM0 '$SS' '$I'`. `$SS` replays the
  startup log. Check for:
  - `FluidNC v... (<branch>-<commit>)` matching the commit you built (`git log -1 --format=%h`)
  - `Configuration file:maslow.yaml`
  - `Encoder connected on` and `Motor detected on` for **all four** arms (Top Left/Top Right/Bottom Left/Bottom Right)
  - `Z Axis driver test passed` and `Z2 Axis driver test passed`
  - either `AP SSID maslow IP 192.168.0.1` + `AP started`, or a joined network
    (STA, the machine joined to an existing WiFi network) with its IP
- **WiFi:** the script prints `$I` after the reboot; check the commit in it. For the boot log
  use `curl -s "http://HOST/command?plain=%24SS"`. Over HTTP it ends at the network lines,
  so the arm encoder/motor self-test lines don't appear there (USB only). `$MINFO` replies
  only over the WebSocket, so it returns nothing over HTTP. For `--fs`, check the `*.yaml`
  files on the machine still match the backup (`curl http://HOST/maslow.yaml | cmp - DIR/maslow.yaml`).

Expected noise, not failures:
- about 16 `ledc: LEDC is not initialized` lines in the first 100 ms of boot
- `Ignored key MaslowAutoUpdate` / `MaslowUpdateURL`
- `ESP_ERR_NVS_NOT_FOUND` + "No saved belt positions" after an erase

Report what was flashed (commit, transport, firmware-only vs filesystem, config kept or reset), the self-test
results, and anything unexpected. If the config was reset (USB `--fs`, `--erase`,
`--reset-config`), remind the user that calibration has to be redone.

## Traps (learned the hard way)

- **Changing `PLATFORMIO_DATA_DIR` between builds wipes `.pio/build`.** The script always uses
  `firmware/.pio/fsdata`. A plain `pio run` from a shell without that variable rebuilds from
  scratch once. That's harmless, but it's the cause if the firmware binaries "disappear".
- **Don't use ArduinoOTA / espota (`pio run -t upload --upload-port HOST`).** It runs the
  transfer inside FluidNC's polling loop without feeding Maslow's update watchdog. On
  2026-10-26 the connection was reset at 7%. `/updatefw` feeds the watchdog.
- **Never use `pio run -t nobuild -t uploadfs`.** With the pinned platform it OTA-writes
  `firmware.bin` into the filesystem partition.
- **`firmware/install_scripts/*/install-wifi_s3.sh` is wrong for this board.** It's an unedited
  copy of the plain-ESP32 script (`--chip esp32`, bootloader at `0x1000`). The S3 bootloader
  goes at `0x0`.
- **USB offsets** (from `firmware/max_littlefs.csv`): bootloader `0x0`, partitions `0x8000`,
  `boot_app0` `0xe000`, firmware `0x10000`, littlefs `0x5F0000` (2 MB). `boot_app0` makes the
  board boot the new firmware even when an earlier OTA update left it on the second firmware slot.
- **`firmware/FluidNC/data/index.html.gz` in git can be stale.** `--fs` always rebuilds the UI
  from `ESP3D-WEBUI/`.
- **Serial:** open the port with DTR/RTS low (`maslow_serial.py` does this) or the board resets.
  Use fixed read windows; the firmware never goes quiet.
- **Running esptool directly:** `pio pkg exec --package tool-esptoolpy -- esptool.py ...`, from
  inside `firmware/`. Don't run PlatformIO's `esptool.py` with `python -I`; isolated mode can't
  find its modules.
