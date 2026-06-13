#ifndef DEGHOST_APP_H
#define DEGHOST_APP_H

#include "sys/app_display.h"
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
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
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
  bool running = false;
  bool finished = false;
  uint8_t stepIndex = 0;
  unsigned long stepStartedAt = 0;
  unsigned long lastAnimAt = 0;
  uint8_t zebraFrame = 0;
  uint8_t noiseFrame = 0;

  void beginSequence();
  void advanceStep();
  uint8_t stepCount() const;
  Pattern patternForStep(uint8_t index) const;
  uint16_t holdMsForStep(uint8_t index) const;
  bool isZebraPattern(Pattern pattern) const;
  bool isNoisePattern(Pattern pattern) const;
  void drawPattern(Adafruit_GFX &gfx, Pattern pattern);
  void drawZebra(Adafruit_GFX &gfx, int dx, int dy, int phasePx);
  void drawNoiseClassic(Adafruit_GFX &gfx);
  void drawNoiseSeeded(Adafruit_GFX &gfx, uint32_t salt);
};

#endif
