#pragma once
#include "Arduino.h"
namespace Bounce2 {
class Button {
public:
  void attach(uint8_t, int) {}
  void interval(uint16_t) {}
  void setPressedState(int) {}
  void update() {}
  bool pressed() const { return false; }
  bool isPressed() const { return false; }
  bool released() const { return false; }
};
}
