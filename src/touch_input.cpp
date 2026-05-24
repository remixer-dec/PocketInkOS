#include "touch_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global.h"
#include <driver/i2c_master.h>

#define FT6336_ADDR 0x38

static const uint32_t TOUCH_POLL_MS = 5;
static const uint32_t TOUCH_TASK_STACK_WORDS = 4096;
static const UBaseType_t TOUCH_TASK_PRIORITY = 1;
static const BaseType_t TOUCH_TASK_CORE = 0;

static i2c_master_bus_handle_t touchBus = NULL;
static i2c_master_dev_handle_t touchDev = NULL;
static bool touchReady = false;
static TaskHandle_t touchTaskHandle = NULL;
static volatile bool pendingTouch = false;
static volatile uint16_t pendingX = 0;
static volatile uint16_t pendingY = 0;
static bool fallbackWasDown = false;

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

static bool pollTouchPress(TouchPoint &point, bool &wasDown) {
  TouchSample sample = {};
  if (!readSample(sample) || sample.count == 0) {
    wasDown = false;
    return false;
  }
  if (wasDown) {
    return false;
  }
  wasDown = true;

  mapTouch(sample.rawX, sample.rawY, point);
  return true;
}

static void queueTouch(const TouchPoint &point) {
  if (pendingTouch) {
    return;
  }
  pendingX = point.x;
  pendingY = point.y;
  pendingTouch = true;
}

static void touchTask(void *) {
  bool taskWasDown = false;
  while (true) {
    if (touchReady) {
      TouchPoint point;
      if (pollTouchPress(point, taskWasDown)) {
        queueTouch(point);
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
    return touchReady && pollTouchPress(point, fallbackWasDown);
  }

  if (!pendingTouch) {
    return false;
  }

  point.x = pendingX;
  point.y = pendingY;
  pendingTouch = false;
  return true;
}
