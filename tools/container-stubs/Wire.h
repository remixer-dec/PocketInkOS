#pragma once
#include "Arduino.h"
class TwoWire {
public:
  void begin(int, int, uint32_t = 100000) {}
  void setTimeOut(uint16_t) {}
  void beginTransmission(uint8_t) {}
  void write(uint8_t) {}
  int endTransmission() { return 0; }
  int endTransmission(bool) { return 0; }
  int requestFrom(uint8_t, int n) { return n; }
  int requestFrom(uint8_t, int n, bool) { return n; }
  int available() { return 0; }
  uint8_t read() { return 0; }
};
inline TwoWire Wire;
