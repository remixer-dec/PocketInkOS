#include "shell/quick_settings.h"
#include "sys/services/connectivity_service.h"
#include "sys/services/device_controls.h"
#include "sys/services/power_control.h"
#include "sys/video/record.h"
#include <Arduino.h>

void QuickSettingsController::open(PowerDialogPage nextPage) {
  page = nextPage;
  openFlag = true;
  openedAt = millis();
}

void QuickSettingsController::close() { openFlag = false; }

bool QuickSettingsController::isOpen() const { return openFlag; }

PowerDialogSnapshot QuickSettingsController::snapshot() const {
  DeviceControlSnapshot controls = deviceControlsSnapshot();
  PowerDialogSnapshot result;
  result.page = page;
  result.wifiOn = wifiIsOn();
  result.wifiConnected = wifiIsConnected();
  result.bluetoothOn = controls.bluetoothOn;
  result.cpuMhz = controls.cpuMhz;
  result.volume = controls.volume;
  result.muted = controls.muted;
  result.recording = videoRecording();
  return result;
}

PowerDialogAction QuickSettingsController::handleTouch(const TouchPoint &point) {
  if (!openFlag) {
    return PowerDialogAction::None;
  }
  if (millis() - openedAt < 350) {
    return PowerDialogAction::None;
  }

  const PowerDialogAction action = powerDialogHitAction(point, page);
  switch (action) {
  case PowerDialogAction::PreviousPage:
    page = previousPage();
    return PowerDialogAction::None;
  case PowerDialogAction::NextPage:
    page = nextPage();
    return PowerDialogAction::None;
  case PowerDialogAction::None:
    return PowerDialogAction::None;
  case PowerDialogAction::Reboot:
  case PowerDialogAction::PowerOff:
  case PowerDialogAction::DeepSleep:
  case PowerDialogAction::WifiToggle:
  case PowerDialogAction::BluetoothToggle:
  case PowerDialogAction::CpuCycle:
  case PowerDialogAction::VolumeDown:
  case PowerDialogAction::VolumeMute:
  case PowerDialogAction::VolumeUp:
  case PowerDialogAction::Screenshot:
  case PowerDialogAction::VideoToggle:
    return action;
  }
  return PowerDialogAction::None;
}

void QuickSettingsController::runAction(
    PowerDialogAction action, const QuickSettingsActions &actions) const {
  switch (action) {
  case PowerDialogAction::Reboot:
    if (actions.prepareForPowerTransition != nullptr) {
      actions.prepareForPowerTransition(actions.context);
    }
    rebootDevice();
    return;
  case PowerDialogAction::PowerOff:
    if (actions.renderPowerOff != nullptr) {
      actions.renderPowerOff(actions.context);
    }
    if (actions.prepareForPowerTransition != nullptr) {
      actions.prepareForPowerTransition(actions.context);
    }
    powerOffDevice();
    return;
  case PowerDialogAction::DeepSleep:
    if (actions.saveRetainedContext != nullptr) {
      actions.saveRetainedContext(actions.context);
    }
    if (actions.enterSleepClock != nullptr) {
      actions.enterSleepClock(actions.context);
    }
    return;
  case PowerDialogAction::WifiToggle:
    wifiToggle();
    return;
  case PowerDialogAction::BluetoothToggle:
    toggleBluetooth();
    return;
  case PowerDialogAction::CpuCycle:
    cycleCpuFrequency();
    return;
  case PowerDialogAction::VolumeDown:
    volumeDown();
    return;
  case PowerDialogAction::VolumeMute:
    toggleMute();
    return;
  case PowerDialogAction::VolumeUp:
    volumeUp();
    return;
  case PowerDialogAction::Screenshot:
    if (actions.captureScreenshot != nullptr) {
      actions.captureScreenshot(actions.context);
    }
    return;
  case PowerDialogAction::VideoToggle:
    if (actions.toggleVideoRecording != nullptr) {
      actions.toggleVideoRecording(actions.context);
    }
    return;
  case PowerDialogAction::PreviousPage:
  case PowerDialogAction::NextPage:
  case PowerDialogAction::None:
    return;
  }
}

PowerDialogPage QuickSettingsController::previousPage() const {
  switch (page) {
  case PowerDialogPage::Power:
    return PowerDialogPage::Screen;
  case PowerDialogPage::Device:
    return PowerDialogPage::Power;
  case PowerDialogPage::Volume:
    return PowerDialogPage::Device;
  case PowerDialogPage::Screen:
    return PowerDialogPage::Volume;
  }
  return PowerDialogPage::Power;
}

PowerDialogPage QuickSettingsController::nextPage() const {
  switch (page) {
  case PowerDialogPage::Power:
    return PowerDialogPage::Device;
  case PowerDialogPage::Device:
    return PowerDialogPage::Volume;
  case PowerDialogPage::Volume:
    return PowerDialogPage::Screen;
  case PowerDialogPage::Screen:
    return PowerDialogPage::Power;
  }
  return PowerDialogPage::Power;
}
