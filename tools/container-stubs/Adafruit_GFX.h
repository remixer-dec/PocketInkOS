#pragma once
#include "Arduino.h"
struct GFXglyph {
  uint16_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;
  int8_t yOffset;
};
struct GFXfont {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
};
class Adafruit_GFX {
public:
  struct TextCall {
    uint16_t color = 0;
    int16_t x = 0;
    int16_t y = 0;
    uint8_t size = 1;
  };

  Adafruit_GFX(int16_t, int16_t) {}
  virtual ~Adafruit_GFX() = default;
  virtual void drawPixel(int16_t, int16_t, uint16_t) {}
  void setFont(const GFXfont * = nullptr) {}
  void fillScreen(uint16_t) {}
  void setTextColor(uint16_t color) { textColor = color; }
  void setTextSize(uint8_t size) { textSize = size; }
  void setCursor(int16_t x, int16_t y) { cursorX = x; cursorY = y; }
  int16_t getCursorX() const { return cursorX; }
  int16_t getCursorY() const { return cursorY; }
  int16_t width() const { return 200; }
  int16_t height() const { return 200; }
  virtual void drawFastHLine(int16_t, int16_t, int16_t, uint16_t) {}
  void print(const char *) {
    if (textCallCount < sizeof(textCalls) / sizeof(textCalls[0])) {
      textCalls[textCallCount++] = {textColor, cursorX, cursorY, textSize};
    }
  }
  void print(char) {}
  void print(const String &) {}
  void print(int) {}
  void print(float, int = 2) {}
  void getTextBounds(const char *text, int16_t x, int16_t y, int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) { if (x1) *x1 = x; if (y1) *y1 = y; if (w) *w = text ? strlen(text) * 6 * textSize : 0; if (h) *h = 8 * textSize; }
  void drawRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawRoundRect(int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void fillRoundRect(int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawLine(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawCircle(int16_t, int16_t, int16_t, uint16_t) {}
  void fillCircle(int16_t, int16_t, int16_t, uint16_t) {}

  TextCall textCalls[64] = {};
  size_t textCallCount = 0;

private:
  uint16_t textColor = 0;
  uint8_t textSize = 1;
  int16_t cursorX = 0;
  int16_t cursorY = 0;
};
