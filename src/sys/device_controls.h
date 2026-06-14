#ifndef DEVICE_CONTROLS_H
#define DEVICE_CONTROLS_H

#include <stdint.h>

struct DeviceControlSnapshot {
  bool bluetoothOn = false;
  uint16_t cpuMhz = 240;
  uint8_t volume = 60;
  bool muted = false;
};

void deviceControlsBegin();
DeviceControlSnapshot deviceControlSnapshot();

void toggleBluetooth();
void cycleCpuFrequency();
void volumeDown();
void volumeUp();
void toggleMute();

#endif
