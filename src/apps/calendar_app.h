#ifndef CALENDAR_APP_H
#define CALENDAR_APP_H

#include "sys/touch_input.h"
#include "sys/app_config.h"
#include "ui/components/date_time_picker_component.h"
#include <Adafruit_GFX.h>

class CalendarApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool update();
  bool hasActiveSession() const;

private:
  DateTimePickerComponent picker;
  DatePickerDate today;
  bool clockReady = false;

  void loadToday();
};

/// Registers Calendar's read-only config schema with the Settings app.
void calendarAppConfigRegister();

#endif
