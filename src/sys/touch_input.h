#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>
#include <driver/i2c_master.h>

struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

enum TouchEventType {
  TOUCH_EVENT_DOWN,
  TOUCH_EVENT_MOVE,
  TOUCH_EVENT_UP,
};

struct TouchEvent {
  TouchEventType type;
  TouchPoint point;
};

class TouchInput {
public:
  void begin();
  bool read(TouchPoint &point);
  bool readEvent(TouchEvent &event);
};

bool touchI2cBegin();
i2c_master_bus_handle_t touchI2cBusHandle();

#endif
