#include "ui/components/audio_player_component.h"

#include "sys/touch_input.h"
#include "ui/components/touch_button.h"

#include <cstdio>
#include <cstring>

namespace {

struct AudioPlayerLayout {
  UiRect title;
  UiRect subtitle;
  UiRect detail;
  UiRect playButton;
  UiRect volumeDown;
  UiRect volumeUp;
  UiRect bar;
  UiRect time;
};

AudioPlayerLayout makeLayout(bool fullscreen) {
  if (fullscreen) {
    return {{8, 8, 184, 10},    {8, 20, 184, 10},   {8, 32, 184, 10},
            {79, 128, 42, 42},  {8, 128, 42, 42},   {150, 128, 42, 42},
            {8, 62, 184, 18},   {8, 46, 184, 10}};
  }
  return {{8, 24, 184, 10},   {8, 36, 184, 10},   {8, 48, 184, 10},
          {79, 132, 42, 42},  {8, 132, 42, 42},   {150, 132, 42, 42},
          {8, 78, 184, 18},   {8, 62, 184, 10}};
}

bool contains(const UiRect &rect, const TouchPoint &point) {
  return TouchButton(rect).contains(point);
}

bool containsPadded(const UiRect &rect, const TouchPoint &point, int16_t pad) {
  UiRect padded = {static_cast<int16_t>(rect.x - pad),
                   static_cast<int16_t>(rect.y - pad),
                   static_cast<int16_t>(rect.w + pad * 2),
                   static_cast<int16_t>(rect.h + pad * 2)};
  return TouchButton(padded).contains(point);
}

void drawTextClipped(Adafruit_GFX &gfx, const UiRect &rect, const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  char buffer[48];
  strncpy(buffer, text, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(buffer, 0, 0, &textX, &textY, &textW, &textH);
  while (buffer[0] != '\0' && textW > static_cast<uint16_t>(rect.w)) {
    const size_t length = strlen(buffer);
    if (length <= 4) {
      break;
    }
    buffer[length - 4] = '.';
    buffer[length - 3] = '.';
    buffer[length - 2] = '.';
    buffer[length - 1] = '\0';
    gfx.getTextBounds(buffer, 0, 0, &textX, &textY, &textW, &textH);
  }

  gfx.setCursor(rect.x, rect.y + rect.h - 4);
  gfx.print(buffer);
}

void drawIconButton(Adafruit_GFX &gfx, const UiRect &rect, char glyph,
                    bool pressed) {
  char icon[2] = {glyph, '\0'};
  TouchButtonContent content;
  content.icon = icon;
  content.iconPlacement = TouchButtonIconPlacement::Only;
  TouchButton(rect, content).draw(gfx, pressed);
}

void formatClock(uint32_t ms, char *out, size_t outSize) {
  const uint32_t totalSeconds = ms / 1000U;
  const uint32_t minutes = totalSeconds / 60U;
  const uint32_t seconds = totalSeconds % 60U;
  snprintf(out, outSize, "%lu:%02lu", static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}

} // namespace

void AudioPlayerComponent::draw(Adafruit_GFX &gfx, const AudioPlayerState &state,
                                bool fullscreen) const {
  const AudioPlayerLayout layout = makeLayout(fullscreen);

  gfx.setTextColor(1);
  gfx.setTextSize(1);
  drawTextClipped(gfx, layout.title, state.title != nullptr ? state.title : "MIDI");
  drawTextClipped(gfx, layout.subtitle,
                  state.subtitle != nullptr ? state.subtitle : "");
  drawTextClipped(gfx, layout.detail, state.detail != nullptr ? state.detail : "");

  const uint32_t durationMs = state.durationMs;
  const uint32_t positionMs =
      durationMs > 0 && state.positionMs > durationMs ? durationMs : state.positionMs;
  const int16_t barY = layout.bar.y + layout.bar.h / 2;
  gfx.drawRect(layout.bar.x, barY - 3, layout.bar.w, 6, 1);
  if (durationMs > 0) {
    const int16_t filled = static_cast<int16_t>(
        (static_cast<uint64_t>(positionMs) * (layout.bar.w - 2)) / durationMs);
    if (filled > 0) {
      gfx.fillRect(layout.bar.x + 1, barY - 2, filled, 4, 1);
    }
  }

  drawIconButton(gfx, layout.playButton, state.playing ? '+' : '&',
                 state.pressedAction == AudioPlayerAction::TogglePlayPause);
  drawIconButton(gfx, layout.volumeDown, 'b',
                 state.pressedAction == AudioPlayerAction::VolumeDown);
  drawIconButton(gfx, layout.volumeUp, 'X',
                 state.pressedAction == AudioPlayerAction::VolumeUp);

  char detail[48];
  char left[12];
  char right[12];
  formatClock(positionMs, left, sizeof(left));
  formatClock(durationMs, right, sizeof(right));
  snprintf(detail, sizeof(detail), "%s / %s   VOL %u%%", left, right,
           static_cast<unsigned>(state.volumePercent));
  gfx.setCursor(layout.time.x, layout.time.y + layout.time.h - 2);
  gfx.print(detail);
}

AudioPlayerEvent AudioPlayerComponent::hitTest(const TouchPoint &point,
                                               bool fullscreen,
                                               uint32_t durationMs) const {
  const AudioPlayerLayout layout = makeLayout(fullscreen);
  AudioPlayerEvent event;
  if (containsPadded(layout.playButton, point, 8)) {
    event.action = AudioPlayerAction::TogglePlayPause;
    return event;
  }
  if (containsPadded(layout.volumeDown, point, 8)) {
    event.action = AudioPlayerAction::VolumeDown;
    return event;
  }
  if (containsPadded(layout.volumeUp, point, 8)) {
    event.action = AudioPlayerAction::VolumeUp;
    return event;
  }
  if (durationMs > 0 && contains(layout.bar, point)) {
    const uint16_t localX =
        point.x > layout.bar.x
            ? static_cast<uint16_t>(point.x - layout.bar.x)
            : static_cast<uint16_t>(0);
    event.action = AudioPlayerAction::Seek;
    event.seekPermille = static_cast<uint16_t>(
        (static_cast<uint32_t>(localX) * 1000U) / layout.bar.w);
  }
  return event;
}
