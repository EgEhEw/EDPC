#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export VK_LAYER_PATH="$SCRIPT_DIR/third_party"
exec "$SCRIPT_DIR/build/edpc" "$@"
