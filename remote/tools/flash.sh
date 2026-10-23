#!/bin/sh
# Build and flash the remote, pausing the serial hub while esptool owns the port.
cd "$(dirname "$0")/.." || exit 1
mkdir -p logs
touch logs/hub.pause
sleep 0.5
pio run -t upload "$@"
status=$?
rm -f logs/hub.pause
exit $status
