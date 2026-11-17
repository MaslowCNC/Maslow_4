#!/bin/sh
# Fresh install: erase the flash, then write the filesystem and the firmware.
# This deletes maslow.yaml, saved settings and calibration.
cd "$(dirname "$0")"
if ! . ./tools.sh; then exit 1; fi

esptool_erase
install_filesystem
install_firmware

printf '\n\nInstallation successful!\nPress Enter to exit...'
read _input

deactivate
