#!/bin/sh
# Flash the spindle board over USB.  If tools/serial_hub.py is running, it is paused for the
# upload (so nobody has to close their console) and resumed afterwards, even if the upload fails.
cd "$(dirname "$0")/.." || exit 1
PIO="$HOME/.platformio/penv/bin/pio"
STATUS=logs/hub.status

hub_running() { nc -z 127.0.0.1 4567 2>/dev/null; }   # the hub's console port is listening

if hub_running; then
    touch logs/hub.pause
    trap 'rm -f logs/hub.pause' EXIT
    i=0
    while ! grep -q paused "$STATUS" 2>/dev/null; do
        i=$((i + 1))
        [ $i -gt 25 ] && { echo "hub did not release the port"; exit 1; }
        sleep 0.2
    done
fi

"$PIO" run -e esp32-s3-devkitc-1 -t upload
