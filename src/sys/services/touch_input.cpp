#include "sys/services/touch_input.h"
#include "sys/drivers/touch_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys/global.h"

static const uint32_t TOUCH_POLL_MS = 5;
static const uint32_t TOUCH_TASK_STACK_WORDS = 4096;
static const UBaseType_t TOUCH_TASK_PRIORITY = 1;
static const BaseType_t TOUCH_TASK_CORE = 0;
static const UBaseType_t TOUCH_QUEUE_DEPTH = 96;

static bool touchReady = false;
static TaskHandle_t touchTaskHandle = NULL;
static QueueHandle_t touchQueue = NULL;
static bool fallbackWasDown = false;
static TouchPoint fallbackLastPoint = {};

static TouchController controller;

static uint16_t clampAxis(uint16_t value, uint16_t maxValue) {
  return value >= maxValue ? maxValue - 1 : value;
}

static void mapTouch(uint16_t rawX, uint16_t rawY, TouchPoint &point) {
#if TOUCH_SWAP_XY
  uint16_t mappedX = rawY;
  uint16_t mappedY = rawX;
#else
  uint16_t mappedX = rawX;
  uint16_t mappedY = rawY;
#endif

  mappedX = clampAxis(mappedX, EPD_WIDTH);
  mappedY = clampAxis(mappedY, EPD_HEIGHT);

#if TOUCH_INVERT_X
  mappedX = EPD_WIDTH - 1 - mappedX;
#endif
#if TOUCH_INVERT_Y
  mappedY = EPD_HEIGHT - 1 - mappedY;
#endif

  point.x = mappedX;
  point.y = mappedY;
}

static bool pointsEqual(const TouchPoint &a, const TouchPoint &b) {
  return a.x == b.x && a.y == b.y;
}

static bool pollTouchEvent(TouchEvent &event, bool &wasDown,
                           TouchPoint &lastPoint) {
  TouchControllerSample sample;
  if (!controller.read(sample)) {
    return false;
  }

  if (sample.count == 0) {
    if (!wasDown) {
      return false;
    }
    wasDown = false;
    event.type = TOUCH_EVENT_UP;
    event.point = lastPoint;
    return true;
  }

  TouchPoint point;
  mapTouch(sample.x, sample.y, point);

  if (!wasDown) {
    wasDown = true;
    lastPoint = point;
    event.type = TOUCH_EVENT_DOWN;
    event.point = point;
    return true;
  }

  if (pointsEqual(point, lastPoint)) {
    return false;
  }

  lastPoint = point;
  event.type = TOUCH_EVENT_MOVE;
  event.point = point;
  return true;
}

static bool pollTouchPress(TouchPoint &point, bool &wasDown,
                           TouchPoint &lastPoint) {
  TouchEvent event;
  while (pollTouchEvent(event, wasDown, lastPoint)) {
    if (event.type == TOUCH_EVENT_DOWN) {
      point = event.point;
      return true;
    }
  }
  return false;
}

static void queueTouch(const TouchEvent &event) {
  if (touchQueue == NULL) {
    return;
  }

  if (xQueueSend(touchQueue, &event, 0) == pdPASS) {
    return;
  }

  TouchEvent dropped;
  xQueueReceive(touchQueue, &dropped, 0);
  xQueueSend(touchQueue, &event, 0);
}

static void touchTask(void *) {
  bool taskWasDown = false;
  TouchPoint taskLastPoint = {};
  while (true) {
    if (touchReady) {
      TouchEvent event;
      if (pollTouchEvent(event, taskWasDown, taskLastPoint)) {
        queueTouch(event);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
  }
}

void TouchInput::begin() {
  touchReady = controller.begin();
  if (touchReady && touchQueue == NULL) {
    touchQueue = xQueueCreate(TOUCH_QUEUE_DEPTH, sizeof(TouchEvent));
  }
  if (touchReady && touchTaskHandle == NULL) {
    if (xTaskCreatePinnedToCore(touchTask, "touch", TOUCH_TASK_STACK_WORDS,
                                NULL, TOUCH_TASK_PRIORITY, &touchTaskHandle,
                                TOUCH_TASK_CORE) != pdPASS) {
      touchTaskHandle = NULL;
    }
  }
}

bool TouchInput::read(TouchPoint &point) {
  if (touchTaskHandle == NULL) {
    return touchReady &&
           pollTouchPress(point, fallbackWasDown, fallbackLastPoint);
  }

  TouchEvent event;
  while (readEvent(event)) {
    if (event.type == TOUCH_EVENT_DOWN) {
      point = event.point;
      return true;
    }
  }
  return false;
}

bool TouchInput::readEvent(TouchEvent &event) {
  if (touchTaskHandle == NULL) {
    if (!touchReady) {
      return false;
    }
    return pollTouchEvent(event, fallbackWasDown, fallbackLastPoint);
  }

  if (touchQueue == NULL) {
    return false;
  }

  return xQueueReceive(touchQueue, &event, 0) == pdPASS;
}
