#ifndef ENVIRONMENT_SENSORS_H
#define ENVIRONMENT_SENSORS_H

/// Raw readings produced by the board's ambient and chip sensors.
struct EnvironmentSensorReadings {
  bool ambientTemperatureValid = false;
  bool ambientHumidityValid = false;
  bool chipTemperatureValid = false;
  float ambientTemperatureC = 0.0f;
  float ambientHumidityPct = 0.0f;
  float chipTemperatureC = 0.0f;
};

/// Low-level access to the SHTC3 and ESP32 internal temperature sensor.
class EnvironmentSensors {
public:
  /// Attaches available sensor hardware.
  void begin();
  /// Samples all available sensors into readings.
  void read(EnvironmentSensorReadings &readings);
};

#endif
