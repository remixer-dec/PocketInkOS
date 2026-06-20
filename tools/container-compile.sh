#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/container-check"
STUBS="$ROOT/tools/container-stubs"
mkdir -p "$BUILD_DIR"

"$ROOT/tools/generate-secrets-header.sh"

COMMON=(-std=c++17 -Wall -Wextra -Werror -fsyntax-only -include "$ROOT/src/sys/arduino_esp32_compat.h" -I"$STUBS" -I"$ROOT/src" -I"$ROOT")
NETWORK_APPS="${ENABLE_NETWORK_APPS:-1}"
if [[ "$NETWORK_APPS" != "0" ]]; then
  COMMON+=(-DENABLE_NETWORK_APPS=1)
else
  COMMON+=(-DENABLE_NETWORK_APPS=0)
fi

FILES=( \
  "$ROOT/src/sys/app_display.cpp" \
  "$ROOT/src/sys/audio_capture.cpp" \
  "$ROOT/src/sys/audio_playback_renderer.cpp" \
  "$ROOT/src/sys/audio_power.cpp" \
  "$ROOT/src/sys/builtin_apps.cpp" \
  "$ROOT/src/netapps/ai_app.cpp" \
  "$ROOT/src/apps/calculator_app.cpp" \
  "$ROOT/src/games/chess_app.cpp" \
  "$ROOT/src/apps/contact_links_app.cpp" \
  "$ROOT/src/apps/deghost_app.cpp" \
  "$ROOT/src/fs/file_provider.cpp" \
  "$ROOT/src/fs/file_viewer_registry.cpp" \
  "$ROOT/src/fs/providers/hex_file_viewer.cpp" \
  "$ROOT/src/fs/providers/epub_file_viewer.cpp" \
  "$ROOT/src/fs/providers/midi_file_viewer.cpp" \
  "$ROOT/src/fs/providers/resonatto/remidi_synth.cpp" \
  "$ROOT/src/fs/providers/pdf_file_viewer.cpp" \
  "$ROOT/src/fs/providers/image_file_viewer.cpp" \
  "$ROOT/src/fs/providers/svg_file_viewer.cpp" \
  "$ROOT/src/fs/providers/text_file_viewer.cpp" \
  "$ROOT/src/apps/files_app.cpp" \
  "$ROOT/src/sys/battery_monitor.cpp" \
  "$ROOT/src/games/cube_app.cpp" \
  "$ROOT/src/sys/device_controls.cpp" \
  "$ROOT/src/sys/device_clock.cpp" \
  "$ROOT/src/sys/environment_monitor.cpp" \
  "$ROOT/src/sys/inactivity_sleep_guard.cpp" \
  "$ROOT/src/sys/pcf85063_clock.cpp" \
  "$ROOT/src/sys/pink_executable.cpp" \
  "$ROOT/src/sys/power_control.cpp" \
  "$ROOT/src/sys/rtc_context.cpp" \
  "$ROOT/src/sys/sd_storage.cpp" \
  "$ROOT/src/sys/sleep_clock_context.cpp" \
  "$ROOT/src/games/hangman_app.cpp" \
  "$ROOT/src/sys/shell_buttons.cpp" \
  "$ROOT/src/ui/components/keyboard_component.cpp" \
  "$ROOT/src/ui/components/audio_player_component.cpp" \
  "$ROOT/src/games/minesweeper_app.cpp" \
  "$ROOT/src/apps/paint_app.cpp" \
  "$ROOT/src/ui/qwerty_zoom/qwerty_zoom_keyboard_component.cpp" \
  "$ROOT/src/apps/qr_app.cpp" \
  "$ROOT/src/apps/gfx_app.cpp" \
  "$ROOT/src/ui/shell_layout.cpp" \
  "$ROOT/src/ui/components/smart_button.cpp" \
  "$ROOT/src/ui/status_bar.cpp" \
  "$ROOT/src/games/sudoku_app.cpp" \
  "$ROOT/src/ui/components/t9_keyboard_component.cpp" \
  "$ROOT/src/ui/text_input_controller.cpp" \
  "$ROOT/src/games/tictactoe_app.cpp" \
  "$ROOT/src/sys/touch_input.cpp" \
  "$ROOT/src/sys/usb_status.cpp" \
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
