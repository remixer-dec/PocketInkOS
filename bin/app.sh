#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/config.sh"

arduino-cli compile --fqbn "$FQBN" --build-path "$PROJECT_DIR/build" "$@" "$PROJECT_DIR/src"
"$ESPTOOL" --chip esp32s3 -p "$PORT" --before usb-reset write-flash 0x10000 "$PROJECT_DIR/build/src.ino.bin"
