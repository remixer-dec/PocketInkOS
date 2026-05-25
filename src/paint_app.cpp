#include "paint_app.h"
#include "global.h"
#include <cstring>

static const int16_t CANVAS_TOP = 18;
static const UBaseType_t PAINT_QUEUE_DEPTH = 128;
static const uint32_t PAINT_TASK_STACK_WORDS = 4096;
static const UBaseType_t PAINT_TASK_PRIORITY = 1;
static const BaseType_t PAINT_TASK_CORE = 1;

static bool inCanvas(const TouchPoint &point) {
  return point.x < EPD_WIDTH && point.y >= CANVAS_TOP && point.y < EPD_HEIGHT;
}

void PaintApp::reset() {
  drawing = false;
  hasInk = false;
  lastPoint = {};
  memset(canvas, 0xFF, sizeof(canvas));
}

void PaintApp::start(AppDisplay &targetDisplay) {
  display = &targetDisplay;
  if (queue == NULL) {
    queue = xQueueCreate(PAINT_QUEUE_DEPTH, sizeof(Command));
  }
  if (queue != NULL) {
    xQueueReset(queue);
  }
  if (taskHandle == NULL && queue != NULL) {
    TaskHandle_t createdTask = NULL;
    fallbackMode =
        xTaskCreatePinnedToCore(taskEntry, "paint", PAINT_TASK_STACK_WORDS,
                                this, PAINT_TASK_PRIORITY, &createdTask,
                                PAINT_TASK_CORE) != pdPASS;
    taskHandle = fallbackMode ? NULL : createdTask;
  }
}

void PaintApp::stop() {
  if (fallbackMode) {
    fallbackMode = false;
    return;
  }
  if (taskHandle == NULL) {
    return;
  }

  Command command = {};
  command.type = COMMAND_STOP;
  enqueue(command);

  uint32_t startedAt = millis();
  while (taskHandle != NULL && millis() - startedAt < 1200) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void PaintApp::clear() {
  if (display == nullptr) {
    return;
  }

  Command command = {};
  command.type = COMMAND_CLEAR;

  if (fallbackMode || taskHandle == NULL) {
    display->lock();
    clearCanvas();
    display->flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
    display->unlock();
    return;
  }

  enqueue(command);
}

void PaintApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(6, 8);
  gfx.print("Paint");
  gfx.setCursor(124, 8);
  gfx.print("PWR clears");
  gfx.drawLine(0, CANVAS_TOP - 1, EPD_WIDTH - 1, CANVAS_TOP - 1, 1);
  drawCanvas(gfx);
}

void PaintApp::handleTouchEvent(const TouchEvent &event) {
  if (fallbackMode || taskHandle == NULL) {
    handleFallbackTouch(event);
    return;
  }

  Command command = {};
  command.type = COMMAND_TOUCH;
  command.touch = event;
  enqueue(command);
}

bool PaintApp::hasActiveSession() const { return hasInk; }

void PaintApp::taskEntry(void *context) {
  static_cast<PaintApp *>(context)->run();
}

void PaintApp::run() {
  Command command;
  while (xQueueReceive(queue, &command, portMAX_DELAY) == pdPASS) {
    if (command.type == COMMAND_STOP) {
      break;
    }

    bool changed = false;
    display->lock();
    if (command.type == COMMAND_CLEAR) {
      clearCanvas();
    } else {
      handleTouch(command.touch);
    }
    changed = true;

    while (xQueueReceive(queue, &command, 0) == pdPASS) {
      if (command.type == COMMAND_STOP) {
        display->unlock();
        taskHandle = NULL;
        vTaskDelete(NULL);
      }
      if (command.type == COMMAND_CLEAR) {
        clearCanvas();
      } else {
        handleTouch(command.touch);
      }
      changed = true;
    }

    if (changed) {
      display->flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
    display->unlock();
  }

  taskHandle = NULL;
  vTaskDelete(NULL);
}

void PaintApp::enqueue(const Command &command) {
  if (queue == NULL) {
    return;
  }

  if (xQueueSend(queue, &command, 0) == pdPASS) {
    return;
  }

  Command dropped;
  xQueueReceive(queue, &dropped, 0);
  xQueueSend(queue, &command, 0);
}

void PaintApp::handleTouch(const TouchEvent &event) {
  if (display == nullptr) {
    return;
  }

  if (event.type == TOUCH_EVENT_DOWN) {
    drawing = inCanvas(event.point);
    lastPoint = event.point;
    if (drawing) {
      drawCanvasPixel(event.point.x, event.point.y);
      display->drawPixel(event.point.x, event.point.y, 1);
      hasInk = true;
    }
    return;
  }

  if (event.type == TOUCH_EVENT_UP) {
    drawing = false;
    return;
  }

  if (!drawing || !inCanvas(event.point)) {
    return;
  }

  drawCanvasLine(lastPoint, event.point);
  display->drawLine(lastPoint.x, lastPoint.y, event.point.x, event.point.y, 1);
  lastPoint = event.point;
  hasInk = true;
}

void PaintApp::handleFallbackTouch(const TouchEvent &event) {
  if (display == nullptr) {
    return;
  }

  display->lock();
  handleTouch(event);
  if (event.type == TOUCH_EVENT_UP) {
    display->flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
  }
  display->unlock();
}

void PaintApp::drawCanvas(Adafruit_GFX &gfx) {
  for (int16_t y = CANVAS_TOP; y < EPD_HEIGHT; y++) {
    for (int16_t x = 0; x < EPD_WIDTH; x++) {
      int index = (y * EPD_WIDTH + x) / 8;
      uint8_t mask = 0x80 >> (x & 7);
      if ((canvas[index] & mask) == 0) {
        gfx.drawPixel(x, y, 1);
      }
    }
  }
}

void PaintApp::drawCanvasLine(TouchPoint from, TouchPoint to) {
  int16_t x0 = from.x;
  int16_t y0 = from.y;
  int16_t x1 = to.x;
  int16_t y1 = to.y;
  int16_t dx = abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;

  while (true) {
    drawCanvasPixel(x0, y0);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int16_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void PaintApp::drawCanvasPixel(int16_t x, int16_t y) {
  if (x < 0 || x >= EPD_WIDTH || y < CANVAS_TOP || y >= EPD_HEIGHT) {
    return;
  }

  int index = (y * EPD_WIDTH + x) / 8;
  uint8_t mask = 0x80 >> (x & 7);
  canvas[index] &= ~mask;
}

void PaintApp::clearCanvas() {
  if (display == nullptr) {
    return;
  }

  memset(canvas, 0xFF, sizeof(canvas));
  display->clear();
  draw(*display);
  hasInk = false;
}
