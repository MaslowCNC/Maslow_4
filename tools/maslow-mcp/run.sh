#!/bin/sh
# Launcher used by .mcp.json: creates the venv on first run, then starts the server.
# Extra arguments (e.g. --allow-motion) are passed through.
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$HERE/.venv/bin/python" ]; then
    python3 -m venv "$HERE/.venv" >&2 && "$HERE/.venv/bin/pip" install -q -r "$HERE/requirements.txt" >&2 || exit 1
fi
exec "$HERE/.venv/bin/python" "$HERE/maslow_mcp.py" "$@"
