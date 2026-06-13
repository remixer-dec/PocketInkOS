#include "ui/status_bar.h"
#include "ui/icon_ascii_font.h"
#include "sys/global.h"

namespace {

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

void drawGlyph(AppDisplay &display, char glyph, int16_t &cursorX) {
  char text[2] = {glyph, '\0'};
  int16_t glyphX;
  int16_t glyphY;
  uint16_t glyphW;
  uint16_t glyphH;
  display.getTextBounds(text, 0, 0, &glyphX, &glyphY, &glyphW, &glyphH);
  cursorX -= static_cast<int16_t>(glyphW);
  display.setCursor(cursorX - glyphX, 11);
  display.print(text);
}

int16_t glyphWidth(AppDisplay &display, char glyph) {
  char text[2] = {glyph, '\0'};
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  return static_cast<int16_t>(textW);
}

void drawRightAlignedText(AppDisplay &display, const char *text, int16_t y,
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

void drawStatusBar(AppDisplay &display, const StatusBarSnapshot &status) {
  display.setTextColor(1);
  display.setTextSize(1);

  if (status.dateText != nullptr && status.dateText[0] != '\0') {
    display.setCursor(4, 8);
    display.print(status.dateText);
  }

  display.setFont(&iconASCII8pt7b);
  display.setTextSize(1);

  int16_t cursorX = EPD_WIDTH - 8;
  const int16_t spacing = 5;
  const char batteryGlyph =
      status.battery != nullptr && status.battery->valid
          ? batteryIconCharForPercentage(status.battery->percentage)
          : 'E';
  const bool hasWifi = status.wifiIcon != '\0';
  const int16_t batteryW = glyphWidth(display, batteryGlyph);
  const int16_t wifiW = hasWifi ? glyphWidth(display, status.wifiIcon) : 0;
  const int16_t iconBlockW = batteryW + (hasWifi ? spacing + wifiW : 0);
  const int16_t iconLeftX = cursorX - iconBlockW;

  if (status.timeText != nullptr && status.timeText[0] != '\0') {
    display.setFont();
    drawRightAlignedText(display, status.timeText, 8, iconLeftX - 4);
    display.setFont(&iconASCII8pt7b);
  }

  drawGlyph(display, batteryGlyph, cursorX);
  if (status.wifiIcon != '\0') {
    cursorX -= spacing;
    drawGlyph(display, status.wifiIcon, cursorX);
  }

  display.setFont();
}
