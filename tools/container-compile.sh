#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/container-check"
STUBS="$ROOT/tools/container-stubs"
mkdir -p "$BUILD_DIR"

"$ROOT/tools/generate-secrets-header.sh"

COMMON=(-std=c++17 -Wall -Wextra -Werror -fsyntax-only -include "$ROOT/src/arduino_esp32_compat.h" -I"$STUBS" -I"$ROOT/src")
NETWORK_APPS="${ENABLE_NETWORK_APPS:-1}"
if [[ "$NETWORK_APPS" != "0" ]]; then
  COMMON+=(-DENABLE_NETWORK_APPS=1)
else
  COMMON+=(-DENABLE_NETWORK_APPS=0)
fi

FILES=( \
  "$ROOT/src/app_display.cpp" \
  "$ROOT/src/audio_capture.cpp" \
  "$ROOT/src/ai_app.cpp" \
  "$ROOT/src/calculator_app.cpp" \
  "$ROOT/src/chess_app.cpp" \
  "$ROOT/src/contact_links_app.cpp" \
  "$ROOT/src/cube_app.cpp" \
  "$ROOT/src/device_clock.cpp" \
  "$ROOT/src/hangman_app.cpp" \
  "$ROOT/src/keyboard_component.cpp" \
  "$ROOT/src/minesweeper_app.cpp" \
  "$ROOT/src/paint_app.cpp" \
  "$ROOT/src/qwerty_zoom_keyboard_component.cpp" \
  "$ROOT/src/qr_app.cpp" \
  "$ROOT/src/smart_button.cpp" \
  "$ROOT/src/sudoku_app.cpp" \
  "$ROOT/src/t9_keyboard_component.cpp" \
  "$ROOT/src/tictactoe_app.cpp" \
  "$ROOT/src/touch_input.cpp" \
  "$ROOT/src/wordle_app.cpp")

if [[ "$NETWORK_APPS" != "0" ]]; then
  FILES+=( \
    "$ROOT/src/hn_app.cpp" \
    "$ROOT/src/lightweight_json_parser.cpp" \
    "$ROOT/src/post_feed.cpp" \
    "$ROOT/src/reddit_app.cpp" \
    "$ROOT/src/weather_app.cpp" \
    "$ROOT/src/wifi_app.cpp")
fi

for file in "${FILES[@]}"; do
  g++ "${COMMON[@]}" "$file"
done

cp "$ROOT/src/src.ino" "$BUILD_DIR/src_ino.cpp"
g++ "${COMMON[@]}" "$BUILD_DIR/src_ino.cpp"
