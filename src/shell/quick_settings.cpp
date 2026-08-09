#include "shell/quick_settings.h"
#include "net/network_interface.h"
#include "net/network_services.h"
#include "sys/services/connectivity_service.h"
#include "sys/services/device_controls.h"
#include "sys/services/power_control.h"
#include "sys/video/record.h"
#include <Arduino.h>

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

void QuickSettingsController::open(PowerDialogPage nextPage) {
  page = nextPage;
  openFlag = true;
  networkUnavailableNotice = false;
  openedAt = millis();
}

void QuickSettingsController::close() {
  openFlag = false;
  networkUnavailableNotice = false;
}

bool QuickSettingsController::isOpen() const { return openFlag; }

PowerDialogSnapshot QuickSettingsController::snapshot() const {
  DeviceControlSnapshot controls = deviceControlsSnapshot();
  PowerDialogSnapshot result;
  result.page = page;
  result.wifiOn = wifiIsOn();
  result.wifiConnected = wifiIsConnected();
  result.bluetoothOn = controls.bluetoothOn;
#if ENABLE_NETWORK_APPS
  const net::NetworkServicesSnapshot services = net::networkServicesSnapshot();
  result.hotspotOn = net::tetheringActive();
  result.httpServerOn = services.http;
  result.ftpServerOn = services.ftp;
  const bool networkAvailable = result.wifiOn || result.hotspotOn;
  result.httpServerAvailable = networkAvailable;
  result.ftpServerAvailable = networkAvailable;
  result.networkUnavailableNotice = networkUnavailableNotice;
#endif
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

  if (networkUnavailableNotice) {
    networkUnavailableNotice = false;
    return PowerDialogAction::None;
  }

  const PowerDialogAction action = powerDialogHitAction(point, page);
  if (page == PowerDialogPage::Network) {
    const PowerDialogSnapshot current = snapshot();
    if (action == PowerDialogAction::HttpServerToggle &&
        !current.httpServerAvailable) {
      networkUnavailableNotice = true;
      return PowerDialogAction::None;
    }
    if (action == PowerDialogAction::FtpServerToggle &&
        !current.ftpServerAvailable) {
      networkUnavailableNotice = true;
      return PowerDialogAction::None;
    }
  }

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
  case PowerDialogAction::HotspotToggle:
  case PowerDialogAction::HttpServerToggle:
  case PowerDialogAction::FtpServerToggle:
  case PowerDialogAction::BluetoothToggle:
  case PowerDialogAction::CpuCycle:
  case PowerDialogAction::VolumeDown:
  case PowerDialogAction::VolumeMute:
  case PowerDialogAction::VolumeUp:
  case PowerDialogAction::Screenshot:
  case PowerDialogAction::VideoToggle:
  case PowerDialogAction::InvertToggle:
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
  case PowerDialogAction::HotspotToggle:
#if ENABLE_NETWORK_APPS
    if (net::tetheringActive()) {
      net::networkServicesShutdown();
      net::stopTethering();
    } else {
      net::startGeneratedTethering();
    }
#endif
    return;
  case PowerDialogAction::HttpServerToggle:
#if ENABLE_NETWORK_APPS
    net::toggleHttpService();
#endif
    return;
  case PowerDialogAction::FtpServerToggle:
#if ENABLE_NETWORK_APPS
    net::toggleFtpService();
#endif
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
  case PowerDialogAction::InvertToggle:
    if (actions.toggleScreenInversion != nullptr) {
      actions.toggleScreenInversion(actions.context);
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
  case PowerDialogPage::Network:
    return PowerDialogPage::Device;
  case PowerDialogPage::Volume:
#if ENABLE_NETWORK_APPS
    return PowerDialogPage::Network;
#else
    return PowerDialogPage::Device;
#endif
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
#if ENABLE_NETWORK_APPS
    return PowerDialogPage::Network;
#else
    return PowerDialogPage::Volume;
#endif
  case PowerDialogPage::Network:
    return PowerDialogPage::Volume;
  case PowerDialogPage::Volume:
    return PowerDialogPage::Screen;
  case PowerDialogPage::Screen:
    return PowerDialogPage::Power;
  }
  return PowerDialogPage::Power;
}
