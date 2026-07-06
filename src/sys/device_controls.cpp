#include "sys/device_controls.h"
#include "sys/system_config.h"

#include <Arduino.h>
#include <stdio.h>

namespace {

static const uint16_t CPU_STEPS[] = {80, 160, 240};
static const uint8_t CPU_STEP_COUNT = sizeof(CPU_STEPS) / sizeof(CPU_STEPS[0]);
static const uint8_t VOLUME_STEP = 10;
static const uint8_t DEFAULT_VOLUME = 60;

bool bluetoothEnabled = false;
uint8_t cpuStep = 2;
volatile uint8_t volumeLevel = DEFAULT_VOLUME;
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
  volumeLevel = DEFAULT_VOLUME;
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
  volumeLevel = clampPercent(volume);
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
  if (muted || volumeLevel == 0 || sourceVolumePercent == 0) {
    return 0;
  }
  const uint8_t source = clampPercent(sourceVolumePercent);
  return static_cast<uint8_t>((static_cast<uint16_t>(source) * volumeLevel +
                               50U) /
                              100U);
}

void volumeDown() {
  volumeLevel = volumeLevel > VOLUME_STEP ? volumeLevel - VOLUME_STEP : 0;
  if (volumeLevel > 0) {
    muted = false;
  }
}

void volumeUp() {
  volumeLevel = volumeLevel + VOLUME_STEP < 100 ? volumeLevel + VOLUME_STEP : 100;
  if (volumeLevel > 0) {
    muted = false;
  }
}

void toggleMute() { muted = !muted; }
