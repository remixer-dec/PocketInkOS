#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/container-check"
STUBS="$ROOT/tools/container-stubs"
mkdir -p "$BUILD_DIR"

"$ROOT/tools/generate-secrets-header.sh"

TTS_APPS="${ENABLE_TTS:-${WITH_TTS:-0}}"
if [[ "$TTS_APPS" != "0" && "$TTS_APPS" != "1" ]]; then
  printf 'ENABLE_TTS/WITH_TTS must be 0 or 1, got %q\n' "$TTS_APPS" >&2
  exit 1
fi

COMMON=(-std=c++17 -Wall -Wextra -Werror -fsyntax-only -include "$ROOT/src/sys/arduino_esp32_compat.h" -I"$STUBS" -I"$ROOT/src" -I"$ROOT")
append_inflect_define() {
  local name="$1"
  local default_value="$2"
  local value="${!name:-$default_value}"
  if [[ "$value" != "0" && "$value" != "1" ]]; then
    printf '%s must be 0 or 1, got %q\n' "$name" "$value" >&2
    exit 1
  fi
  COMMON+=("-D$name=$value")
  printf -v "$3" '%s' "$value"
}
append_inflect_int_define() {
  local name="$1"
  local default_value="$2"
  local value="${!name:-$default_value}"
  if [[ ! "$value" =~ ^[0-9]+$ || "$value" -lt 1 ]]; then
    printf '%s must be a positive integer, got %q\n' "$name" "$value" >&2
    exit 1
  fi
  COMMON+=("-D$name=$value")
  printf -v "$3" '%s' "$value"
}
append_inflect_nonnegative_int_define() {
  local name="$1"
  local default_value="$2"
  local value="${!name:-$default_value}"
  if [[ ! "$value" =~ ^[0-9]+$ ]]; then
    printf '%s must be a non-negative integer, got %q\n' "$name" "$value" >&2
    exit 1
  fi
  COMMON+=("-D$name=$value")
  printf -v "$3" '%s' "$value"
}
append_inflect_symbol_define() {
  local name="$1"
  local default_value="$2"
  local allowed="$3"
  local value="${!name:-$default_value}"
  if [[ ! "$value" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
    printf '%s must be one of [%s], got %q\n' "$name" "$allowed" "$value" >&2
    exit 1
  fi
  case " $allowed " in
    *" $value "*) ;;
    *)
      printf '%s must be one of [%s], got %q\n' "$name" "$allowed" "$value" >&2
      exit 1
      ;;
  esac
  COMMON+=("-D$name=$value")
  printf -v "$4" '%s' "$value"
}
COMMON+=("-DENABLE_TTS=$TTS_APPS")
if [[ "$TTS_APPS" != "0" ]]; then
  COMMON+=(-DINFLECT_LOW_MEMORY -include "$ROOT/src/tts/ggml_config_arduino.h" -I"$ROOT/src/tts/InflectNanoTTS/src" -I"$ROOT/src/tts/InflectNanoTTS/ggml/include" -I"$ROOT/src/tts/InflectNanoTTS/ggml/src" -I"$ROOT/src/tts/InflectNanoTTS/ggml/src/ggml-cpu")
  append_inflect_define INFLECT_USE_ESP_DSP_CONTIG 0 INFLECT_DSP_CONTIG
  append_inflect_define INFLECT_USE_ESP_DSP_STRIDED 1 INFLECT_DSP_STRIDED
  append_inflect_define INFLECT_USE_RESBLOCK_IM2COL 1 INFLECT_RESBLOCK_IM2COL
  append_inflect_symbol_define INFLECT_VOCODER_BACKEND neural "neural griffin_lim" INFLECT_BACKEND
  append_inflect_nonnegative_int_define INFLECT_GRIFFIN_LIM_ITERS 8 INFLECT_GL_ITERS
  append_inflect_define INFLECT_PROFILE_ACOUSTIC_OPS 0 INFLECT_ACOUSTIC_PROFILE
  append_inflect_define INFLECT_PROFILE_VOCODER_OPS 0 INFLECT_VOCODER_PROFILE
  append_inflect_define INFLECT_ACOUSTIC_SKIP_POSTNET 0 INFLECT_SKIP_POSTNET
  printf '[container-compile] TTS enabled: low_memory=1 esp_dsp_contig=%s esp_dsp_strided=%s resblock_im2col=%s backend=%s griffin_lim_iters=%s\n' \
    "$INFLECT_DSP_CONTIG" \
    "$INFLECT_DSP_STRIDED" \
    "$INFLECT_RESBLOCK_IM2COL" \
    "$INFLECT_BACKEND" \
    "$INFLECT_GL_ITERS"
  printf '[container-compile] TTS profiling: acoustic=%s vocoder=%s acoustic_skip_postnet=%s\n' \
    "$INFLECT_ACOUSTIC_PROFILE" \
    "$INFLECT_VOCODER_PROFILE" \
    "$INFLECT_SKIP_POSTNET"
else
  printf '[container-compile] TTS disabled; set ENABLE_TTS=1 or WITH_TTS=1 to include it\n'
fi
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

if [[ "$TTS_APPS" != "0" ]]; then
  FILES+=( \
    "$ROOT/src/apps/tts_app.cpp" \
    "$ROOT/src/tts/ggml_backend_registry_esp32.cpp" \
    "$ROOT/src/tts/inflect_tts_wrapper.cpp" \
    "$ROOT/src/tts/tts_ggml_psram_buffer.cpp" \
    "$ROOT/src/tts/InflectNanoTTS/src/synthesizer.cpp" \
    "$ROOT/src/tts/InflectNanoTTS/src/griffin_lim_vocoder.cpp" \
    "$ROOT/src/tts/InflectNanoTTS/src/model_loader.cpp" \
    "$ROOT/src/tts/InflectNanoTTS/src/acoustic_model.cpp" \
    "$ROOT/src/tts/InflectNanoTTS/src/vocoder_model.cpp" \
    "$ROOT/src/tts/InflectNanoTTS/src/text_frontend.cpp")
fi

for file in "${FILES[@]}"; do
  g++ "${COMMON[@]}" "$file"
done

cp "$ROOT/src/src.ino" "$BUILD_DIR/src_ino.cpp"
g++ "${COMMON[@]}" "$BUILD_DIR/src_ino.cpp"
