#include "shell/shell_lockscreen_layout.h"

#include "sys/global.h"
#include "sys/system_config.h"
#include "ui/icon_ascii_font.h"

#include <ctype.h>

namespace {

LockscreenLayoutConfig lockscreenConfig;

bool asciiEqualIgnoreCase(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    const char ca = static_cast<char>(tolower(static_cast<unsigned char>(*a)));
    const char cb = static_cast<char>(tolower(static_cast<unsigned char>(*b)));
    if (ca != cb) {
      return false;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

bool parseBoolValue(const char *value, bool fallback) {
  if (asciiEqualIgnoreCase(value, "1") || asciiEqualIgnoreCase(value, "true") ||
      asciiEqualIgnoreCase(value, "yes") || asciiEqualIgnoreCase(value, "on")) {
    return true;
  }
  if (asciiEqualIgnoreCase(value, "0") ||
      asciiEqualIgnoreCase(value, "false") ||
      asciiEqualIgnoreCase(value, "no") || asciiEqualIgnoreCase(value, "off")) {
    return false;
  }
  return fallback;
}

void drawCenteredText(AppDisplay &display, const char *text, int16_t y) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  display.setCursor((EPD_WIDTH - textW) / 2 - textX, y);
  display.print(text);
}

void drawCenteredIcon(AppDisplay &display, char icon, int16_t y,
                      uint8_t textSize = 1) {
  const char text[2] = {icon, '\0'};
  display.setFont(&iconASCII12pt7b);
  display.setTextSize(textSize);
  display.setTextColor(1);
  int16_t iconX;
  int16_t iconY;
  uint16_t iconW;
  uint16_t iconH;
  display.getTextBounds(text, 0, 0, &iconX, &iconY, &iconW, &iconH);
  display.setCursor((EPD_WIDTH - static_cast<int16_t>(iconW)) / 2 - iconX, y);
  display.print(text);
  display.setFont();
  display.setTextSize(1);
}

} // namespace

void lockscreenLayoutConfigReload() {
  lockscreenConfig = LockscreenLayoutConfig{};
  char value[CONFIG_VALUE_MAX];
  if (configGet("lockscreen.topIcon", value, sizeof(value)) &&
      value[0] != '\0') {
    lockscreenConfig.topIcon = value[0];
  }
  if (configGet("lockscreen.date", value, sizeof(value))) {
    lockscreenConfig.showDate =
        parseBoolValue(value, lockscreenConfig.showDate);
  }
}

const LockscreenLayoutConfig &lockscreenLayoutConfig() {
  return lockscreenConfig;
}

void drawLockscreenSleepScreen(AppDisplay &display) {
  display.fillScreen(0);
  drawCenteredIcon(display, lockscreenConfig.topIcon, 114, 2);
}

void drawLockscreenClockScreen(AppDisplay &display, const char *timeText,
                               const char *dateText, bool timeEstimated) {
  display.fillScreen(0);
  if (lockscreenConfig.topIcon != '\0') {
    drawCenteredIcon(display, lockscreenConfig.topIcon, 42, 1);
  }

  display.setFont();
  display.setTextColor(1);
  display.setTextSize(4);
  int16_t timeX;
  int16_t timeY;
  uint16_t timeW;
  uint16_t timeH;
  const char *visibleTime = timeText != nullptr ? timeText : "";
  display.getTextBounds(visibleTime, 0, 0, &timeX, &timeY, &timeW, &timeH);
  // "12:34 AM" does not fit at the 24-hour clock's scale. Measure first so
  // both formats remain genuinely centered instead of clipping at the edge.
  if (timeW > EPD_WIDTH - 12) {
    display.setTextSize(3);
  }
  display.getTextBounds(visibleTime, 0, 104, &timeX, &timeY, &timeW, &timeH);
  drawCenteredText(display, visibleTime, 104);
  if (timeEstimated && timeW > 0) {
    const int16_t lineX = (EPD_WIDTH - static_cast<int16_t>(timeW)) / 2;
    display.drawLine(lineX, timeY - 3,
                     lineX + static_cast<int16_t>(timeW) - 1, timeY - 3, 1);
  }
  if (lockscreenConfig.showDate) {
    display.setTextSize(1);
    drawCenteredText(display, dateText != nullptr ? dateText : "", 140);
  }
  display.setTextSize(1);
}
