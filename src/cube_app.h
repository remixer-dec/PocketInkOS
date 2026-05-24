#ifndef CUBE_APP_H
#define CUBE_APP_H

#include "touch_input.h"
#include <Adafruit_GFX.h>

class CubeApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;

private:
  float angle = 0.0f;
  unsigned int frame = 0;
  bool started = false;

  void advance();
};

#endif
