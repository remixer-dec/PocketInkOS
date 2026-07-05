#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include <Adafruit_GFX.h>
#include "sys/touch_input.h"

class SettingsApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;

private:
  bool confirmOpen = false;
  bool done = false;

  void resetSystemConfig();
};

#endif
