#include "sys/drivers/environment_sensors.h"

#include "sys/drivers/shared_i2c_bus.h"

#include <Arduino.h>
#include <driver/i2c_master.h>
#include <math.h>

#if __has_include(<driver/temperature_sensor.h>)
#include <driver/temperature_sensor.h>
#define HAS_NEW_TEMP_SENSOR_API 1
#elif __has_include(<driver/temp_sensor.h>)
#include <driver/temp_sensor.h>
#define HAS_LEGACY_TEMP_SENSOR_API 1
#else
#define HAS_NEW_TEMP_SENSOR_API 0
#define HAS_LEGACY_TEMP_SENSOR_API 0
#endif

namespace {

constexpr uint8_t kShtc3Address = 0x70;
constexpr uint16_t kShtc3Wakeup = 0x3517;
constexpr uint16_t kShtc3Sleep = 0xb098;
constexpr uint16_t kShtc3MeasureTemperatureFirst = 0x7866;

i2c_master_dev_handle_t shtc3Device = nullptr;

#if HAS_NEW_TEMP_SENSOR_API
temperature_sensor_handle_t chipTemperatureHandle = nullptr;
#endif

bool sendShtc3Command(uint16_t command) {
  if (shtc3Device == nullptr) {
    return false;
  }
  uint8_t data[2] = {static_cast<uint8_t>(command >> 8),
                     static_cast<uint8_t>(command & 0xff)};
  return sharedI2cTransmit(shtc3Device, data, sizeof(data), 100) == ESP_OK;
}

uint8_t shtc3Crc(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xff;
  const uint8_t data[2] = {msb, lsb};
  for (uint8_t byte : data) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

bool readShtc3(EnvironmentSensorReadings &readings) {
  if (!sendShtc3Command(kShtc3Wakeup)) {
    return false;
  }
  delay(1);
  if (!sendShtc3Command(kShtc3MeasureTemperatureFirst)) {
    sendShtc3Command(kShtc3Sleep);
    return false;
  }
  delay(15);

  uint8_t raw[6] = {};
  if (sharedI2cReceive(shtc3Device, raw, sizeof(raw), 100) != ESP_OK) {
    sendShtc3Command(kShtc3Sleep);
    return false;
  }
  sendShtc3Command(kShtc3Sleep);
  if (shtc3Crc(raw[0], raw[1]) != raw[2] ||
      shtc3Crc(raw[3], raw[4]) != raw[5]) {
    return false;
  }

  const uint16_t rawTemperature =
      (static_cast<uint16_t>(raw[0]) << 8) | raw[1];
  const uint16_t rawHumidity =
      (static_cast<uint16_t>(raw[3]) << 8) | raw[4];
  readings.ambientTemperatureC =
      -45.0f + 175.0f * rawTemperature / 65536.0f;
  readings.ambientHumidityPct = 100.0f * rawHumidity / 65536.0f;
  readings.ambientTemperatureValid = !isnan(readings.ambientTemperatureC);
  readings.ambientHumidityValid = !isnan(readings.ambientHumidityPct);
  return readings.ambientTemperatureValid && readings.ambientHumidityValid;
}

} // namespace

void EnvironmentSensors::begin() {
  if (shtc3Device == nullptr && sharedI2cBegin()) {
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = kShtc3Address;
    config.scl_speed_hz = 400000;
    sharedI2cAddDevice(&config, &shtc3Device);
  }

#if HAS_NEW_TEMP_SENSOR_API
  if (chipTemperatureHandle == nullptr) {
    temperature_sensor_config_t config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&config, &chipTemperatureHandle) == ESP_OK) {
      if (temperature_sensor_enable(chipTemperatureHandle) != ESP_OK) {
        chipTemperatureHandle = nullptr;
      }
    }
  }
#elif HAS_LEGACY_TEMP_SENSOR_API
  temp_sensor_config_t config = TSENS_CONFIG_DEFAULT();
  temp_sensor_set_config(config);
  temp_sensor_start();
#endif
}

void EnvironmentSensors::read(EnvironmentSensorReadings &readings) {
  readings = {};
  readShtc3(readings);

#if HAS_NEW_TEMP_SENSOR_API
  float celsius = 0.0f;
  if (chipTemperatureHandle != nullptr &&
      temperature_sensor_get_celsius(chipTemperatureHandle, &celsius) ==
          ESP_OK &&
      !isnan(celsius)) {
    readings.chipTemperatureC = celsius;
    readings.chipTemperatureValid = true;
  }
#elif HAS_LEGACY_TEMP_SENSOR_API
  float celsius = 0.0f;
  if (temp_sensor_read_celsius(&celsius) == ESP_OK && !isnan(celsius)) {
    readings.chipTemperatureC = celsius;
    readings.chipTemperatureValid = true;
  }
#endif
}
