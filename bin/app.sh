#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/config.sh"

BUILD_DIR="$PROJECT_DIR/build"
SKETCH_DIR="$PROJECT_DIR/src"
APP_BIN="$BUILD_DIR/src.ino.bin"

log() {
  printf '\n[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

log "Project: $PROJECT_DIR"
log "Sketch:  $SKETCH_DIR"
log "Build:   $BUILD_DIR"
log "Port:    $PORT"
log "FQBN:    $FQBN"

COMPILE_ARGS=(arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR")
if [[ "${VERBOSE:-0}" == "1" ]]; then
  COMPILE_ARGS+=(--verbose)
fi
COMPILE_ARGS+=("$@" "$SKETCH_DIR")

log "Compiling firmware"
printf 'Command:'
printf ' %q' "${COMPILE_ARGS[@]}"
printf '\n'
"${COMPILE_ARGS[@]}"

if [[ ! -f "$APP_BIN" ]]; then
  printf 'Expected app binary was not created: %s\n' "$APP_BIN" >&2
  exit 1
fi

log "Compiled app binary"
ls -lh "$APP_BIN"

FLASH_ARGS=("$ESPTOOL" --chip esp32s3 -p "$PORT" -b "$UPLOAD_SPEED" --before usb-reset write-flash 0x10000 "$APP_BIN")

log "Flashing app binary"
printf 'Command:'
printf ' %q' "${FLASH_ARGS[@]}"
printf '\n'
"${FLASH_ARGS[@]}"

log "Done"
