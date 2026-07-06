#ifndef DEVICE_CONTROLS_H
#define DEVICE_CONTROLS_H

#include <stdint.h>

/// Snapshot of user-facing hardware toggles shown by status and settings UI, used by quick settings and power dialog display.
struct DeviceControlSnapshot {
  /// Whether Bluetooth radio state is currently considered enabled.
  bool bluetoothOn = false;
  /// Current configured CPU clock in MHz.
  uint16_t cpuMhz = 240;
  /// User volume from 0 to 100.
  uint8_t volume = 60;
  /// True when audio output should be suppressed without changing volume.
  bool muted = false;
};

/// Initializes device-control state from persisted config and hardware defaults.
void deviceControlsBegin();
/// Returns the latest device-control state for display.
DeviceControlSnapshot deviceControlsSnapshot();
/// Restores runtime audio controls from retained RTC state.
void deviceControlsRestoreAudio(uint8_t volume, bool muteState);

/// Toggles the Bluetooth preference and hardware state when supported.
void toggleBluetooth();
/// Advances to the next supported CPU frequency profile.
void cycleCpuFrequency();
/// Returns source volume scaled by the current output volume and mute state.
uint8_t deviceEffectiveOutputVolumePercent(uint8_t sourceVolumePercent);
/// Decreases runtime output volume by one UI step.
void volumeDown();
/// Increases runtime output volume by one UI step.
void volumeUp();
/// Toggles mute while preserving the configured volume level.
void toggleMute();

#endif
