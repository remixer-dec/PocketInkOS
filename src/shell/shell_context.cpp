#include "shell/shell_context.h"
#include "sys/services/connectivity_service.h"
#include "sys/builtin_apps.h"
#include "sys/services/device_clock.h"
#include "sys/services/device_controls.h"
#include "sys/rtc_context.h"
#include <cstring>

namespace {

bool restorableShellScreen(Screen value) {
  return value == SCREEN_HOME || value == SCREEN_MENU;
}

} // namespace

Screen shellContextScreenForSave(Screen screen, bool textInputScreen,
                                 const AppDefinition *activeDefinition) {
  if (textInputScreen) {
    return SCREEN_HOME;
  }
  if (activeDefinition != nullptr) {
    return SCREEN_APP;
  }
  if (restorableShellScreen(screen)) {
    return screen;
  }
  return SCREEN_HOME;
}

void shellContextSaveForSleep(Screen screen, bool textInputScreen,
                              const AppDefinition *activeDefinition,
                              const MenuState &menuState) {
  RtcContextSnapshot snapshot = {};
  snapshot.navigation.screen =
      shellContextScreenForSave(screen, textInputScreen, activeDefinition);
  snapshot.navigation.menuCategory = menuState.category;
  snapshot.navigation.menuPage = menuState.page;
  snapshot.system.wifiOn = wifiIsOn();
  snapshot.system.clockSet =
      deviceClock.snapshotLocalUnix(snapshot.system.clockLocalUnix);
  DeviceControlSnapshot controls = deviceControlsSnapshot();
  snapshot.system.volume = controls.volume;
  snapshot.system.muted = controls.muted;

  if (activeDefinition != nullptr && activeDefinition->id != nullptr) {
    snapshot.navigation.hasActiveApp = true;
    strncpy(snapshot.navigation.appId, activeDefinition->id,
            RTC_CONTEXT_APP_ID_SIZE - 1);
    snapshot.navigation.appId[RTC_CONTEXT_APP_ID_SIZE - 1] = '\0';

    if (activeDefinition->behavior.onSaveContext != nullptr) {
      size_t length = activeDefinition->behavior.onSaveContext(
          snapshot.app.appData, RTC_CONTEXT_APP_CAPACITY);
      if (length > RTC_CONTEXT_APP_CAPACITY) {
        length = RTC_CONTEXT_APP_CAPACITY;
      }
      snapshot.app.appDataLength = static_cast<uint16_t>(length);
    }
  }

  rtcContextSave(snapshot);
}

bool shellContextRestoreAfterSleep(MenuState &menuState,
                                   const ShellContextRestoreActions &actions) {
  RtcContextSnapshot snapshot;
  if (!rtcContextLoad(snapshot)) {
    return false;
  }

  if (!deviceClock.isSet() && snapshot.system.clockSet) {
    deviceClock.restoreLocalUnix(snapshot.system.clockLocalUnix);
  }

  restoreWifiOn(snapshot.system.wifiOn);
  deviceControlsRestoreAudio(snapshot.system.volume, snapshot.system.muted);
  menuState.category = snapshot.navigation.menuCategory;
  menuState.page = snapshot.navigation.menuPage;

  if (snapshot.navigation.hasActiveApp && actions.launchApp != nullptr) {
    AppCatalogEntry app;
    if (findAppById(snapshot.navigation.appId, app)) {
      actions.launchApp(actions.context, app.definition);
      const AppDefinition *active =
          actions.activeDefinition != nullptr
              ? actions.activeDefinition(actions.context)
                                              : nullptr;
      if (active != nullptr && active->behavior.onRestoreContext != nullptr) {
        active->behavior.onRestoreContext(snapshot.app.appData,
                                          snapshot.app.appDataLength);
      }
      rtcContextClear();
      return true;
    }
  }

  if (restorableShellScreen(snapshot.navigation.screen) &&
      actions.switchTo != nullptr) {
    actions.switchTo(actions.context, snapshot.navigation.screen);
  }

  rtcContextClear();
  return true;
}
