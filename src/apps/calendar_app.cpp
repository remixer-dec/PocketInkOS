#include "apps/calendar_app.h"
#include "sys/device_clock.h"
#include "sys/global.h"

void CalendarApp::reset() {
  loadToday();
  picker.setToday(today, clockReady);
  picker.setTimePickerEnabled(false);
  picker.reset(today);
}

void CalendarApp::draw(Adafruit_GFX &gfx) {
  picker.draw(gfx);
  if (!clockReady) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    gfx.getTextBounds("clock not set", 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(textW)) / 2 - textX, 190);
    gfx.print("clock not set");
  }
}

bool CalendarApp::handleTouch(const TouchPoint &point) {
  return picker.handleTouch(point);
}

bool CalendarApp::update() { return picker.update(); }

bool CalendarApp::hasActiveSession() const {
  DatePickerDate selected = picker.selectedDate();
  return clockReady && (selected.year != today.year ||
                        selected.month != today.month ||
                        selected.day != today.day);
}

void CalendarApp::loadToday() {
  int64_t localUnix = 0;
  clockReady = deviceClock.snapshotLocalUnix(localUnix);
  if (!clockReady) {
    today = DatePickerDate();
    return;
  }

  int64_t days = localUnix / 86400;
  if (localUnix < 0 && localUnix % 86400 != 0) {
    days--;
  }
  today = DateTimePickerComponent::dateFromUnixDays(static_cast<int>(days));
}
