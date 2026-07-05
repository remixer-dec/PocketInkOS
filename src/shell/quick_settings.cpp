#include "shell/quick_settings.h"
#include "shell/connectivity.h"
#include "sys/device_controls.h"
#include "sys/power_control.h"
#include <Arduino.h>

void QuickSettingsController::open(PowerDialogPage nextPage) {
  page = nextPage;
  openFlag = true;
  openedAt = millis();
}

void QuickSettingsController::close() { openFlag = false; }

bool QuickSettingsController::isOpen() const { return openFlag; }

PowerDialogSnapshot QuickSettingsController::snapshot() const {
  DeviceControlSnapshot controls = deviceControlSnapshot();
  PowerDialogSnapshot result;
  result.page = page;
  result.wifiOn = wifiIsOn();
  result.wifiConnected = wifiIsConnected();
  result.bluetoothOn = controls.bluetoothOn;
  result.cpuMhz = controls.cpuMhz;
  result.volume = controls.volume;
  result.muted = controls.muted;
  return result;
}

bool QuickSettingsController::handleTouch(const TouchPoint &point,
                                          const QuickSettingsActions &actions) {
  if (!openFlag) {
    return false;
  }
  if (millis() - openedAt < 350) {
    return true;
  }

  const PowerDialogAction action = powerDialogHitAction(point, page);
  switch (action) {
  case PowerDialogAction::PreviousPage:
    page = previousPage();
    return true;
  case PowerDialogAction::NextPage:
    page = nextPage();
    return true;
  case PowerDialogAction::Reboot:
    showActionFeedback(actions, action);
    if (actions.prepareForPowerTransition != nullptr) {
      actions.prepareForPowerTransition(actions.context);
    }
    rebootDevice();
    return true;
  case PowerDialogAction::PowerOff:
    showActionFeedback(actions, action);
    if (actions.renderPowerOff != nullptr) {
      actions.renderPowerOff(actions.context);
    }
    if (actions.prepareForPowerTransition != nullptr) {
      actions.prepareForPowerTransition(actions.context);
    }
    powerOffDevice();
    return true;
  case PowerDialogAction::DeepSleep:
    if (actions.saveRetainedContext != nullptr) {
      actions.saveRetainedContext(actions.context);
    }
    showActionFeedback(actions, action);
    if (actions.enterSleepClock != nullptr) {
      actions.enterSleepClock(actions.context);
    }
    return true;
  case PowerDialogAction::WifiToggle:
    showActionFeedback(actions, action);
    wifiToggle();
    return true;
  case PowerDialogAction::BluetoothToggle:
    showActionFeedback(actions, action);
    toggleBluetooth();
    return true;
  case PowerDialogAction::CpuCycle:
    showActionFeedback(actions, action);
    cycleCpuFrequency();
    return true;
  case PowerDialogAction::VolumeDown:
    showActionFeedback(actions, action);
    volumeDown();
    return true;
  case PowerDialogAction::VolumeMute:
    showActionFeedback(actions, action);
    toggleMute();
    return true;
  case PowerDialogAction::VolumeUp:
    showActionFeedback(actions, action);
    volumeUp();
    return true;
  case PowerDialogAction::None:
    return true;
  }
  return true;
}

void QuickSettingsController::showActionFeedback(
    const QuickSettingsActions &actions, PowerDialogAction action) const {
  if (actions.showActionFeedback != nullptr) {
    actions.showActionFeedback(actions.context, action);
  }
}

PowerDialogPage QuickSettingsController::previousPage() const {
  switch (page) {
  case PowerDialogPage::Power:
    return PowerDialogPage::Volume;
  case PowerDialogPage::Device:
    return PowerDialogPage::Power;
  case PowerDialogPage::Volume:
    return PowerDialogPage::Device;
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
    return PowerDialogPage::Power;
  }
  return PowerDialogPage::Power;
}
