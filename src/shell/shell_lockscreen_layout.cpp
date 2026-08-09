#include "shell/shell_lockscreen_layout.h"

#include "sys/global.h"
#include "sys/services/lockscreen_content.h"
#include "sys/system_config.h"
#include "ui/icon_ascii_font.h"

#include <ctype.h>
#include <cstring>

namespace {

LockscreenLayoutConfig lockscreenConfig;

constexpr int16_t kClockY = 104;
constexpr uint8_t kClockTextSize = 4;
constexpr uint8_t kPeriodTextSize = 2;
constexpr int16_t kPeriodGap = 4;

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

bool splitTimePeriod(const char *text, char *time, size_t timeSize,
                     const char *&period) {
  period = "";
  if (text == nullptr || time == nullptr || timeSize == 0) {
    return false;
  }
  const size_t length = strlen(text);
  const bool hasPeriod =
      length >= 3 && text[length - 3] == ' ' && text[length - 1] == 'M' &&
      (text[length - 2] == 'A' || text[length - 2] == 'P');
  const size_t timeLength = hasPeriod ? length - 3 : length;
  if (timeLength >= timeSize) {
    return false;
  }
  memcpy(time, text, timeLength);
  time[timeLength] = '\0';
  if (hasPeriod) {
    period = text + length - 2;
  }
  return true;
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
  const bool contentDrawn = drawLockscreenContent(display);
  if (!contentDrawn && lockscreenConfig.topIcon != '\0') {
    drawCenteredIcon(display, lockscreenConfig.topIcon, 42, 1);
  }

  display.setFont();
  display.setTextColor(1);
  char visibleTime[16];
  const char *period = "";
  if (!splitTimePeriod(timeText != nullptr ? timeText : "", visibleTime,
                       sizeof(visibleTime), period)) {
    visibleTime[0] = '\0';
  }

  display.setTextSize(kClockTextSize);
  int16_t timeX;
  int16_t timeY;
  uint16_t timeW;
  uint16_t timeH;
  display.getTextBounds(visibleTime, 0, kClockY, &timeX, &timeY, &timeW,
                        &timeH);

  display.setTextSize(kPeriodTextSize);
  int16_t periodX;
  int16_t periodY;
  uint16_t periodW;
  uint16_t periodH;
  display.getTextBounds(period, 0, 0, &periodX, &periodY, &periodW, &periodH);

  const int16_t gap = periodW > 0 ? kPeriodGap : 0;
  const int16_t groupW = static_cast<int16_t>(timeW) + gap +
                         static_cast<int16_t>(periodW);
  const int16_t timeLeft = (EPD_WIDTH - groupW) / 2;

  display.setTextSize(kClockTextSize);
  display.setCursor(timeLeft - timeX, kClockY);
  display.print(visibleTime);
  if (periodW > 0) {
    const int16_t periodLeft =
        timeLeft + static_cast<int16_t>(timeW) + gap;
    const int16_t timeBottom = timeY + static_cast<int16_t>(timeH);
    display.setTextSize(kPeriodTextSize);
    display.setCursor(periodLeft - periodX,
                      timeBottom - static_cast<int16_t>(periodH) - periodY);
    display.print(period);
  }
  if (timeEstimated && timeW > 0) {
    display.drawLine(timeLeft, timeY - 3,
                     timeLeft + static_cast<int16_t>(timeW) - 1, timeY - 3,
                     1);
  }
  if (lockscreenConfig.showDate) {
    display.setTextSize(1);
    drawCenteredText(display, dateText != nullptr ? dateText : "", 140);
  }
  display.setTextSize(1);
}
