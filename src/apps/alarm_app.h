#ifndef ALARM_APP_H
#define ALARM_APP_H

#include "sys/app_runtime.h"
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
  /// Level-1 periodic hook that decides whether the interactive alarm must wake.
  static PeriodicTaskAction onPeriodic(const PeriodicTaskContext &context);
  /// Renders the compact alarm state retained by the periodic hook.
  static void drawWidget(Adafruit_GFX &gfx, const uint8_t *value,
                         size_t valueSize, int16_t x, int16_t y,
                         int16_t width, int16_t height);

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
  void restorePeriodicState();
  void savePeriodicState() const;
  void startTimer();
  void stopTimer();
  void drawTimer(Adafruit_GFX &gfx);
  void drawAlarm(Adafruit_GFX &gfx);
};

#endif
