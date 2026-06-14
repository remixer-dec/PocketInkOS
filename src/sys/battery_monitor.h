#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stddef.h>

constexpr float BATTERY_EMPTY_VOLTAGE = 2.80f;
constexpr float BATTERY_FULL_VOLTAGE = 3.95f;

struct BatterySocPoint {
  float voltage;
  int percentage;
};

struct BatteryMonitorConfig {
  int adcPin;
  int controlPin;
  int sampleCount;
  int adcMaxReading;
  float adcReferenceVoltage;
  float dividerScale;
  const BatterySocPoint *socCurve;
  size_t socCurveLength;
};

struct BatterySnapshot {
  bool valid;
  float voltage;
  int percentage;
};

class BatteryMonitor {
public:
  void begin();
  void refresh();
  void configure(const BatteryMonitorConfig &config);
  const BatterySnapshot &snapshot() const;

private:
  BatteryMonitorConfig config_ = {};
  BatterySnapshot data_ = {false, 0.0f, 0};
  bool initialized_ = false;
};

extern BatteryMonitor batteryMonitor;

#endif
