#include "sys/drivers/shared_i2c_bus.h"

#include "sys/global.h"

#include <Arduino.h>

namespace {

i2c_master_bus_handle_t sharedBus = nullptr;

void releaseLine(int pin) {
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(5);
}

void pullLineLow(int pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
  delayMicroseconds(5);
}

bool recoverBusLines() {
  releaseLine(ESP32_I2C_SDA);
  releaseLine(ESP32_I2C_SCL);

  // A reset can leave a slave mid-byte even when both lines happen to read
  // high. Finish a held byte when needed, then always generate a STOP.
  for (uint8_t pulse = 0;
       pulse < 9U && digitalRead(ESP32_I2C_SDA) == LOW; pulse++) {
    pullLineLow(ESP32_I2C_SCL);
    releaseLine(ESP32_I2C_SCL);
    if (digitalRead(ESP32_I2C_SCL) == LOW) {
      break;
    }
  }
  pullLineLow(ESP32_I2C_SDA);
  releaseLine(ESP32_I2C_SCL);
  releaseLine(ESP32_I2C_SDA);
  return digitalRead(ESP32_I2C_SDA) == HIGH &&
         digitalRead(ESP32_I2C_SCL) == HIGH;
}

} // namespace

bool sharedI2cBegin() {
  if (sharedBus != nullptr) {
    return true;
  }

  if (!recoverBusLines()) {
    return false;
  }
  i2c_master_bus_config_t config = {};
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.i2c_port = I2C_NUM_0;
  config.scl_io_num = static_cast<gpio_num_t>(ESP32_I2C_SCL);
  config.sda_io_num = static_cast<gpio_num_t>(ESP32_I2C_SDA);
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&config, &bus) != ESP_OK || bus == nullptr) {
    return false;
  }
  sharedBus = bus;
  return true;
}

i2c_master_bus_handle_t sharedI2cBusHandle() { return sharedBus; }
