#ifndef ENVIRONMENT_MONITOR_H
#define ENVIRONMENT_MONITOR_H

struct EnvironmentSnapshot {
  bool ambientTemperatureValid;
  bool ambientHumidityValid;
  bool chipTemperatureValid;
  float ambientTemperatureC;
  float ambientHumidityPct;
  float chipTemperatureC;
};

class EnvironmentMonitor {
public:
  void begin();
  void refresh();
  const EnvironmentSnapshot &snapshot() const;

private:
  EnvironmentSnapshot data_ = {false, false, false, 0.0f, 0.0f, 0.0f};
};

extern EnvironmentMonitor environmentMonitor;

#endif
