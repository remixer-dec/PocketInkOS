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
  void drawRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawLine(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
  void drawCircle(int16_t, int16_t, int16_t, uint16_t) {}
};
