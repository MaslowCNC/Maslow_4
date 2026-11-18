#!/bin/sh
# Install the Maslow 4 firmware. Keeps the filesystem (maslow.yaml, web UI, files).
cd "$(dirname "$0")"
if ! . ./tools.sh; then exit 1; fi

install_firmware

deactivate
