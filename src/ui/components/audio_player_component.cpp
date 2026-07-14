#include "ui/components/audio_player_component.h"

#include "sys/services/touch_input.h"
#include "sys/system_config.h"
#include "ui/components/touch_button.h"

#include <cstdio>
#include <cstring>
#include <atomic>

namespace {

std::atomic<bool> volumeFeedbackPending{false};

struct PlayerGeometry {
  UiRect artwork = {};
  UiRect title = {};
  UiRect subtitle = {};
  UiRect detail = {};
  UiRect previous = {};
  UiRect play = {};
  UiRect next = {};
  UiRect volumeDown = {};
  UiRect volumeUp = {};
  UiRect bar = {};
  UiRect time = {};
  UiRect volumeMeter = {};
};

bool present(const UiRect &rect) { return rect.w > 0 && rect.h > 0; }

PlayerGeometry geometryFor(AudioPlayerLayoutStyle style, bool fullscreen) {
  const int16_t top = fullscreen ? 4 : 23;
  PlayerGeometry layout;
  switch (style) {
  case AudioPlayerLayoutStyle::Artwork:
    layout.artwork = {0, 0, 200, 200};
    layout.title = {8, 75, 184, 18};
    layout.subtitle = {8, 94, 184, 11};
    layout.bar = {8, 108, 184, 20};
    layout.time = {8, 126, 184, 12};
    layout.previous = {8, 146, 52, 46};
    layout.play = {70, 138, 60, 60};
    layout.next = {140, 146, 52, 46};
    break;
  case AudioPlayerLayoutStyle::Minimal:
    layout.artwork = {0, 0, 200, 200};
    layout.title = {8, static_cast<int16_t>(top + 18), 184, 24};
    layout.subtitle = {8, static_cast<int16_t>(top + 45), 184, 12};
    layout.detail = {8, static_cast<int16_t>(top + 60), 184, 11};
    layout.bar = {8, 99, 184, 20};
    layout.time = {8, 117, 184, 12};
    layout.volumeDown = {12, 139, 78, 51};
    layout.volumeUp = {110, 139, 78, 51};
    break;
  case AudioPlayerLayoutStyle::Mixer:
    layout.artwork = {8, top, 56, 56};
    layout.title = {72, static_cast<int16_t>(top + 5), 120, 20};
    layout.subtitle = {72, static_cast<int16_t>(top + 28), 120, 11};
    layout.detail = {72, static_cast<int16_t>(top + 41), 120, 10};
    layout.bar = {8, 80, 184, 20};
    layout.time = {8, 98, 184, 12};
    layout.previous = {8, 125, 45, 36};
    layout.play = {67, 107, 66, 66};
    layout.next = {147, 125, 45, 36};
    layout.volumeMeter = {55, 176, 90, 18};
    layout.volumeDown = {2, 166, 48, 30};
    layout.volumeUp = {150, 166, 48, 30};
    break;
  case AudioPlayerLayoutStyle::Controls:
    layout.title = {5, static_cast<int16_t>(fullscreen ? 31 : 39), 190, 30};
    layout.subtitle = {8, static_cast<int16_t>(fullscreen ? 66 : 72), 184, 13};
    layout.detail = {8, static_cast<int16_t>(fullscreen ? 81 : 86), 184, 10};
    layout.bar = {8, 101, 184, 20};
    layout.time = {8, 119, 184, 12};
    layout.volumeDown = {4, 139, 36, 53};
    layout.previous = {43, 139, 36, 53};
    layout.play = {82, 139, 36, 53};
    layout.next = {121, 139, 36, 53};
    layout.volumeUp = {160, 139, 36, 53};
    break;
  }
  return layout;
}

bool contains(const UiRect &rect, const TouchPoint &point) {
  return present(rect) && TouchButton(rect).contains(point);
}

void copyFittedText(Adafruit_GFX &gfx, const char *text, int16_t maxWidth,
                    char *buffer, size_t bufferSize) {
  if (bufferSize == 0) {
    return;
  }
  const char *source = text != nullptr ? text : "";
  strncpy(buffer, source, bufferSize - 1);
  buffer[bufferSize - 1] = '\0';

  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.getTextBounds(buffer, 0, 0, &x, &y, &width, &height);
  while (buffer[0] != '\0' && width > static_cast<uint16_t>(maxWidth)) {
    const size_t length = strlen(buffer);
    if (length <= 4) {
      buffer[0] = '\0';
      break;
    }
    buffer[length - 4] = '.';
    buffer[length - 3] = '.';
    buffer[length - 2] = '.';
    buffer[length - 1] = '\0';
    gfx.getTextBounds(buffer, 0, 0, &x, &y, &width, &height);
  }
}

void printPlayerText(Adafruit_GFX &gfx, const char *text, int16_t x, int16_t y,
                     bool overArtwork) {
  if (overArtwork) {
    gfx.setTextColor(1);
    gfx.setCursor(x - 1, y - 1);
    gfx.print(text);
    gfx.setCursor(x + 1, y + 1);
    gfx.print(text);
    gfx.setCursor(x - 1, y + 1);
    gfx.print(text);
    gfx.setCursor(x + 1, y - 1);
    gfx.print(text);
  }
  gfx.setTextColor(overArtwork ? 0 : 1);
  gfx.setCursor(x, y);
  gfx.print(text);
}

void drawFittedText(Adafruit_GFX &gfx, const UiRect &rect, const char *text,
                    uint8_t textSize, bool centered, bool overArtwork) {
  if (!present(rect) || text == nullptr || text[0] == '\0') {
    return;
  }
  gfx.setFont();
  gfx.setTextSize(textSize);
  char buffer[64];
  copyFittedText(gfx, text, rect.w, buffer, sizeof(buffer));
  if (buffer[0] == '\0') {
    return;
  }
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(buffer, 0, 0, &textX, &textY, &textW, &textH);
  // The built-in font bounds include one trailing spacing column. Excluding
  // it avoids the optical left shift that is most visible at text size 2.
  const uint16_t visualTextW =
      centered && textW > textSize ? textW - textSize : textW;
  const int16_t x = centered
                        ? rect.x +
                              (rect.w - static_cast<int16_t>(visualTextW)) / 2 -
                              textX
                        : rect.x - textX;
  const int16_t y = rect.y + (rect.h - static_cast<int16_t>(textH)) / 2 - textY;
  printPlayerText(gfx, buffer, x, y, overArtwork);
  gfx.setTextSize(1);
}

void drawPlayerTitle(Adafruit_GFX &gfx, const UiRect &rect, const char *text,
                     bool overArtwork) {
  if (!present(rect) || text == nullptr || text[0] == '\0') {
    return;
  }
  gfx.setFont();
  gfx.setTextSize(2);
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.getTextBounds(text, 0, 0, &x, &y, &width, &height);
  drawFittedText(gfx, rect, text, width <= static_cast<uint16_t>(rect.w) ? 2 : 1,
                 true, overArtwork);
}

void drawTime(Adafruit_GFX &gfx, const UiRect &rect, uint32_t positionMs,
              uint32_t durationMs, bool overArtwork) {
  char left[12];
  char right[12];
  const uint32_t positionSeconds = positionMs / 1000U;
  const uint32_t durationSeconds = durationMs / 1000U;
  snprintf(left, sizeof(left), "%lu:%02lu",
           static_cast<unsigned long>(positionSeconds / 60U),
           static_cast<unsigned long>(positionSeconds % 60U));
  snprintf(right, sizeof(right), "%lu:%02lu",
           static_cast<unsigned long>(durationSeconds / 60U),
           static_cast<unsigned long>(durationSeconds % 60U));
  drawFittedText(
      gfx,
      {rect.x, rect.y, static_cast<int16_t>(rect.w / 2), rect.h}, left, 1,
      false, overArtwork);

  gfx.setTextSize(1);
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(right, 0, 0, &textX, &textY, &textW, &textH);
  const int16_t x =
      rect.x + rect.w - static_cast<int16_t>(textW) - textX;
  const int16_t y =
      rect.y + (rect.h - static_cast<int16_t>(textH)) / 2 - textY;
  printPlayerText(gfx, right, x, y, overArtwork);
}

void drawProgress(Adafruit_GFX &gfx, const UiRect &rect, uint32_t positionMs,
                  uint32_t durationMs, bool overArtwork) {
  const int16_t trackX = rect.x;
  const int16_t trackW = rect.w;
  const int16_t centerY = rect.y + rect.h / 2;
  if (overArtwork) {
    gfx.fillRoundRect(trackX - 1, centerY - 4, trackW + 2, 9, 4, 0);
  }
  gfx.drawRect(trackX, centerY - 3, trackW, 7, 1);
  const uint32_t clamped = durationMs > 0 && positionMs > durationMs
                               ? durationMs
                               : positionMs;
  int16_t thumbX = trackX;
  if (durationMs > 0 && trackW > 1) {
    thumbX = trackX + static_cast<int16_t>(
                          (static_cast<uint64_t>(clamped) * (trackW - 1)) /
                          durationMs);
    const int16_t fill = thumbX - trackX;
    if (fill > 1) {
      gfx.fillRect(trackX + 1, centerY - 2, fill - 1, 5, 1);
    }
  }
  if (overArtwork) {
    gfx.fillCircle(thumbX, centerY, 7, 0);
  }
  gfx.fillCircle(thumbX, centerY, 5, 0);
  gfx.drawCircle(thumbX, centerY, 5, 1);
}

void drawTriangle(Adafruit_GFX &gfx, int16_t centerX, int16_t centerY,
                  bool pointsRight, uint8_t color) {
  for (int16_t dy = -8; dy <= 8; dy++) {
    const int16_t span = 11 - (static_cast<int16_t>(dy < 0 ? -dy : dy) * 11) / 8;
    if (pointsRight) {
      gfx.drawLine(centerX - 6, centerY + dy, centerX - 6 + span,
                   centerY + dy, color);
    } else {
      gfx.drawLine(centerX + 6 - span, centerY + dy, centerX + 6,
                   centerY + dy, color);
    }
  }
}

void drawSpeaker(Adafruit_GFX &gfx, int16_t centerX, int16_t centerY,
                 bool plus, uint8_t color) {
  gfx.fillRect(centerX - 10, centerY - 3, 4, 7, color);
  for (int16_t x = 0; x < 7; x++) {
    const int16_t halfHeight = 3 + x / 2;
    gfx.drawLine(centerX - 6 + x, centerY - halfHeight,
                 centerX - 6 + x, centerY + halfHeight, color);
  }
  gfx.drawLine(centerX + 5, centerY, centerX + 12, centerY, color);
  if (plus) {
    gfx.drawLine(centerX + 9, centerY - 4, centerX + 9, centerY + 4, color);
  }
}

void drawMediaIcon(Adafruit_GFX &gfx, const UiRect &rect,
                   AudioPlayerAction action, bool playing, uint8_t color) {
  const int16_t centerX = rect.x + rect.w / 2;
  const int16_t centerY = rect.y + rect.h / 2;
  switch (action) {
  case AudioPlayerAction::TogglePlayPause:
    if (playing) {
      gfx.fillRect(centerX - 7, centerY - 9, 5, 18, color);
      gfx.fillRect(centerX + 3, centerY - 9, 5, 18, color);
    } else {
      drawTriangle(gfx, centerX + 1, centerY, true, color);
    }
    break;
  case AudioPlayerAction::PreviousTrack:
    gfx.fillRect(centerX - 10, centerY - 9, 3, 18, color);
    drawTriangle(gfx, centerX + 1, centerY, false, color);
    break;
  case AudioPlayerAction::NextTrack:
    drawTriangle(gfx, centerX - 1, centerY, true, color);
    gfx.fillRect(centerX + 8, centerY - 9, 3, 18, color);
    break;
  case AudioPlayerAction::VolumeDown:
    drawSpeaker(gfx, centerX, centerY, false, color);
    break;
  case AudioPlayerAction::VolumeUp:
    drawSpeaker(gfx, centerX, centerY, true, color);
    break;
  case AudioPlayerAction::None:
  case AudioPlayerAction::Seek:
    break;
  }
}

void fillButtonShape(Adafruit_GFX &gfx, const UiRect &rect, bool circular,
                     uint8_t color) {
  if (circular) {
    gfx.fillCircle(rect.x + rect.w / 2, rect.y + rect.h / 2,
                   (rect.w < rect.h ? rect.w : rect.h) / 2, color);
  } else {
    gfx.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, color);
  }
}

void outlineButtonShape(Adafruit_GFX &gfx, const UiRect &rect, bool circular,
                        uint8_t color) {
  if (circular) {
    gfx.drawCircle(rect.x + rect.w / 2, rect.y + rect.h / 2,
                   (rect.w < rect.h ? rect.w : rect.h) / 2, color);
  } else {
    gfx.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 6, color);
  }
}

void drawButton(Adafruit_GFX &gfx, const UiRect &rect,
                AudioPlayerAction action, const AudioPlayerState &state,
                AudioPlayerLayoutStyle layoutStyle, bool enabled = true) {
  if (!present(rect)) {
    return;
  }
  // E-paper cannot provide timely press animation. Keep player controls stable
  // so a tap causes only the action redraw, not a delayed inversion redraw.
  const bool trackFeedback =
      state.pressedAction == action &&
      (action == AudioPlayerAction::PreviousTrack ||
       action == AudioPlayerAction::NextTrack);
  const bool inverted =
      (layoutStyle == AudioPlayerLayoutStyle::Artwork) != trackFeedback;
  const bool circular =
      action == AudioPlayerAction::TogglePlayPause &&
      (layoutStyle == AudioPlayerLayoutStyle::Artwork ||
       layoutStyle == AudioPlayerLayoutStyle::Mixer);
  fillButtonShape(gfx, rect, circular, inverted ? 1 : 0);
  outlineButtonShape(gfx, rect, circular, inverted ? 0 : 1);
  drawMediaIcon(gfx, rect, action, state.playing, inverted ? 0 : 1);
  if (!enabled) {
    for (int16_t x = rect.x + 3; x < rect.x + rect.w - 3; x += 4) {
      gfx.drawPixel(x, rect.y + rect.h - 4, inverted ? 0 : 1);
    }
  }
}

bool drawArtwork(Adafruit_GFX &gfx, const UiRect &rect,
                 const AudioPlayerState &state) {
  if (!present(rect)) {
    return false;
  }
  if (state.renderArtwork != nullptr &&
      state.renderArtwork(state.artworkUser, gfx, rect.x, rect.y, rect.w,
                          rect.h)) {
    return true;
  }
  if (state.artwork == nullptr || state.artworkWidth == 0 ||
      state.artworkHeight == 0 || state.artworkWidth > rect.w - 4 ||
      state.artworkHeight > rect.h - 4) {
    return false;
  }
  const int16_t startX = rect.x + (rect.w - state.artworkWidth) / 2;
  const int16_t startY = rect.y + (rect.h - state.artworkHeight) / 2;
  const uint16_t stride = (state.artworkWidth + 7U) / 8U;
  for (uint16_t y = 0; y < state.artworkHeight; y++) {
    for (uint16_t x = 0; x < state.artworkWidth; x++) {
      if ((state.artwork[static_cast<size_t>(y) * stride + x / 8U] &
           (0x80U >> (x & 7U))) != 0) {
        gfx.drawPixel(startX + x, startY + y, 1);
      }
    }
  }
  return true;
}

void drawVolumeMeter(Adafruit_GFX &gfx, const UiRect &rect,
                     const AudioPlayerState &state) {
  if (!present(rect)) {
    return;
  }
  const uint8_t volume = state.muted ? 0 : state.volumePercent;
  const uint8_t filled = static_cast<uint8_t>((volume + 9U) / 10U);
  const int16_t gap = 2;
  const int16_t blockWidth = (rect.w - gap * 9) / 10;
  for (uint8_t i = 0; i < 10; i++) {
    const int16_t x = rect.x + i * (blockWidth + gap);
    if (i < filled) {
      gfx.fillRect(x, rect.y, blockWidth, rect.h, 1);
    } else {
      gfx.drawRect(x, rect.y, blockWidth, rect.h, 1);
    }
  }
}

void drawVolumeFeedback(Adafruit_GFX &gfx, const AudioPlayerState &state,
                        bool overArtwork) {
  char text[12];
  snprintf(text, sizeof(text), "VOL %u%%",
           static_cast<unsigned>(state.muted ? 0U : state.volumePercent));
  gfx.setFont();
  gfx.setTextSize(1);
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  printPlayerText(gfx, text,
                  196 - static_cast<int16_t>(textW) - textX, 4 - textY,
                  overArtwork);
}

} // namespace

AudioPlayerLayoutStyle audioPlayerConfiguredLayout() {
  const char *value = currentSystemConfig().audioPlayerLayout;
  if (strcmp(value, "artwork") == 0) {
    return AudioPlayerLayoutStyle::Artwork;
  }
  if (strcmp(value, "minimal") == 0) {
    return AudioPlayerLayoutStyle::Minimal;
  }
  if (strcmp(value, "mixer") == 0) {
    return AudioPlayerLayoutStyle::Mixer;
  }
  return AudioPlayerLayoutStyle::Controls;
}

void audioPlayerShowVolumeFeedback() { volumeFeedbackPending = true; }

void AudioPlayerComponent::draw(Adafruit_GFX &gfx, const AudioPlayerState &state,
                                bool fullscreen) const {
  const AudioPlayerLayoutStyle style = audioPlayerConfiguredLayout();
  const PlayerGeometry layout = geometryFor(style, fullscreen);
  const uint32_t positionMs =
      state.durationMs > 0 && state.positionMs > state.durationMs
          ? state.durationMs
          : state.positionMs;

  gfx.setFont();
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  if (style == AudioPlayerLayoutStyle::Artwork ||
      style == AudioPlayerLayoutStyle::Minimal) {
    // These layouts replace the file-viewer header. A failed or absent cover
    // therefore falls back to a clean white player, not stale header pixels.
    gfx.fillRect(0, 0, 200, 200, 0);
  }
  drawArtwork(gfx, layout.artwork, state);
  const bool overArtwork = style == AudioPlayerLayoutStyle::Artwork ||
                           style == AudioPlayerLayoutStyle::Minimal;
  drawPlayerTitle(gfx, layout.title,
                  state.title != nullptr ? state.title : "AUDIO",
                  overArtwork);
  drawFittedText(gfx, layout.subtitle, state.subtitle, 1, true, overArtwork);
  drawFittedText(gfx, layout.detail, state.detail, 1, true, overArtwork);
  drawProgress(gfx, layout.bar, positionMs, state.durationMs, overArtwork);
  drawTime(gfx, layout.time, positionMs, state.durationMs, overArtwork);
  if (style == AudioPlayerLayoutStyle::Minimal) {
    const UiRect icon = {87, 116, 26, 18};
    drawMediaIcon(gfx, {86, 115, 26, 18},
                  AudioPlayerAction::TogglePlayPause, state.playing, 0);
    drawMediaIcon(gfx, {88, 117, 26, 18},
                  AudioPlayerAction::TogglePlayPause, state.playing, 0);
    drawMediaIcon(gfx, icon, AudioPlayerAction::TogglePlayPause, state.playing,
                  1);
  }
  drawButton(gfx, layout.previous, AudioPlayerAction::PreviousTrack, state,
             style, state.canPrevious);
  drawButton(gfx, layout.play, AudioPlayerAction::TogglePlayPause, state,
             style);
  drawButton(gfx, layout.next, AudioPlayerAction::NextTrack, state,
             style, state.canNext);
  drawButton(gfx, layout.volumeDown, AudioPlayerAction::VolumeDown, state,
             style);
  drawButton(gfx, layout.volumeUp, AudioPlayerAction::VolumeUp, state, style);
  drawVolumeMeter(gfx, layout.volumeMeter, state);
  if (volumeFeedbackPending.exchange(false)) {
    drawVolumeFeedback(gfx, state, overArtwork);
  }
  gfx.setTextSize(1);
}

AudioPlayerEvent AudioPlayerComponent::hitTest(const TouchPoint &point,
                                               bool fullscreen,
                                               uint32_t durationMs) const {
  const PlayerGeometry layout =
      geometryFor(audioPlayerConfiguredLayout(), fullscreen);
  AudioPlayerEvent event;
  if (contains(layout.play, point)) {
    event.action = AudioPlayerAction::TogglePlayPause;
  } else if (contains(layout.previous, point)) {
    event.action = AudioPlayerAction::PreviousTrack;
  } else if (contains(layout.next, point)) {
    event.action = AudioPlayerAction::NextTrack;
  } else if (contains(layout.volumeDown, point)) {
    event.action = AudioPlayerAction::VolumeDown;
  } else if (contains(layout.volumeUp, point)) {
    event.action = AudioPlayerAction::VolumeUp;
  } else if (durationMs > 0 && contains(layout.bar, point)) {
    const uint16_t localX = point.x > layout.bar.x
                                ? static_cast<uint16_t>(point.x - layout.bar.x)
                                : 0;
    event.action = AudioPlayerAction::Seek;
    event.seekPermille = layout.bar.w > 1
                             ? static_cast<uint16_t>(
                                   (static_cast<uint32_t>(localX) * 1000U) /
                                   static_cast<uint16_t>(layout.bar.w - 1))
                             : 0;
    if (event.seekPermille > 1000U) {
      event.seekPermille = 1000U;
    }
  }
  return event;
}
