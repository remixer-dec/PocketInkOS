#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/container-check"
STUBS="$ROOT/tools/container-stubs"
mkdir -p "$BUILD_DIR"

"$ROOT/tools/generate-secrets-header.sh"

COMMON=(-std=c++17 -Wall -Wextra -Werror -fsyntax-only -include "$ROOT/src/sys/arduino_esp32_compat.h" -I"$STUBS" -I"$ROOT/src")
NETWORK_APPS="${ENABLE_NETWORK_APPS:-1}"
if [[ "$NETWORK_APPS" != "0" ]]; then
  COMMON+=(-DENABLE_NETWORK_APPS=1)
else
  COMMON+=(-DENABLE_NETWORK_APPS=0)
fi

FILES=( \
  "$ROOT/src/sys/app_display.cpp" \
  "$ROOT/src/sys/audio_capture.cpp" \
  "$ROOT/src/netapps/ai_app.cpp" \
  "$ROOT/src/apps/calculator_app.cpp" \
  "$ROOT/src/games/chess_app.cpp" \
  "$ROOT/src/apps/contact_links_app.cpp" \
  "$ROOT/src/games/cube_app.cpp" \
  "$ROOT/src/sys/device_clock.cpp" \
  "$ROOT/src/games/hangman_app.cpp" \
  "$ROOT/src/ui/components/keyboard_component.cpp" \
  "$ROOT/src/games/minesweeper_app.cpp" \
  "$ROOT/src/apps/paint_app.cpp" \
  "$ROOT/src/ui/qwerty_zoom/qwerty_zoom_keyboard_component.cpp" \
  "$ROOT/src/apps/qr_app.cpp" \
  "$ROOT/src/ui/components/smart_button.cpp" \
  "$ROOT/src/games/sudoku_app.cpp" \
  "$ROOT/src/ui/t9_keyboard/t9_keyboard_component.cpp" \
  "$ROOT/src/games/tictactoe_app.cpp" \
  "$ROOT/src/sys/touch_input.cpp" \
  "$ROOT/src/games/wordle_app.cpp")

if [[ "$NETWORK_APPS" != "0" ]]; then
  FILES+=( \
    "$ROOT/src/netapps/hn_app.cpp" \
    "$ROOT/src/netapps/lightweight_json_parser.cpp" \
    "$ROOT/src/netapps/post_feed.cpp" \
    "$ROOT/src/netapps/reddit_app.cpp" \
    "$ROOT/src/netapps/weather_app.cpp" \
    "$ROOT/src/netapps/wifi_app.cpp")
fi

for file in "${FILES[@]}"; do
  g++ "${COMMON[@]}" "$file"
done

cp "$ROOT/src/src.ino" "$BUILD_DIR/src_ino.cpp"
g++ "${COMMON[@]}" "$BUILD_DIR/src_ino.cpp"
