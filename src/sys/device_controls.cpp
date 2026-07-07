#include "sys/device_controls.h"
#include "sys/system_config.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

static const uint16_t CPU_STEPS[] = {80, 160, 240};
static const uint8_t CPU_STEP_COUNT = sizeof(CPU_STEPS) / sizeof(CPU_STEPS[0]);
static const uint8_t VOLUME_STEP = 10;
static const uint8_t DEFAULT_VOLUME = 40;
static const uint8_t DEFAULT_OUTPUT_VOLUME = 10;
static const float VOLUME_CURVE_EXPONENT = 2.5129f;

bool bluetoothEnabled = false;
uint8_t cpuStep = 2;
volatile uint8_t volumeLevel = DEFAULT_VOLUME;
volatile uint8_t outputVolumeLevel = DEFAULT_OUTPUT_VOLUME;
volatile bool muted = false;

uint8_t cpuStepIndexForMhz(uint16_t mhz) {
  for (uint8_t i = 0; i < CPU_STEP_COUNT; i++) {
    if (CPU_STEPS[i] == mhz) {
      return i;
    }
  }
  return CPU_STEP_COUNT - 1;
}

void applyBluetoothState() {
#if defined(ARDUINO_ARCH_ESP32)
  if (bluetoothEnabled) {
    btStart();
  } else {
    btStop();
  }
#endif
}

void applyCpuFrequency() {
#if defined(ARDUINO_ARCH_ESP32)
  setCpuFrequencyMhz(CPU_STEPS[cpuStep]);
#endif
}

uint8_t clampPercent(uint8_t percent) {
  return percent > 100U ? 100U : percent;
}

// Preserves the pre-unified player calibration: UI 40 maps to ~10% PCM gain.
uint8_t calibratedOutputVolume(uint8_t uiVolume) {
  if (uiVolume == 0) {
    return 0;
  }
  if (uiVolume >= 100) {
    return 100;
  }

  const float normalized = static_cast<float>(uiVolume) / 100.0f;
  const int mapped =
      static_cast<int>(powf(normalized, VOLUME_CURVE_EXPONENT) * 100.0f +
                       0.5f);
  return static_cast<uint8_t>(mapped < 1 ? 1 : mapped);
}

void setVolumeLevel(uint8_t uiVolume) {
  volumeLevel = clampPercent(uiVolume);
  outputVolumeLevel = calibratedOutputVolume(volumeLevel);
}

uint16_t currentCpuMhz() {
#if defined(ARDUINO_ARCH_ESP32)
  return static_cast<uint16_t>(getCpuFrequencyMhz());
#else
  return CPU_STEPS[cpuStep];
#endif
}

} // namespace

void deviceControlsBegin() {
  cpuStep = cpuStepIndexForMhz(currentSystemConfig().cpu.mhz);
  setVolumeLevel(DEFAULT_VOLUME);
  muted = false;
  applyBluetoothState();
  applyCpuFrequency();
}

DeviceControlSnapshot deviceControlsSnapshot() {
  DeviceControlSnapshot snapshot;
  snapshot.bluetoothOn = bluetoothEnabled;
  snapshot.cpuMhz = currentCpuMhz();
  snapshot.volume = volumeLevel;
  snapshot.muted = muted;
  return snapshot;
}

void deviceControlsRestoreAudio(uint8_t volume, bool muteState) {
  setVolumeLevel(volume);
  muted = muteState;
}

void toggleBluetooth() {
  bluetoothEnabled = !bluetoothEnabled;
  applyBluetoothState();
}

void cycleCpuFrequency() {
  cpuStep = (cpuStep + 1) % CPU_STEP_COUNT;
  char mhzText[8];
  snprintf(mhzText, sizeof(mhzText), "%u",
           static_cast<unsigned>(CPU_STEPS[cpuStep]));
  configSet("cpu.mhz", mhzText);
  applyCpuFrequency();
}

uint8_t deviceEffectiveOutputVolumePercent(uint8_t sourceVolumePercent) {
  const uint8_t output = outputVolumeLevel;
  if (muted || output == 0 || sourceVolumePercent == 0) {
    return 0;
  }
  const uint8_t source = clampPercent(sourceVolumePercent);
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(source) * output + 50U) / 100U);
}

void volumeDown() {
  const uint8_t current = volumeLevel;
  setVolumeLevel(current > VOLUME_STEP ? current - VOLUME_STEP : 0);
  if (volumeLevel > 0) {
    muted = false;
  }
}

void volumeUp() {
  const uint8_t current = volumeLevel;
  setVolumeLevel(current + VOLUME_STEP < 100 ? current + VOLUME_STEP : 100);
  if (volumeLevel > 0) {
    muted = false;
  }
}

void toggleMute() { muted = !muted; }
