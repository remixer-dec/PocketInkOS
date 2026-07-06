#ifndef DEGHOST_APP_H
#define DEGHOST_APP_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class DeghostApp {
public:
  void reset();
  void start(AppDisplay &display);
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  AppEventResult handleBackButton();
  bool handlePowerButton();
  AppEventResult consumePendingEvent();
  bool hasActiveSession() const;

private:
  enum Mode : uint8_t {
    MODE_SELECT,
    MODE_QUICK,
    MODE_SLOW,
  };

  enum Pattern {
    PATTERN_WHITE,
    PATTERN_BLACK,
    PATTERN_ZEBRA_0,
    PATTERN_ZEBRA_30,
    PATTERN_ZEBRA_45,
    PATTERN_ZEBRA_60,
    PATTERN_ZEBRA_90,
    PATTERN_ZEBRA_120,
    PATTERN_ZEBRA_150,
    PATTERN_NOISE,
    PATTERN_NOISE_COPY,
  };

  AppDisplay *display = nullptr;
  Mode mode = MODE_SELECT;
  AppEventResult pendingEvent = AppEventResult::Unhandled;
  bool running = false;
  bool finished = false;
  uint8_t stepIndex = 0;
  unsigned long stepStartedAt = 0;
  unsigned long lastAnimAt = 0;
  uint8_t zebraFrame = 0;
  uint8_t noiseFrame = 0;

  void beginSequence(Mode nextMode);
  void advanceStep();
  uint8_t stepCount() const;
  Pattern patternForStep(uint8_t index) const;
  uint16_t holdMsForStep(uint8_t index) const;
  bool isZebraPattern(Pattern pattern) const;
  bool isNoisePattern(Pattern pattern) const;
  bool containsQuickButton(const TouchPoint &point) const;
  bool containsSlowButton(const TouchPoint &point) const;
  void drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                        uint8_t size);
  void drawModeButton(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t w,
                      int16_t h, const char *label);
  void drawModeSelect(Adafruit_GFX &gfx);
  void drawFinished(Adafruit_GFX &gfx);
  void drawPattern(Adafruit_GFX &gfx, Pattern pattern);
  void drawZebra(Adafruit_GFX &gfx, int dx, int dy, int phasePx);
  void drawNoiseClassic(Adafruit_GFX &gfx);
  void drawNoiseSeeded(Adafruit_GFX &gfx, uint32_t salt);
};

#endif
