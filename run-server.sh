#!/bin/bash
# Launch the CTF server.
#
# Usage: ./run-server.sh [port]
#   port defaults to 8889 if omitted.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/build/ctf.app/Contents/MacOS/ctf"

if [ ! -f "$BINARY" ]; then
    echo "Error: $BINARY not found."
    echo "Run 'cmake --build build' first."
    exit 1
fi

exec "$BINARY" --server "$@"
