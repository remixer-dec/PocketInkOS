#ifndef ENVIRONMENT_MONITOR_H
#define ENVIRONMENT_MONITOR_H

/// Cached temperature and humidity readings from onboard sensors.
struct EnvironmentSnapshot {
  /// True when ambientTemperatureC contains a valid sensor value.
  bool ambientTemperatureValid;
  /// True when ambientHumidityPct contains a valid sensor value.
  bool ambientHumidityValid;
  /// True when chipTemperatureC contains a valid sensor value.
  bool chipTemperatureValid;
  /// Ambient temperature in degrees Celsius.
  float ambientTemperatureC;
  /// Ambient relative humidity percentage.
  float ambientHumidityPct;
  /// ESP32 internal temperature in degrees Celsius.
  float chipTemperatureC;
};

/// Polls environmental sensors and caches values for UI display.
class EnvironmentMonitor {
public:
  /// Initializes sensor hardware and cached state.
  void begin();
  /// Refreshes cached sensor readings.
  void refresh();
  /// Returns the latest cached environmental readings.
  const EnvironmentSnapshot &snapshot() const;

private:
  EnvironmentSnapshot data_ = {false, false, false, 0.0f, 0.0f, 0.0f};
};

/// Process-wide environment monitor instance.
extern EnvironmentMonitor environmentMonitor;

#endif
