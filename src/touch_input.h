#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>

struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

class TouchInput {
public:
  void begin();
  bool read(TouchPoint &point);

private:
  bool wasDown = false;
};

#endif
