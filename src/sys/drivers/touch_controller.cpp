#include "sys/drivers/touch_controller.h"

#include "sys/drivers/shared_i2c_bus.h"
#include "sys/global.h"

#include <Arduino.h>
#include <driver/i2c_master.h>

namespace {

constexpr uint8_t kAddress = 0x38;

} // namespace

bool TouchController::begin() {
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

  if (!sharedI2cBegin()) {
    return false;
  }
  if (device != nullptr) {
    return true;
  }

  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = kAddress;
  config.scl_speed_hz = 400000;
  i2c_master_dev_handle_t handle = nullptr;
  if (i2c_master_bus_add_device(sharedI2cBusHandle(), &config, &handle) !=
      ESP_OK) {
    return false;
  }
  device = handle;
  return true;
}

void TouchController::end() {
  pinMode(EPD_TP_RST_PIN, OUTPUT);
  digitalWrite(EPD_TP_RST_PIN, LOW);
}

bool TouchController::read(TouchControllerSample &sample) {
  if (device == nullptr) {
    return false;
  }

  uint8_t countRegister = 0x02;
  uint8_t count = 0;
  if (i2c_master_transmit_receive(device, &countRegister, 1, &count, 1, 100) !=
      ESP_OK) {
    return false;
  }
  sample.count = count & 0x0f;
  if (sample.count == 0) {
    sample.x = 0;
    sample.y = 0;
    return true;
  }

  uint8_t pointRegister = 0x03;
  uint8_t data[4] = {};
  if (i2c_master_transmit_receive(device, &pointRegister, 1, data,
                                  sizeof(data), 100) != ESP_OK) {
    return false;
  }
  sample.x = ((static_cast<uint16_t>(data[0]) & 0x0f) << 8) | data[1];
  sample.y = ((static_cast<uint16_t>(data[2]) & 0x0f) << 8) | data[3];
  return sample.count <= 2 && sample.x < 4096 && sample.y < 4096;
}
