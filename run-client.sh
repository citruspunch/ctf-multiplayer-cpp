#!/bin/bash
# Launch the CTF client.
#
# Usage: ./run-client.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/build/ctf.app/Contents/MacOS/ctf"

if [ ! -f "$BINARY" ]; then
    echo "Error: $BINARY not found."
    echo "Run 'cmake --build build' first."
    exit 1
fi

exec "$BINARY" --client
