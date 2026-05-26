#include "touch_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "global.h"
#include <driver/i2c_master.h>

#define FT6336_ADDR 0x38

static const uint32_t TOUCH_POLL_MS = 5;
static const uint32_t TOUCH_TASK_STACK_WORDS = 4096;
static const UBaseType_t TOUCH_TASK_PRIORITY = 1;
static const BaseType_t TOUCH_TASK_CORE = 0;
static const UBaseType_t TOUCH_QUEUE_DEPTH = 96;

static i2c_master_bus_handle_t touchBus = NULL;
static i2c_master_dev_handle_t touchDev = NULL;
static bool touchReady = false;
static TaskHandle_t touchTaskHandle = NULL;
static QueueHandle_t touchQueue = NULL;
static bool fallbackWasDown = false;
static TouchPoint fallbackLastPoint = {};

struct TouchSample {
  uint8_t count;
  uint16_t rawX;
  uint16_t rawY;
};

static bool readRegister(uint8_t reg, uint8_t *buf, size_t len) {
  if (!touchReady || touchDev == NULL) {
    return false;
  }
  return i2c_master_transmit_receive(touchDev, &reg, 1, buf, len, 100) ==
         ESP_OK;
}

static bool readSample(TouchSample &sample) {
  uint8_t count = 0;
  if (!readRegister(0x02, &count, 1)) {
    return false;
  }

  sample.count = count & 0x0F;
  if (sample.count == 0) {
    sample.rawX = 0;
    sample.rawY = 0;
    return true;
  }

  uint8_t data[4] = {0};
  if (!readRegister(0x03, data, sizeof(data))) {
    return false;
  }

  sample.rawX = (((uint16_t)data[0] & 0x0F) << 8) | data[1];
  sample.rawY = (((uint16_t)data[2] & 0x0F) << 8) | data[3];
  return sample.count <= 2 && sample.rawX < 4096 && sample.rawY < 4096;
}

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
  TouchSample sample = {};
  if (!readSample(sample)) {
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
  mapTouch(sample.rawX, sample.rawY, point);

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

static bool beginTouchBus() {
  if (touchBus == NULL) {
    i2c_master_bus_config_t busConfig = {};
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.scl_io_num = (gpio_num_t)ESP32_I2C_SCL;
    busConfig.sda_io_num = (gpio_num_t)ESP32_I2C_SDA;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;

    if (i2c_new_master_bus(&busConfig, &touchBus) != ESP_OK) {
      return false;
    }
  }

  if (touchDev == NULL) {
    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = FT6336_ADDR;
    devConfig.scl_speed_hz = 400000;

    if (i2c_master_bus_add_device(touchBus, &devConfig, &touchDev) != ESP_OK) {
      return false;
    }
  }

  return true;
}

void TouchInput::begin() {
  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW);
  delay(100);

  pinMode(EPD_TP_RST_PIN, OUTPUT);
  pinMode(EPD_TP_INT_PIN, INPUT_PULLUP);

  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(100);
  digitalWrite(EPD_TP_RST_PIN, LOW);
  delay(100);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(300);

  touchReady = beginTouchBus();
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

i2c_master_bus_handle_t touchI2cBusHandle() { return touchBus; }
