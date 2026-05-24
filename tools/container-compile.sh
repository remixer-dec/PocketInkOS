#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/container-check"
STUBS="$ROOT/tools/container-stubs"
mkdir -p "$BUILD_DIR"

COMMON=(-std=c++17 -Wall -Wextra -Werror -fsyntax-only -I"$STUBS" -I"$ROOT/src")

for file in \
  "$ROOT/src/app_display.cpp" \
  "$ROOT/src/keyboard_component.cpp" \
  "$ROOT/src/minesweeper_app.cpp" \
  "$ROOT/src/smart_button.cpp" \
  "$ROOT/src/tictactoe_app.cpp" \
  "$ROOT/src/touch_input.cpp"; do
  g++ "${COMMON[@]}" "$file"
done

cp "$ROOT/src/src.ino" "$BUILD_DIR/src_ino.cpp"
g++ "${COMMON[@]}" "$BUILD_DIR/src_ino.cpp"
