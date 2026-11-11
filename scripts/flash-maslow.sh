#!/usr/bin/env bash
# Build and flash Maslow 4 firmware (ESP32-S3, PlatformIO env wifi_s3).
#
#   scripts/flash-maslow.sh build [--fs]                  build only
#   scripts/flash-maslow.sh wifi  [HOST] [--fs]           HTTP update (default HOST maslow.local)
#   scripts/flash-maslow.sh usb   [PORT] [--fs] [--erase] USB flash (default PORT /dev/ttyACM0)
#   scripts/flash-maslow.sh backup  [HOST]                save the machine's *.yaml files locally
#   scripts/flash-maslow.sh restore [HOST] DIR            upload *.yaml from DIR, then restart
#
#   --fs          also rebuild the web UI and update the filesystem.
#                 usb:  writes the whole filesystem image, so the machine's *.yaml files
#                       (calibrated anchors) are replaced by the repo defaults. Run `backup` first.
#                 wifi: uploads the data files one by one and leaves *.yaml alone
#                       (they are still backed up first, as a safety copy).
#   --reset-config  (wifi --fs) also upload the repo default maslow.yaml: calibration is lost.
#   --erase       (usb only, implies --fs) erase the whole flash first: also wipes NVS
#                 (WiFi settings, saved belt positions, Z position). A factory reset.
#   --skip-build  reuse the existing build output.
#
# WiFi notes: maslow.local only resolves when the machine is joined to your network.
# When the machine is hosting its own "maslow" access point, join it and use 192.168.0.1.
#
# Env: PIO (path to pio; default `pio` on PATH, else ~/.platformio-venv/bin/pio)
set -euo pipefail

# File size in bytes; stat -c is GNU-only, so use wc -c, which works on macOS too.
fsize() { wc -c <"$1" | tr -d " "; }

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW="$REPO/firmware"
ENV_NAME=wifi_s3
BUILD="$FW/.pio/build/$ENV_NAME"

if [[ -n "${PIO:-}" ]]; then :
elif command -v pio >/dev/null; then PIO=pio
elif [[ -x "$HOME/.platformio-venv/bin/pio" ]]; then PIO="$HOME/.platformio-venv/bin/pio"
else echo "PlatformIO not found. Install: python3 -m venv ~/.platformio-venv && ~/.platformio-venv/bin/pip install platformio" >&2; exit 1
fi

MODE="${1:-}"; shift || true
TARGET=""; FS=0; ERASE=0; SKIP_BUILD=0; RESET_CONFIG=0; DIR=""
BACKUP_ROOT="$REPO/.maslow-backups"
for a in "$@"; do
    case "$a" in
        --fs) FS=1 ;;
        --erase) ERASE=1; FS=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        --reset-config) RESET_CONFIG=1 ;;
        -*) echo "unknown option $a" >&2; exit 2 ;;
        *) if [[ $MODE == restore && -d "$a" ]]; then DIR="$a"; else TARGET="$a"; fi ;;
    esac
done
case "$MODE" in
    build) ;;
    wifi) TARGET="${TARGET:-maslow.local}"; [[ $ERASE == 1 ]] && { echo "--erase needs usb" >&2; exit 2; } ;;
    usb)  TARGET="${TARGET:-/dev/ttyACM0}" ;;
    backup) TARGET="${TARGET:-maslow.local}" ;;
    restore) TARGET="${TARGET:-maslow.local}"; [[ -n "$DIR" ]] || { echo "restore needs a backup directory" >&2; exit 2; } ;;
    *) sed -n '2,25p' "$0"; exit 2 ;;
esac

# ---- Config (*.yaml) backup and restore over HTTP --------------------------------------
reachable() {  # a few tries: the web server or mDNS sometimes misses one request
    local i; for i in 1 2 3; do curl -s -o /dev/null -m 5 "http://$TARGET/" && return 0; sleep 1; done; return 1
}
need_reachable() {
    reachable && return 0
    echo "Maslow not reachable at http://$TARGET/." >&2
    echo "If it is hosting its own 'maslow' network, join it and use 192.168.0.1." >&2
    [[ "$MODE" == wifi ]] && echo "Otherwise connect it over USB-C and run: $0 usb [PORT]$([[ $FS == 1 ]] && echo ' --fs')" >&2
    exit 3
}
list_yaml() {  # names of *.yaml files in the machine's local filesystem root
    curl -sf -m 10 "http://$TARGET/files?path=/" |
        python3 -c 'import json,sys; [print(f["name"]) for f in json.load(sys.stdin)["files"] if f["name"].endswith(".yaml")]'
}
backup_config() {  # sets BACKUP_DIR
    BACKUP_DIR="$BACKUP_ROOT/${TARGET}-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$BACKUP_DIR"
    local names; names="$(list_yaml)" || { echo "Could not list files on $TARGET" >&2; exit 5; }
    [[ -n "$names" ]] || { echo "No .yaml files on $TARGET" >&2; exit 5; }
    echo "== Backing up config files to $BACKUP_DIR"
    while read -r n; do
        curl -sf -m 20 -o "$BACKUP_DIR/$n" "http://$TARGET/$n" || { echo "Download of $n failed" >&2; exit 5; }
        [[ -s "$BACKUP_DIR/$n" ]] || { echo "Downloaded $n is empty" >&2; exit 5; }
        echo "   $n ($(fsize "$BACKUP_DIR/$n") bytes)"
    done <<< "$names"
    curl -sf -m 5 "http://$TARGET/command?plain=%24Config%2FFilename" | grep -o 'Config/Filename=.*' > "$BACKUP_DIR/.active" || true
}
restore_config() {  # $1 = backup dir
    local dir="$1" f n
    echo "== Restoring config files from $dir"
    for f in "$dir"/*.yaml; do
        n="$(basename "$f")"
        curl -sf -m 30 -F "path=/" -F "/${n}S=$(fsize "$f")" -F "myfile[]=@$f;filename=/$n" \
            "http://$TARGET/files" > /dev/null || { echo "Upload of $n failed. Backup kept in $dir" >&2; exit 6; }
        # Read it back and compare byte for byte
        if ! curl -sf -m 20 "http://$TARGET/$n" | cmp -s - "$f"; then
            echo "$n on the machine does not match the backup. Backup kept in $dir" >&2; exit 6
        fi
        echo "   $n restored and verified"
    done
    echo "== Restarting so the restored config is loaded"
    curl -s -m 5 "http://$TARGET/command?plain=%5BESP444%5DRESTART" > /dev/null || true
    sleep 5; for _ in $(seq 1 30); do reachable && break; sleep 2; done
    reachable || echo "Warning: machine did not come back after restart" >&2
}
case "$MODE" in
    backup)  need_reachable; backup_config; echo "Restore with: $0 restore $TARGET $BACKUP_DIR"; exit 0 ;;
    restore) need_reachable; restore_config "$DIR"; exit 0 ;;
esac

# Always build with the same data dir. PlatformIO treats a changed PLATFORMIO_DATA_DIR as a
# new configuration and wipes .pio/build, so firmware and filesystem must be built under one value.
export PLATFORMIO_DATA_DIR="$FW/.pio/fsdata"

stage_fs() {
    echo "== Building web UI"
    (cd "$REPO/ESP3D-WEBUI" && { [[ -d node_modules ]] || npm ci --no-audit --no-fund; } && npm run build:en)
    # The committed data/index.html.gz can lag the UI source, so always use a fresh build.
    rm -rf "$PLATFORMIO_DATA_DIR" && mkdir -p "$PLATFORMIO_DATA_DIR"
    find "$FW/FluidNC/data" -maxdepth 1 -type f ! -name index.html.gz -exec cp {} "$PLATFORMIO_DATA_DIR/" \;
    cp "$REPO/ESP3D-WEBUI/dist/index.html.gz" "$PLATFORMIO_DATA_DIR/"
    echo "Filesystem contents:"; ls -l "$PLATFORMIO_DATA_DIR"
}

if [[ $SKIP_BUILD == 0 ]]; then
    mkdir -p "$PLATFORMIO_DATA_DIR"
    [[ $FS == 1 ]] && stage_fs
    echo "== Building firmware"
    (cd "$FW" && "$PIO" run -e "$ENV_NAME")
    # WiFi uploads the staged files one by one; only USB needs the littlefs image.
    if [[ $FS == 1 && $MODE != wifi ]]; then (cd "$FW" && "$PIO" run -e "$ENV_NAME" -t buildfs); fi
elif [[ $FS == 1 && $MODE == wifi ]]; then
    # WiFi uploads the staged files directly, so always stage the current UI build;
    # otherwise --skip-build would send whatever was staged last time.
    stage_fs
fi
for f in bootloader.bin partitions.bin firmware.bin; do
    [[ -f "$BUILD/$f" ]] || { echo "missing $BUILD/$f (build first)" >&2; exit 1; }
done
[[ $FS == 1 && $MODE == usb && ! -f "$BUILD/littlefs.bin" ]] && { echo "missing littlefs.bin (build with --fs)" >&2; exit 1; }

[[ "$MODE" == build ]] && { echo "Build complete: $BUILD"; exit 0; }

if [[ "$MODE" == usb ]]; then
    [[ -e "$TARGET" ]] || { echo "No serial device at $TARGET. Is the Maslow plugged in over USB-C?" >&2; exit 3; }
    esptool() { (cd "$FW" && "$PIO" pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 --port "$TARGET" --baud 921600 "$@"); }
    BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    if [[ $ERASE == 1 ]]; then echo "== Erasing entire flash"; esptool erase_flash; fi
    # Offsets match firmware/max_littlefs.csv. boot_app0 resets otadata so the board boots
    # app0 even if an earlier OTA update had switched it to app1.
    IMAGES=(0x0 "$BUILD/bootloader.bin" 0x8000 "$BUILD/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$BUILD/firmware.bin")
    [[ $FS == 1 ]] && IMAGES+=(0x5F0000 "$BUILD/littlefs.bin")
    echo "== Writing over USB to $TARGET"
    esptool --before default_reset --after hard_reset write_flash -z --verify \
        --flash_mode keep --flash_freq keep --flash_size keep "${IMAGES[@]}"
    echo "Done. Check boot with: scripts/maslow_serial.py --port $TARGET '\$SS'"
    exit 0
fi

# WiFi: the same HTTP endpoints the web UI uses. Not ArduinoOTA (espota): it runs the whole
# transfer inside FluidNC's polling loop without feeding Maslow's update watchdog, and the
# connection was reset partway through. /updatefw feeds the watchdog during flash writes.
need_reachable
wait_back() {
    sleep 5; for _ in $(seq 1 40); do reachable && return 0; sleep 2; done; return 1
}
upload_file() {  # $1 = local file, uploaded to the machine's filesystem root under its basename
    local f="$1" n; n="$(basename "$f")"
    curl -sf -m 60 -F "path=/" -F "/${n}S=$(fsize "$f")" -F "myfile[]=@$f;filename=/$n" \
        "http://$TARGET/files" > /dev/null || { echo "Upload of $n failed" >&2; exit 6; }
    curl -sf -m 60 "http://$TARGET/$n" | cmp -s - "$f" || { echo "$n on the machine does not match" >&2; exit 6; }
    echo "   $n uploaded and verified"
}

[[ $FS == 1 ]] && backup_config

echo "== Firmware upload to http://$TARGET/updatefw"
FWSIZE="$(fsize "$BUILD/firmware.bin")"
RESP="$(curl -s -m 300 -F "/firmware.binS=$FWSIZE" -F "myfile[]=@$BUILD/firmware.bin;filename=/firmware.bin" \
    "http://$TARGET/updatefw")" || { echo "Firmware upload failed (curl). The machine keeps its old firmware." >&2; exit 4; }
# UploadStatus 3 = SUCCESSFUL (WebUIServer.h); the machine reboots about a second later.
[[ "$RESP" == *'"status":"3"'* ]] || { echo "Firmware upload rejected: $RESP. The machine keeps its old firmware." >&2; exit 4; }
echo "== Uploaded; waiting for reboot"
wait_back || { echo "Machine did not come back after the firmware update." >&2; exit 4; }
curl -s -m 5 "http://$TARGET/command?plain=%24I" | grep -o 'FluidNC v[^]]*' || true

if [[ $FS == 1 ]]; then
    echo "== Uploading filesystem files"
    for f in "$PLATFORMIO_DATA_DIR"/*; do
        [[ "$f" == *.yaml && $RESET_CONFIG == 0 ]] && { echo "   $(basename "$f") skipped (keeping the machine's copy)"; continue; }
        upload_file "$f"
    done
    [[ $RESET_CONFIG == 1 ]] && { echo "== Restarting to load the default config"
        curl -s -m 5 "http://$TARGET/command?plain=%5BESP444%5DRESTART" > /dev/null || true; wait_back || true; }
fi
echo "Done. Reload the web UI at http://$TARGET/ (a hard refresh) to get the new UI."
