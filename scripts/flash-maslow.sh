#!/usr/bin/env bash
# Build and flash Maslow 4 firmware (ESP32-S3, PlatformIO env wifi_s3).
#
#   scripts/flash-maslow.sh build [--fs]                  build only
#   scripts/flash-maslow.sh wifi  [HOST] [--fs]           OTA update (default HOST maslow.local)
#   scripts/flash-maslow.sh usb   [PORT] [--fs] [--erase] USB flash (default PORT /dev/ttyACM0)
#
#   --fs          also rebuild the web UI and flash the filesystem image.
#                 This REPLACES maslow.yaml with the repo default: calibration is lost.
#   --erase       (usb only, implies --fs) erase the whole flash first: also wipes NVS
#                 (WiFi settings, saved belt positions, Z position). A factory reset.
#   --skip-build  reuse the existing build output.
#
# WiFi notes: maslow.local only resolves when the machine is joined to your network.
# When the machine is hosting its own "maslow" access point, join it and use 192.168.0.1.
#
# Env: PIO (path to pio; default `pio` on PATH, else ~/.platformio-venv/bin/pio)
set -euo pipefail

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
TARGET=""; FS=0; ERASE=0; SKIP_BUILD=0
for a in "$@"; do
    case "$a" in
        --fs) FS=1 ;;
        --erase) ERASE=1; FS=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        -*) echo "unknown option $a" >&2; exit 2 ;;
        *) TARGET="$a" ;;
    esac
done
case "$MODE" in
    build) ;;
    wifi) TARGET="${TARGET:-maslow.local}"; [[ $ERASE == 1 ]] && { echo "--erase needs usb" >&2; exit 2; } ;;
    usb)  TARGET="${TARGET:-/dev/ttyACM0}" ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
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
    [[ $FS == 1 ]] && (cd "$FW" && "$PIO" run -e "$ENV_NAME" -t buildfs)
fi
for f in bootloader.bin partitions.bin firmware.bin; do
    [[ -f "$BUILD/$f" ]] || { echo "missing $BUILD/$f (build first)" >&2; exit 1; }
done
[[ $FS == 1 && ! -f "$BUILD/littlefs.bin" ]] && { echo "missing littlefs.bin (build with --fs)" >&2; exit 1; }

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

# WiFi (ArduinoOTA via espota; firmware and filesystem both supported)
reachable() { curl -s -o /dev/null -m 5 "http://$TARGET/"; }
if ! reachable; then
    echo "Maslow not reachable at http://$TARGET/." >&2
    echo "If it is hosting its own 'maslow' network, join it and use 192.168.0.1." >&2
    echo "Otherwise connect it over USB-C and run: $0 usb [PORT]$([[ $FS == 1 ]] && echo ' --fs')" >&2
    exit 3
fi
# Never add `-t nobuild` here: in the pinned platform-espressif32, nobuild + uploadfs picks
# firmware.bin as the image and OTA-writes it into the filesystem partition.
# Without it, pio just re-checks the (already current) build before uploading.
echo "== OTA firmware upload to $TARGET"
(cd "$FW" && "$PIO" run -e "$ENV_NAME" -t upload --upload-port "$TARGET")
if [[ $FS == 1 ]]; then
    echo "== Waiting for reboot"
    sleep 5; for _ in $(seq 1 30); do reachable && break; sleep 2; done
    reachable || { echo "Machine did not come back after firmware update; filesystem not flashed." >&2; exit 4; }
    echo "== OTA filesystem upload to $TARGET"
    (cd "$FW" && "$PIO" run -e "$ENV_NAME" -t uploadfs --upload-port "$TARGET")
fi
echo "Done. The web UI at http://$TARGET/ should show the new version after it reboots."
