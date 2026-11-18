#!/bin/sh
# macOS: double-click to install the Maslow remote firmware (runs Full-Install.sh in Terminal).
cd "$(dirname "$0")" || exit 1
exec sh ./Full-Install.sh
