#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "epaper_driver_bsp.h"
#include "global.h"
#include <Adafruit_GFX.h>

class AppDisplay : public Adafruit_GFX {
public:
  AppDisplay();
  void begin();
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void flush();
  void flushPartial(int16_t x, int16_t y, int16_t w, int16_t h);
  void clear();

private:
  epaper_driver_display driver;
  bool partialReady = false;
};

#endif
