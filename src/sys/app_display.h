#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "sys/drivers/epaper_driver_bsp.h"
#include "sys/global.h"
#include <Adafruit_GFX.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class AppDisplay : public Adafruit_GFX {
public:
  AppDisplay();
  void begin();
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void drawBlackPixelUnchecked(int16_t x, int16_t y);
  void drawBlackBlock2x2Unchecked(int16_t x, int16_t y);
  void flush();
  void flushPartial(int16_t x, int16_t y, int16_t w, int16_t h);
  void clear();
  void requestFullRefresh();
  void lock();
  void unlock();

private:
  epaper_driver_display driver;
  bool partialReady = false;
  volatile bool locked = false;
};

#endif
