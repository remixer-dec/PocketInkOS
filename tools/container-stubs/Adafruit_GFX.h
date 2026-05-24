#pragma once
#include "Arduino.h"
class Adafruit_GFX {
public:
  Adafruit_GFX(int16_t, int16_t) {}
  virtual ~Adafruit_GFX() = default;
  virtual void drawPixel(int16_t, int16_t, uint16_t) {}
  void fillScreen(uint16_t) {}
  void setTextColor(uint16_t) {}
  void setTextSize(uint8_t) {}
  void setCursor(int16_t, int16_t) {}
  void print(const char *) {}
  void print(char) {}
  void print(const String &) {}
  void print(int) {}
  void print(float, int = 2) {}
  void getTextBounds(const char *text, int16_t x, int16_t y, int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) { if (x1) *x1 = x; if (y1) *y1 = y; if (w) *w = text ? strlen(text) * 12 : 0; if (h) *h = 16; }
  void drawRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawLine(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawCircle(int16_t, int16_t, int16_t, uint16_t) {}
};
