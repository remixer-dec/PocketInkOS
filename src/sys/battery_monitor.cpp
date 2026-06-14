#include "sys/battery_monitor.h"
#include "sys/global.h"

namespace {

constexpr BatterySocPoint kDefaultSocCurve[] = {
    {BATTERY_EMPTY_VOLTAGE, 0}, {2.95f, 5},  {3.10f, 10},
    {3.20f, 15},                {3.30f, 25}, {3.45f, 40},
    {3.60f, 55},                {3.70f, 70}, {3.80f, 85},
    {3.90f, 96},                {BATTERY_FULL_VOLTAGE, 100},
};

BatteryMonitorConfig defaultConfig() {
  BatteryMonitorConfig config = {};
  config.adcPin = BAT_ADC_PIN;
  config.controlPin = BAT_CTRL_PIN;
  config.sampleCount = 8;
  config.adcMaxReading = 4095;
  config.adcReferenceVoltage = 3.3f;
  config.dividerScale = 2.0f;
  config.socCurve = kDefaultSocCurve;
  config.socCurveLength = sizeof(kDefaultSocCurve) / sizeof(kDefaultSocCurve[0]);
  return config;
}

float clampUnit(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

int clampPercentage(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}

int estimatePercentageFromCurve(float voltage, const BatteryMonitorConfig &config) {
  if (config.socCurve == nullptr || config.socCurveLength == 0) {
    return 0;
  }

  const BatterySocPoint *curve = config.socCurve;
  if (voltage <= curve[0].voltage) {
    return curve[0].percentage;
  }

  for (size_t i = 1; i < config.socCurveLength; ++i) {
    if (voltage > curve[i].voltage) {
      continue;
    }

    const BatterySocPoint &lower = curve[i - 1];
    const BatterySocPoint &upper = curve[i];
    const float span = upper.voltage - lower.voltage;
    if (span <= 0.0f) {
      return clampPercentage(upper.percentage);
    }

    const float ratio = clampUnit((voltage - lower.voltage) / span);
    const float interpolated =
        lower.percentage + ratio * (upper.percentage - lower.percentage);
    return clampPercentage(static_cast<int>(interpolated + 0.5f));
  }

  return curve[config.socCurveLength - 1].percentage;
}

} // namespace

BatteryMonitor batteryMonitor;

void BatteryMonitor::begin() {
  if (config_.sampleCount <= 0) {
    config_ = defaultConfig();
  }
  if (initialized_) {
    return;
  }
  pinMode(config_.controlPin, OUTPUT);
  // Board docs describe this line as enabling VBAT-related power. Keep it
  // asserted so battery-powered operation does not depend on a transient ADC
  // sampling window.
  digitalWrite(config_.controlPin, HIGH);
  initialized_ = true;
}

void BatteryMonitor::configure(const BatteryMonitorConfig &config) {
  config_ = config;
  initialized_ = false;
}

void BatteryMonitor::refresh() {
  begin();
  delay(2);

  uint32_t total = 0;
  for (int i = 0; i < config_.sampleCount; ++i) {
    total += static_cast<uint32_t>(analogRead(config_.adcPin));
    delay(1);
  }

  const float average = static_cast<float>(total) / config_.sampleCount;
  const float batteryVoltage =
      (average / config_.adcMaxReading) * config_.adcReferenceVoltage *
      config_.dividerScale;

  data_.valid = true;
  data_.voltage = batteryVoltage;
  data_.percentage = estimatePercentageFromCurve(batteryVoltage, config_);
}

const BatterySnapshot &BatteryMonitor::snapshot() const { return data_; }
