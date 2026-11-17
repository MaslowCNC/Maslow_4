#!/bin/sh
cd "$(dirname "$0")"
if ! . ./tools.sh; then exit 1; fi

run_fluidterm $*
