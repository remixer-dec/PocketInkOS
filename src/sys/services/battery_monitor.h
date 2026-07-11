#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stddef.h>

/// Voltage treated as 0 percent when estimating battery state of charge.
constexpr float BATTERY_EMPTY_VOLTAGE = 2.80f;
/// Voltage treated as 100 percent when estimating battery state of charge.
constexpr float BATTERY_FULL_VOLTAGE = 3.95f;

/// One voltage-to-percentage point in the battery state-of-charge curve.
struct BatterySocPoint {
  /// Measured battery voltage.
  float voltage;
  /// Estimated charge percentage for voltage.
  int percentage;
};

/// Hardware and calibration settings used by BatteryMonitor.
struct BatteryMonitorConfig {
  /// ADC input pin connected to the battery divider.
  int adcPin;
  /// GPIO that enables the battery divider or measurement circuit.
  int controlPin;
  /// Number of ADC samples averaged per refresh.
  int sampleCount;
  /// Maximum raw ADC reading.
  int adcMaxReading;
  /// ADC reference voltage used to convert readings to volts.
  float adcReferenceVoltage;
  /// Divider multiplier from ADC voltage to battery voltage.
  float dividerScale;
  /// Optional state-of-charge interpolation curve.
  const BatterySocPoint *socCurve;
  /// Number of entries in socCurve.
  size_t socCurveLength;
};

/// Cached battery reading exposed to UI and power policy.
struct BatterySnapshot {
  /// True after at least one successful reading.
  bool valid;
  /// Latest measured battery voltage.
  float voltage;
  /// Estimated charge percentage from 0 to 100.
  int percentage;
};

/// Samples battery voltage and exposes a cached percentage estimate.
class BatteryMonitor {
public:
  /// Initializes the monitor with default board pins.
  void begin();
  /// Takes a new ADC reading and updates the cached snapshot.
  void refresh();
  /// Replaces monitor hardware/calibration settings.
  void configure(const BatteryMonitorConfig &config);
  /// Returns the latest cached battery reading.
  const BatterySnapshot &snapshot() const;

private:
  BatteryMonitorConfig config_ = {};
  BatterySnapshot data_ = {false, 0.0f, 0};
  bool initialized_ = false;
};

/// Process-wide battery monitor instance used by firmware UI.
extern BatteryMonitor batteryMonitor;

#endif
