#include "sys/device_controls.h"

#include <Arduino.h>

namespace {

static const uint16_t CPU_STEPS[] = {80, 160, 240};
static const uint8_t CPU_STEP_COUNT = sizeof(CPU_STEPS) / sizeof(CPU_STEPS[0]);
static const uint8_t VOLUME_STEP = 10;

bool bluetoothEnabled = false;
uint8_t cpuStep = 2;
uint8_t volumeLevel = 60;
bool muted = false;

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

uint16_t currentCpuMhz() {
#if defined(ARDUINO_ARCH_ESP32)
  return static_cast<uint16_t>(getCpuFrequencyMhz());
#else
  return CPU_STEPS[cpuStep];
#endif
}

} // namespace

void deviceControlsBegin() {
  applyBluetoothState();
  applyCpuFrequency();
}

DeviceControlSnapshot deviceControlSnapshot() {
  DeviceControlSnapshot snapshot;
  snapshot.bluetoothOn = bluetoothEnabled;
  snapshot.cpuMhz = currentCpuMhz();
  snapshot.volume = volumeLevel;
  snapshot.muted = muted;
  return snapshot;
}

void toggleBluetooth() {
  bluetoothEnabled = !bluetoothEnabled;
  applyBluetoothState();
}

void cycleCpuFrequency() {
  cpuStep = (cpuStep + 1) % CPU_STEP_COUNT;
  applyCpuFrequency();
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
