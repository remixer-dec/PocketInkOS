#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include "sys/system_config.h"
#include "sys/services/touch_input.h"
#include "ui/components/network_settings_component.h"
#include "ui/components/t9_keyboard_component.h"

#include <Adafruit_GFX.h>

/// Navigates editable system configuration as a compact files-style tree.
class SettingsApp {
public:
  void reset();
  void release();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handlePowerButton();
  bool update();
  bool hasActiveSession() const;

private:
  enum class Page : uint8_t {
    Root,
    Interface,
    Group,
    Options,
    Widgets,
    IconPicker,
    SunButtonApps,
    Input,
    AppList,
    AppSettings,
    AppOptions,
    AppInput,
    Network,
    About,
  };
  Page page = Page::Root;
  uint8_t group = 0;
  uint8_t editingSetting = 0xff;
  uint8_t appIndex = 0xff;
  uint8_t appSettingIndex = 0xff;
  uint16_t scrollOffset = 0;
  int16_t selectedIndex = -1;
  char *input = nullptr;
  uint8_t inputCapacity = 0;
  uint16_t widgetMask = 0;
  T9KeyboardComponent inputKeyboard;
  NetworkSettingsComponent networkSettings;
  char popup[20] = {};
  unsigned long popupUntil = 0;

  void resetList();
  void openInterface();
  void openGroup(uint8_t nextGroup);
  void openOptions(uint8_t setting);
  void openIconPicker(uint8_t setting);
  void openSunButtonApps(uint8_t setting);
  void openInput(uint8_t setting);
  void openWidgets();
  void openAppList();
  void openAppSettings(uint8_t nextAppIndex);
  void openAppOptions(uint8_t nextSettingIndex);
  void openAppInput(uint8_t nextSettingIndex);
  bool allocateInput(uint8_t capacity);
  void releaseInput();
  bool handleListTouch(const TouchPoint &point);
  bool handleInputTouch(const TouchPoint &point);
  void activateListIndex(uint16_t index);
  void saveSetting(const char *value);
  void saveWidgets();
  void saveAppSetting(const char *value);
  void showPopup(const char *message);
  bool popupVisible() const;
};

#endif
