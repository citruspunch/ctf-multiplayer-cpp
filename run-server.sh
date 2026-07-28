#!/bin/bash
# Launch the CTF server via macOS app bundle.
# The .app bundle is created automatically after `cmake --build`.
#
# Usage: ./run-server.sh [port]
#   port defaults to 8889 if omitted.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_BUNDLE="$SCRIPT_DIR/build/ctf.app"

if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: $APP_BUNDLE not found."
    echo "Run 'cmake --build --preset=release' first to create the bundle."
    exit 1
fi

open -n "$APP_BUNDLE" --args --server "$@"
