#ifndef ALARM_APP_H
#define ALARM_APP_H

#include "sys/services/touch_input.h"
#include "ui/ui_helpers.h"

#include <Adafruit_GFX.h>
#include <stdint.h>

/// In-app timer and alarm clock. It intentionally has no background persistence.
class AlarmApp {
public:
  void reset();
  void onEnter();
  void release();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool update();
  bool hasActiveSession() const;

private:
  struct AudioState;
  struct State;

  enum class Tab : uint8_t { Timer, Alarm };

  AudioState *audio = nullptr;
  State *state = nullptr;

  bool ensureState();
  void releaseState();
  bool ensureAudio();
  void releaseAudio();
  bool startChime();
  void stopRinging();
  void adjustTimer(int16_t delta);
  void adjustAlarmHour(int8_t delta);
  void adjustAlarmMinute(int8_t delta);
  void startTimer();
  void stopTimer();
  void drawTimer(Adafruit_GFX &gfx);
  void drawAlarm(Adafruit_GFX &gfx);
};

#endif
