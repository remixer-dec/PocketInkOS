#include "sys/drivers/shared_i2c_bus.h"

#include "sys/global.h"

#include <Arduino.h>
#include <atomic>

namespace {

i2c_master_bus_handle_t sharedBus = nullptr;
std::atomic_flag busLocked = ATOMIC_FLAG_INIT;
unsigned long lastRecoveryAt = 0;
bool recoveryAttempted = false;

constexpr unsigned long kRecoveryCooldownMs = 1000;
constexpr uint8_t kStartupRecoveryAttempts = 3;

class BusLock {
public:
  BusLock() {
    while (busLocked.test_and_set(std::memory_order_acquire)) {
      delay(1);
    }
  }

  ~BusLock() { busLocked.clear(std::memory_order_release); }
};

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

bool beginLocked() {
  if (sharedBus != nullptr) {
    return true;
  }

  for (uint8_t attempt = 0; attempt < kStartupRecoveryAttempts; attempt++) {
    if (recoverBusLines()) {
      break;
    }
    if (attempt + 1 == kStartupRecoveryAttempts) {
      return false;
    }
    delay(10);
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
  // Use the controller's hardware bus-clear sequence as a second recovery
  // layer after a reset interrupted a slave transaction.
  i2c_master_bus_reset(sharedBus);
  return true;
}

template <typename Operation> esp_err_t runTransaction(Operation operation) {
  BusLock lock;
  esp_err_t result = operation();
  if (result == ESP_OK || sharedBus == nullptr) {
    return result;
  }

  const unsigned long now = millis();
  if (recoveryAttempted && now - lastRecoveryAt < kRecoveryCooldownMs) {
    return result;
  }

  recoveryAttempted = true;
  lastRecoveryAt = now;
  if (i2c_master_bus_reset(sharedBus) == ESP_OK) {
    result = operation();
  }
  return result;
}

} // namespace

bool sharedI2cBegin() {
  BusLock lock;
  return beginLocked();
}

esp_err_t sharedI2cAddDevice(const i2c_device_config_t *config,
                             i2c_master_dev_handle_t *device) {
  BusLock lock;
  if (!beginLocked()) {
    return ESP_ERR_INVALID_STATE;
  }
  return i2c_master_bus_add_device(sharedBus, config, device);
}

esp_err_t sharedI2cTransmit(i2c_master_dev_handle_t device,
                            const uint8_t *data, size_t size, int timeoutMs) {
  return runTransaction(
      [=]() { return i2c_master_transmit(device, data, size, timeoutMs); });
}

esp_err_t sharedI2cReceive(i2c_master_dev_handle_t device, uint8_t *data,
                           size_t size, int timeoutMs) {
  return runTransaction(
      [=]() { return i2c_master_receive(device, data, size, timeoutMs); });
}

esp_err_t sharedI2cTransmitReceive(i2c_master_dev_handle_t device,
                                   const uint8_t *writeData, size_t writeSize,
                                   uint8_t *readData, size_t readSize,
                                   int timeoutMs) {
  return runTransaction([=]() {
    return i2c_master_transmit_receive(device, writeData, writeSize, readData,
                                       readSize, timeoutMs);
  });
}

i2c_master_bus_handle_t sharedI2cBusHandle() { return sharedBus; }
