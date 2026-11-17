#!/bin/sh
# Replace the filesystem with the release's (web UI and config templates).
# This deletes maslow.yaml and any uploaded files on the machine.
cd "$(dirname "$0")"
if ! . ./tools.sh; then exit 1; fi

install_filesystem

deactivate
