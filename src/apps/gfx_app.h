#ifndef GFX_APP_H
#define GFX_APP_H

#include "sys/app_display.h"
#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class GfxApp {
public:
  void reset();
  void start(AppDisplay &display);
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handlePowerButton();
  bool hasActiveSession() const;

private:
  static const uint8_t SHADER_COUNT = 8;

  AppDisplay *display = nullptr;
  uint8_t shaderIndex = 0;
  bool running = false;
  unsigned long lastFrameAt = 0;
  float timeSec = 0.0f;
  uint32_t frame = 0;

  uint16_t frameIntervalMs() const;
  void drawShader(Adafruit_GFX &gfx);
  float sampleShader(int16_t x, int16_t y, float t) const;
  void drawCubeShader(Adafruit_GFX &gfx);
};

#endif
