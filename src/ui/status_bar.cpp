#include "ui/status_bar.h"
#include "ui/icon_ascii_font.h"
#include "sys/global.h"
#include "sys/drivers/usb_status.h"
#include "sys/services/connectivity_service.h"
#include "sys/services/device_clock.h"

namespace {

static const int16_t kStatusBarHeight = 20;
static const int16_t kStatusDividerY = kStatusBarHeight - 3;
static const int16_t kStatusTextY = 5;
static const int16_t kStatusIconY = 14;

char batteryIconCharForPercentage(int percentage) {
  if (percentage >= 90) {
    return 'B';
  }
  if (percentage >= 65) {
    return 'C';
  }
  if (percentage >= 35) {
    return 'D';
  }
  return 'E';
}

void drawGlyph(Adafruit_GFX &display, char glyph, int16_t &cursorX) {
  char text[2] = {glyph, '\0'};
  int16_t glyphX;
  int16_t glyphY;
  uint16_t glyphW;
  uint16_t glyphH;
  display.getTextBounds(text, 0, 0, &glyphX, &glyphY, &glyphW, &glyphH);
  cursorX -= static_cast<int16_t>(glyphW);
  display.setCursor(cursorX - glyphX, kStatusIconY);
  display.print(text);
}

int16_t glyphWidth(Adafruit_GFX &display, char glyph) {
  char text[2] = {glyph, '\0'};
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  return static_cast<int16_t>(textW);
}

void drawRightAlignedText(Adafruit_GFX &display, const char *text, int16_t y,
                          int16_t rightX) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  display.setCursor(rightX - textW - textX, y);
  display.print(text);
}

} // namespace

void captureStatusBarSnapshot(StatusBarSnapshot &status, char *dateText,
                              size_t dateSize, char *timeText,
                              size_t timeSize) {
  if (dateText != nullptr && dateSize > 0) {
    dateText[0] = '\0';
  }
  if (timeText != nullptr && timeSize > 0) {
    timeText[0] = '\0';
  }
  if (deviceClock.isSet()) {
    if (dateText != nullptr && dateSize > 0) {
      deviceClock.formatDate(dateText, dateSize);
    }
    if (timeText != nullptr && timeSize > 0) {
      deviceClock.formatTime(timeText, timeSize);
    }
  }

  const BatterySnapshot &battery = batteryMonitor.snapshot();
  status = {};
  status.dateText = dateText;
  status.timeText = timeText;
  status.battery = &battery;
  status.wifiIcon = wifiStatusIcon();
  status.usbConnected = usbConnected();
}

void drawStatusBar(Adafruit_GFX &display) {
  char dateText[16] = "";
  char timeText[16] = "";
  StatusBarSnapshot status = {};
  captureStatusBarSnapshot(status, dateText, sizeof(dateText), timeText,
                           sizeof(timeText));
  drawStatusBar(display, status);
}

void drawStatusBar(Adafruit_GFX &display, const StatusBarSnapshot &status) {
  display.fillRect(0, 0, EPD_WIDTH, kStatusBarHeight, 0);
  display.setTextColor(1);
  display.setTextSize(1);

  if (status.dateText != nullptr && status.dateText[0] != '\0') {
    display.setCursor(4, kStatusTextY);
    display.print(status.dateText);
  }

  display.setFont(&iconASCII12pt7b);
  display.setTextSize(1);

  int16_t cursorX = EPD_WIDTH - 2;
  const int16_t spacing = 5;
  const char batteryGlyph =
      status.usbConnected
          ? 'A'
          : (status.battery != nullptr && status.battery->valid
                 ? batteryIconCharForPercentage(status.battery->percentage)
                 : 'E');
  const bool hasWifi = status.wifiIcon != '\0';
  const int16_t batteryW = glyphWidth(display, batteryGlyph);
  const int16_t wifiW = hasWifi ? glyphWidth(display, status.wifiIcon) : 0;
  const int16_t iconBlockW = batteryW + (hasWifi ? spacing + wifiW : 0);
  const int16_t iconLeftX = cursorX - iconBlockW;

  if (status.timeText != nullptr && status.timeText[0] != '\0') {
    display.setFont();
    drawRightAlignedText(display, status.timeText, kStatusTextY,
                         iconLeftX - 4);
    display.setFont(&iconASCII12pt7b);
  }

  drawGlyph(display, batteryGlyph, cursorX);
  if (status.wifiIcon != '\0') {
    cursorX -= spacing;
    drawGlyph(display, status.wifiIcon, cursorX);
  }

  display.setFont();
  display.drawLine(0, kStatusDividerY, EPD_WIDTH - 1, kStatusDividerY, 1);
}
