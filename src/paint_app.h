#ifndef PAINT_APP_H
#define PAINT_APP_H

#include "app_display.h"
#include "touch_input.h"
#include <Adafruit_GFX.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class PaintApp {
public:
  void reset();
  void start(AppDisplay &display);
  void stop();
  void clear();
  void draw(Adafruit_GFX &gfx);
  void handleTouchEvent(const TouchEvent &event);
  bool hasActiveSession() const;

private:
  enum CommandType {
    COMMAND_TOUCH,
    COMMAND_CLEAR,
    COMMAND_STOP,
  };

  struct Command {
    CommandType type;
    TouchEvent touch;
  };

  AppDisplay *display = nullptr;
  QueueHandle_t queue = NULL;
  TaskHandle_t taskHandle = NULL;
  bool drawing = false;
  bool fallbackMode = false;
  bool hasInk = false;
  TouchPoint lastPoint = {};
  uint8_t canvas[(EPD_WIDTH * EPD_HEIGHT) / 8] = {};

  static void taskEntry(void *context);
  void run();
  void enqueue(const Command &command);
  void handleTouch(const TouchEvent &event);
  void handleFallbackTouch(const TouchEvent &event);
  void drawCanvas(Adafruit_GFX &gfx);
  void drawCanvasLine(TouchPoint from, TouchPoint to);
  void drawCanvasPixel(int16_t x, int16_t y);
  void clearCanvas();
};

#endif
