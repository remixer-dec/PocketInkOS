#include "sys/services/environment_monitor.h"

#include "sys/drivers/environment_sensors.h"
#include "sys/system_config.h"

#include <Arduino.h>

namespace {

EnvironmentSensors sensors;

} // namespace

EnvironmentMonitor environmentMonitor;

void EnvironmentMonitor::begin() { sensors.begin(); }

void EnvironmentMonitor::refresh() {
  begin();
  EnvironmentSensorReadings readings;
  sensors.read(readings);
  if (!readings.ambientTemperatureValid ||
      !readings.ambientHumidityValid) {
    delay(5);
    sensors.read(readings);
  }

  data_.ambientTemperatureValid = readings.ambientTemperatureValid;
  data_.ambientHumidityValid = readings.ambientHumidityValid;
  data_.chipTemperatureValid = readings.chipTemperatureValid;
  data_.ambientTemperatureC =
      readings.ambientTemperatureC +
      currentSystemConfig().sensors.temperatureOffsetC;
  data_.ambientHumidityPct = readings.ambientHumidityPct;
  data_.chipTemperatureC = readings.chipTemperatureC;
}

const EnvironmentSnapshot &EnvironmentMonitor::snapshot() const {
  return data_;
}
