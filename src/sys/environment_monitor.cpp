#include "sys/environment_monitor.h"
#include "sys/global.h"
#include "sys/touch_input.h"

#include <math.h>

#include <driver/i2c_master.h>

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

static const uint8_t SHTC3_ADDR = 0x70;
static const uint16_t SHTC3_WAKEUP = 0x3517;
static const uint16_t SHTC3_SLEEP = 0xB098;
static const uint16_t SHTC3_MEASURE_T_FIRST = 0x7866;

static i2c_master_dev_handle_t shtc3Device = NULL;
static bool shtc3Ready = false;

#if HAS_NEW_TEMP_SENSOR_API
static temperature_sensor_handle_t chipTempHandle = NULL;
static bool chipTempReady = false;
#endif

EnvironmentMonitor environmentMonitor;

static bool shtc3SendCommand(uint16_t command) {
  if (!shtc3Ready || shtc3Device == NULL) {
    return false;
  }
  uint8_t data[2] = {static_cast<uint8_t>(command >> 8),
                     static_cast<uint8_t>(command & 0xFF)};
  return i2c_master_transmit(shtc3Device, data, sizeof(data), 100) == ESP_OK;
}

static uint8_t shtc3Crc(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xFF;
  uint8_t data[2] = {msb, lsb};
  for (uint8_t byte : data) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

static bool shtc3Read(EnvironmentSnapshot &data) {
  if (!shtc3SendCommand(SHTC3_WAKEUP)) {
    return false;
  }
  delay(1);

  if (!shtc3SendCommand(SHTC3_MEASURE_T_FIRST)) {
    shtc3SendCommand(SHTC3_SLEEP);
    return false;
  }
  delay(15);

  uint8_t raw[6] = {0};
  if (i2c_master_receive(shtc3Device, raw, sizeof(raw), 100) != ESP_OK) {
    shtc3SendCommand(SHTC3_SLEEP);
    return false;
  }
  shtc3SendCommand(SHTC3_SLEEP);

  if (shtc3Crc(raw[0], raw[1]) != raw[2] ||
      shtc3Crc(raw[3], raw[4]) != raw[5]) {
    return false;
  }

  uint16_t rawTemp = (static_cast<uint16_t>(raw[0]) << 8) | raw[1];
  uint16_t rawHumidity = (static_cast<uint16_t>(raw[3]) << 8) | raw[4];
  data.ambientTemperatureC = -45.0f + 175.0f * rawTemp / 65536.0f;
  data.ambientHumidityPct = 100.0f * rawHumidity / 65536.0f;
  data.ambientTemperatureValid = !isnan(data.ambientTemperatureC);
  data.ambientHumidityValid = !isnan(data.ambientHumidityPct);
  return data.ambientTemperatureValid && data.ambientHumidityValid;
}

void EnvironmentMonitor::begin() {
  if (!shtc3Ready) {
    i2c_master_bus_handle_t bus = touchI2cBusHandle();
    if (bus != NULL) {
      i2c_device_config_t devConfig = {};
      devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      devConfig.device_address = SHTC3_ADDR;
      devConfig.scl_speed_hz = 400000;

      shtc3Ready =
          i2c_master_bus_add_device(bus, &devConfig, &shtc3Device) == ESP_OK;
    }
  }

#if HAS_NEW_TEMP_SENSOR_API
  if (!chipTempReady) {
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10,
                                                                            80);
    if (temperature_sensor_install(&config, &chipTempHandle) == ESP_OK &&
        temperature_sensor_enable(chipTempHandle) == ESP_OK) {
      chipTempReady = true;
    }
  }
#elif HAS_LEGACY_TEMP_SENSOR_API
  temp_sensor_config_t config = TSENS_CONFIG_DEFAULT();
  temp_sensor_set_config(config);
  temp_sensor_start();
#endif
}

void EnvironmentMonitor::refresh() {
  begin();
  data_.ambientTemperatureValid = false;
  data_.ambientHumidityValid = false;
  data_.chipTemperatureValid = false;

  if (shtc3Ready) {
    if (!shtc3Read(data_)) {
      delay(5);
      shtc3Read(data_);
    }
  }

#if HAS_NEW_TEMP_SENSOR_API
  if (chipTempReady && chipTempHandle != NULL) {
    float celsius = 0.0f;
    if (temperature_sensor_get_celsius(chipTempHandle, &celsius) == ESP_OK &&
        !isnan(celsius)) {
      data_.chipTemperatureC = celsius;
      data_.chipTemperatureValid = true;
    }
  }
#elif HAS_LEGACY_TEMP_SENSOR_API
  float celsius = 0.0f;
  if (temp_sensor_read_celsius(&celsius) == ESP_OK && !isnan(celsius)) {
    data_.chipTemperatureC = celsius;
    data_.chipTemperatureValid = true;
  }
#endif
}

const EnvironmentSnapshot &EnvironmentMonitor::snapshot() const {
  return data_;
}
