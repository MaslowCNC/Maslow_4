#!/bin/sh
# Erase the whole flash, including saved settings, calibration and belt lengths.
cd "$(dirname "$0")"
if ! . ./tools.sh; then exit 1; fi

esptool_erase

deactivate
