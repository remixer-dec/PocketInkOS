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

void recoverBusLines() {
  releaseLine(ESP32_I2C_SDA);
  releaseLine(ESP32_I2C_SCL);
  if (digitalRead(ESP32_I2C_SDA) == HIGH &&
      digitalRead(ESP32_I2C_SCL) == HIGH) {
    return;
  }

  // A software reset can interrupt a slave mid-byte. Clock it to the end of
  // that byte, then issue a STOP before installing the ESP-IDF bus driver.
  for (uint8_t pulse = 0; pulse < 9U; pulse++) {
    pullLineLow(ESP32_I2C_SCL);
    releaseLine(ESP32_I2C_SCL);
  }
  pullLineLow(ESP32_I2C_SDA);
  releaseLine(ESP32_I2C_SCL);
  releaseLine(ESP32_I2C_SDA);
}

} // namespace

bool sharedI2cBegin() {
  if (sharedBus != nullptr) {
    return true;
  }

  recoverBusLines();
  i2c_master_bus_config_t config = {};
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.i2c_port = I2C_NUM_0;
  config.scl_io_num = static_cast<gpio_num_t>(ESP32_I2C_SCL);
  config.sda_io_num = static_cast<gpio_num_t>(ESP32_I2C_SDA);
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&config, &sharedBus) == ESP_OK;
}

i2c_master_bus_handle_t sharedI2cBusHandle() { return sharedBus; }
