#include "apps/settings_app.h"

#include "shell/shell_home_layout.h"
#include "sys/app_config.h"
#include "sys/builtin_apps.h"
#include "sys/global.h"
#include "sys/system_config.h"
#include "ui/components/list_component.h"
#include "ui/icon_ascii_font.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t kNoSetting = 0xff;
constexpr int16_t kHeaderY = 5;
constexpr int16_t kHeaderLineY = 18;
constexpr int16_t kListY = 24;
constexpr int16_t kRowHeight = 22;
constexpr int16_t kListBottomY = 199;
constexpr int16_t kListSplitX = 148;
constexpr uint8_t kVisibleRows = 7;
constexpr unsigned long kPopupMs = 1200;
constexpr UiRect kAboutUpdateButton = {34, 112, 132, 30};
constexpr char kFirstIconGlyph = '!';
constexpr char kLastIconGlyph = '~';
constexpr uint8_t kIconPickerCount =
    static_cast<uint8_t>(kLastIconGlyph - kFirstIconGlyph + 1);

enum Group : uint8_t {
  GROUP_ROOT = 0,
  GROUP_TIME,
  GROUP_CALIBRATION,
  GROUP_WIFI,
  GROUP_GENERAL,
  GROUP_HOME,
  GROUP_LOCKSCREEN,
};

enum RootItem : uint8_t {
  ROOT_TIME,
  ROOT_CALIBRATION,
  ROOT_WIFI,
  ROOT_INTERFACE,
  ROOT_APPS,
  ROOT_ABOUT,
};

enum SettingId : uint8_t {
  SETTING_TIME_FORMAT,
  SETTING_DATE_FORMAT,
  SETTING_SYNC_PROTOCOL,
  SETTING_SYNC_SERVER,
  SETTING_TIMEZONE,
  SETTING_BATTERY_CALIBRATION,
  SETTING_TEMPERATURE_OFFSET,
  SETTING_WIFI_SSID,
  SETTING_WIFI_PASSWORD,
  SETTING_WIFI_DISPLAY_NAME,
  SETTING_WIFI_AUTO_CONNECT,
  SETTING_HOME_LAYOUT,
  SETTING_HOME_SUN_BUTTON,
  SETTING_HOME_WIDGETS,
  SETTING_LOCKSCREEN_ICON,
  SETTING_LOCKSCREEN_DATE,
  SETTING_LOCKSCREEN_INACTIVITY,
};

enum class SettingKind : uint8_t {
  Options,
  Text,
  Numeric,
  MultiSelect,
  IconPicker,
  AppPicker,
  Reserved,
};

struct SettingOption {
  const char *label;
  const char *value;
};

struct InactivityOption {
  const char *label;
  uint8_t minutes;
};

struct SettingDefinition {
  SettingId id;
  const char *label;
  const char *key;
  SettingKind kind;
  const SettingOption *options;
  uint8_t optionCount;
  uint8_t inputCapacity;
};

const char *const kRootLabels[] = {"Date & time", "Calibration", "Wi-Fi",
                                   "Interface", "Apps", "About"};
const char *const kInterfaceLabels[] = {"General", "Home", "Lockscreen"};

const SettingOption kTimeFormatOptions[] = {
    {"24 hour", "24h"},
    {"12 hour", "12h"},
};
const SettingOption kDateFormatOptions[] = {
    {"YYYY-MM-DD", "iso"},
    {"MM/DD/YYYY", "mdy"},
    {"DD/MM/YYYY", "dmy"},
    {"DD.MM.YYYY", "dmy-dot"},
    {"Weekday", "weekday"},
};
const SettingOption kSyncProtocolOptions[] = {
    {"HTTPS", "https"},
    {"NTP", "ntp"},
};
const SettingOption kSyncServerOptions[] = {
    {"HTTPS default", "https://time.now/developer/api/ip"},
    {"NTP pool", "pool.ntp.org"},
    {"NTP Google", "time.google.com"},
    {"NTP Cloudflare", "time.cloudflare.com"},
};
const SettingOption kTimezoneOptions[] = {
    {"AUTO", "auto"}, {"GMT-8", "UTC-8"}, {"GMT-5", "UTC-5"},
    {"GMT-3", "UTC-3"}, {"GMT", "UTC"},     {"GMT+1", "UTC+1"},
    {"GMT+3", "UTC+3"}, {"GMT+5:30", "UTC+5:30"},
    {"GMT+8", "UTC+8"}, {"GMT+10", "UTC+10"},
};
const SettingOption kBoolOptions[] = {
    {"OFF", "false"},
    {"ON", "true"},
};
const SettingOption kLayoutOptions[] = {
    {"Default", "default"},
    {"Ultra large", "ultra-large"},
};
const InactivityOption kInactivityOptions[] = {
    {"0 (never)", 0}, {"1 min", 1}, {"2 min", 2},
    {"3 min", 3},     {"5 min", 5}, {"10 min", 10},
};
constexpr uint8_t kInactivityOptionCount =
    sizeof(kInactivityOptions) / sizeof(kInactivityOptions[0]);

const SettingDefinition kTimeSettings[] = {
    {SETTING_TIME_FORMAT, "Time format", "time.format", SettingKind::Options,
     kTimeFormatOptions,
     static_cast<uint8_t>(sizeof(kTimeFormatOptions) / sizeof(kTimeFormatOptions[0])),
     0},
    {SETTING_DATE_FORMAT, "Date format", "date.format", SettingKind::Options,
     kDateFormatOptions,
     static_cast<uint8_t>(sizeof(kDateFormatOptions) / sizeof(kDateFormatOptions[0])),
     0},
    {SETTING_SYNC_PROTOCOL, "Sync protocol", "time.syncProtocol",
     SettingKind::Options, kSyncProtocolOptions,
     static_cast<uint8_t>(sizeof(kSyncProtocolOptions) /
                          sizeof(kSyncProtocolOptions[0])),
     0},
    {SETTING_SYNC_SERVER, "Sync server", "time.syncServer", SettingKind::Options,
     kSyncServerOptions,
     static_cast<uint8_t>(sizeof(kSyncServerOptions) / sizeof(kSyncServerOptions[0])),
     0},
    {SETTING_TIMEZONE, "Timezone", "timezone", SettingKind::Options,
     kTimezoneOptions,
     static_cast<uint8_t>(sizeof(kTimezoneOptions) / sizeof(kTimezoneOptions[0])),
     0},
};

const SettingDefinition kCalibrationSettings[] = {
    {SETTING_BATTERY_CALIBRATION, "Battery calibration", nullptr,
     SettingKind::Reserved, nullptr, 0, 0},
    {SETTING_TEMPERATURE_OFFSET, "Temperature offset", "sensor.tempOffsetC",
     SettingKind::Numeric, nullptr, 0, 16},
};

const SettingDefinition kWifiSettings[] = {
    {SETTING_WIFI_SSID, "SSID", "wifi.ssid", SettingKind::Text, nullptr, 0,
     64},
    {SETTING_WIFI_PASSWORD, "Password", "wifi.password", SettingKind::Text,
     nullptr, 0, 64},
    {SETTING_WIFI_DISPLAY_NAME, "Display name", "wifi.displayName",
     SettingKind::Text, nullptr, 0, 24},
    {SETTING_WIFI_AUTO_CONNECT, "Auto connect", "wifi.autoConnect",
     SettingKind::Options, kBoolOptions,
     static_cast<uint8_t>(sizeof(kBoolOptions) / sizeof(kBoolOptions[0])), 0},
};

const SettingDefinition kGeneralSettings[] = {
    // Layout is intentionally only a selector; the setting owns no extra UI logic.
    {SETTING_HOME_LAYOUT, "Layout", "layout", SettingKind::Options,
     kLayoutOptions,
     static_cast<uint8_t>(sizeof(kLayoutOptions) / sizeof(kLayoutOptions[0])),
     0},
};

const SettingDefinition kHomeSettings[] = {
    {SETTING_HOME_SUN_BUTTON, "SUN button app", "home.sunBtnApp",
     SettingKind::AppPicker, nullptr, 0, 32},
    {SETTING_HOME_WIDGETS, "Widgets", "home.widgets", SettingKind::MultiSelect,
     nullptr, 0, CONFIG_VALUE_MAX},
};

const SettingDefinition kLockscreenSettings[] = {
    {SETTING_LOCKSCREEN_INACTIVITY, "On inactivity",
     "lockscreen.inactivityMinutes", SettingKind::Options,
     nullptr, kInactivityOptionCount,
     5},
    {SETTING_LOCKSCREEN_ICON, "Top icon", "lockscreen.topIcon",
     SettingKind::IconPicker, nullptr, 0, 2},
    {SETTING_LOCKSCREEN_DATE, "Show date", "lockscreen.date",
     SettingKind::Options, kBoolOptions,
     static_cast<uint8_t>(sizeof(kBoolOptions) / sizeof(kBoolOptions[0])), 0},
};

struct SettingGroupDefinition {
  const char *title;
  const SettingDefinition *settings;
  uint8_t count;
};

const SettingGroupDefinition kSettingGroups[] = {
    {"DATE & TIME", kTimeSettings,
     static_cast<uint8_t>(sizeof(kTimeSettings) / sizeof(kTimeSettings[0]))},
    {"CALIBRATION", kCalibrationSettings,
     static_cast<uint8_t>(sizeof(kCalibrationSettings) /
                          sizeof(kCalibrationSettings[0]))},
    {"WI-FI", kWifiSettings,
     static_cast<uint8_t>(sizeof(kWifiSettings) / sizeof(kWifiSettings[0]))},
    {"GENERAL", kGeneralSettings,
     static_cast<uint8_t>(sizeof(kGeneralSettings) /
                          sizeof(kGeneralSettings[0]))},
    {"HOME", kHomeSettings,
     static_cast<uint8_t>(sizeof(kHomeSettings) / sizeof(kHomeSettings[0]))},
    {"LOCKSCREEN", kLockscreenSettings,
     static_cast<uint8_t>(sizeof(kLockscreenSettings) /
                          sizeof(kLockscreenSettings[0]))},
};

void drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  gfx.setCursor(EPD_WIDTH - 4 - static_cast<int16_t>(textW) - textX, y);
  gfx.print(text);
}

void drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                      uint8_t textSize = 1) {
  gfx.setTextSize(textSize);
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(textW)) / 2 - textX, y);
  gfx.print(text);
}

void copyShortText(char *out, size_t outSize, const char *source) {
  const char *text = source != nullptr ? source : "";
  if (outSize == 0) {
    return;
  }
  const size_t length = strlen(text);
  if (length + 1 <= outSize) {
    memcpy(out, text, length + 1);
    return;
  }
  const size_t prefix = outSize > 4 ? outSize - 4 : 0;
  if (prefix > 0) {
    memcpy(out, text, prefix);
  }
  if (outSize >= 4) {
    memcpy(out + prefix, "...", 4);
  } else {
    out[outSize - 1] = '\0';
  }
}

struct WidgetOption {
  const char *label;
  const char *token;
  uint16_t bit;
};

const WidgetOption kWidgetOptions[] = {
    {"Battery", "battery", SHELL_WIDGET_BATTERY},
    {"SD card", "sd", SHELL_WIDGET_SD},
    {"Temperature", "temp", SHELL_WIDGET_TEMP},
    {"Humidity", "hum", SHELL_WIDGET_HUM},
    {"CPU temperature", "cpuTemp", SHELL_WIDGET_CPU_TEMP},
    {"Logo text", "logoText", SHELL_WIDGET_LOGO_TEXT},
    {"Button help", "buttonHelp", SHELL_WIDGET_BUTTON_HELP},
};
constexpr uint8_t kWidgetOptionCount =
    sizeof(kWidgetOptions) / sizeof(kWidgetOptions[0]);

uint16_t widgetMaskFromValue(const char *value) {
  if (value == nullptr || strcmp(value, "none") == 0) {
    return 0;
  }
  uint16_t mask = 0;
  for (const WidgetOption &option : kWidgetOptions) {
    if (strstr(value, option.token) != nullptr) {
      mask |= option.bit;
    }
  }
  return mask;
}

bool hasStaticOption(const SettingDefinition &setting, const char *value) {
  for (uint8_t i = 0; i < setting.optionCount; i++) {
    if (strcmp(setting.options[i].value, value) == 0) {
      return true;
    }
  }
  return false;
}

const InactivityOption *inactivityOptionForMinutes(uint8_t minutes) {
  for (const InactivityOption &option : kInactivityOptions) {
    if (option.minutes == minutes) {
      return &option;
    }
  }
  return nullptr;
}

uint8_t optionCountFor(const SettingDefinition &setting) {
  if (setting.id == SETTING_LOCKSCREEN_INACTIVITY) {
    return setting.optionCount + 1;
  }
  if (setting.id != SETTING_SYNC_SERVER) {
    return setting.optionCount;
  }
  char current[CONFIG_VALUE_MAX] = {};
  configGet(setting.key, current, sizeof(current));
  return static_cast<uint8_t>(setting.optionCount +
                              (!hasStaticOption(setting, current) ? 1 : 0));
}

bool optionForIndex(const SettingDefinition &setting, uint16_t index,
                    const char *&label, const char *&value,
                    char *customLabel, size_t customLabelSize,
                    char *customValue, size_t customValueSize) {
  if (setting.id == SETTING_LOCKSCREEN_INACTIVITY) {
    if (index < kInactivityOptionCount) {
      const InactivityOption &option = kInactivityOptions[index];
      label = option.label;
      snprintf(customValue, customValueSize, "%u",
               static_cast<unsigned>(option.minutes));
      value = customValue;
      return true;
    }
    if (index != kInactivityOptionCount) {
      return false;
    }
    configGet(setting.key, customValue, customValueSize);
    uint8_t minutes = 0;
    if (configParseLockscreenInactivityMinutes(customValue, minutes) &&
        inactivityOptionForMinutes(minutes) == nullptr) {
      snprintf(customLabel, customLabelSize, "Custom: %u min",
               static_cast<unsigned>(minutes));
    } else {
      snprintf(customLabel, customLabelSize, "Custom...");
    }
    label = customLabel;
    value = "custom";
    return true;
  }
  if (index < setting.optionCount) {
    label = setting.options[index].label;
    value = setting.options[index].value;
    return true;
  }
  if (setting.id != SETTING_SYNC_SERVER || index != setting.optionCount) {
    return false;
  }
  configGet(setting.key, customValue, customValueSize);
  if (hasStaticOption(setting, customValue)) {
    return false;
  }
  snprintf(customLabel, customLabelSize, "Current: %s", customValue);
  label = customLabel;
  value = customValue;
  return true;
}

const SettingDefinition *settingsForGroup(Group group, uint8_t &count) {
  const uint8_t index = static_cast<uint8_t>(group);
  if (index >= GROUP_TIME && index <= GROUP_LOCKSCREEN) {
    const SettingGroupDefinition &definition = kSettingGroups[index - GROUP_TIME];
    count = definition.count;
    return definition.settings;
  }
  count = 0;
  return nullptr;
}

bool groupInsideInterface(Group group) {
  return group == GROUP_GENERAL || group == GROUP_HOME ||
         group == GROUP_LOCKSCREEN;
}

const SettingDefinition *settingById(uint8_t rawId) {
  for (uint8_t group = GROUP_TIME; group <= GROUP_LOCKSCREEN; group++) {
    uint8_t count = 0;
    const SettingDefinition *settings =
        settingsForGroup(static_cast<Group>(group), count);
    for (uint8_t i = 0; settings != nullptr && i < count; i++) {
      if (settings[i].id == static_cast<SettingId>(rawId)) {
        return &settings[i];
      }
    }
  }
  return nullptr;
}

const char *groupTitle(Group group) {
  const uint8_t index = static_cast<uint8_t>(group);
  if (index >= GROUP_TIME && index <= GROUP_LOCKSCREEN) {
    return kSettingGroups[index - GROUP_TIME].title;
  }
  return "SETTINGS";
}

const char *optionLabel(const SettingDefinition &setting, const char *value) {
  if (setting.options == nullptr || value == nullptr) {
    return value;
  }
  for (uint8_t i = 0; i < setting.optionCount; i++) {
    if (strcmp(setting.options[i].value, value) == 0) {
      return setting.options[i].label;
    }
  }
  return value;
}

const char *appOptionLabel(const AppConfigSetting &setting, const char *value) {
  for (uint8_t i = 0; i < setting.optionCount; i++) {
    if (strcmp(setting.options[i].value, value) == 0) {
      return setting.options[i].label;
    }
  }
  return value;
}

} // namespace

void SettingsApp::reset() {
  releaseInput();
  page = Page::Root;
  group = GROUP_ROOT;
  editingSetting = kNoSetting;
  appIndex = kNoSetting;
  appSettingIndex = kNoSetting;
  popup[0] = '\0';
  popupUntil = 0;
  inputKeyboard.setLayout(T9KeyboardComponent::Layout::Numbers);
  inputKeyboard.setLayout(T9KeyboardComponent::Layout::Text);
  resetList();
}

void SettingsApp::release() { releaseInput(); }

void SettingsApp::resetList(Mode nextMode) {
  mode = nextMode;
  scrollOffset = 0;
  selectedIndex = -1;
}

void SettingsApp::openInterface() {
  page = Page::Interface;
  group = GROUP_ROOT;
  resetList();
}

void SettingsApp::openGroup(uint8_t nextGroup) {
  group = nextGroup;
  page = Page::Group;
  resetList();
}

void SettingsApp::openOptions(uint8_t setting) {
  editingSetting = setting;
  page = Page::Options;
  resetList();
}

void SettingsApp::openIconPicker(uint8_t setting) {
  editingSetting = setting;
  page = Page::IconPicker;
  resetList();
}

void SettingsApp::openSunButtonApps(uint8_t setting) {
  editingSetting = setting;
  page = Page::SunButtonApps;
  resetList();
}

void SettingsApp::openInput(uint8_t setting) {
  const SettingDefinition *definition = settingById(setting);
  if (definition == nullptr || definition->key == nullptr) {
    return;
  }
  editingSetting = setting;
  const uint8_t capacity = definition->inputCapacity > 1
                               ? definition->inputCapacity
                               : CONFIG_VALUE_MAX;
  if (!allocateInput(capacity)) {
    return;
  }
  configGet(definition->key, input, inputCapacity);
  input[inputCapacity - 1] = '\0';
  inputKeyboard.setLayout(definition->kind == SettingKind::Numeric
                                  || definition->id == SETTING_LOCKSCREEN_INACTIVITY
                              ? T9KeyboardComponent::Layout::Numeric
                              : T9KeyboardComponent::Layout::Text);
  page = Page::Input;
}

void SettingsApp::openWidgets() {
  char value[CONFIG_VALUE_MAX] = {};
  configGet("home.widgets", value, sizeof(value));
  widgetMask = widgetMaskFromValue(value);
  page = Page::Widgets;
  resetList();
}

void SettingsApp::openAppList() {
  page = Page::AppList;
  resetList();
}

void SettingsApp::openAppSettings(uint8_t nextAppIndex) {
  if (appConfigRegistrationAt(nextAppIndex) == nullptr) {
    return;
  }
  appIndex = nextAppIndex;
  page = Page::AppSettings;
  resetList();
}

void SettingsApp::openAppOptions(uint8_t nextSettingIndex) {
  const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
  if (registration == nullptr || nextSettingIndex >= registration->settingCount) {
    return;
  }
  appSettingIndex = nextSettingIndex;
  page = Page::AppOptions;
  resetList();
}

void SettingsApp::openAppInput(uint8_t nextSettingIndex) {
  const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
  if (registration == nullptr || nextSettingIndex >= registration->settingCount) {
    return;
  }
  const AppConfigSetting &setting = registration->settings[nextSettingIndex];
  appSettingIndex = nextSettingIndex;
  const uint8_t capacity = setting.inputCapacity > 1 ? setting.inputCapacity
                                                       : CONFIG_VALUE_MAX;
  if (!allocateInput(capacity)) {
    return;
  }
  appConfigGet(*registration, setting, input, inputCapacity);
  input[inputCapacity - 1] = '\0';
  inputKeyboard.setLayout(setting.type == AppConfigType::Int ||
                                  setting.type == AppConfigType::Float
                              ? T9KeyboardComponent::Layout::Numeric
                              : T9KeyboardComponent::Layout::Text);
  page = Page::AppInput;
}

bool SettingsApp::allocateInput(uint8_t capacity) {
  releaseInput();
  if (capacity <= 1) {
    showPopup("NO MEMORY");
    return false;
  }
  input = static_cast<char *>(heap_caps_malloc(
      capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (input == nullptr) {
    input = static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
  }
  if (input == nullptr) {
    showPopup("NO MEMORY");
    return false;
  }
  inputCapacity = capacity;
  input[0] = '\0';
  return true;
}

void SettingsApp::releaseInput() {
  if (input != nullptr) {
    heap_caps_free(input);
    input = nullptr;
  }
  inputCapacity = 0;
}

bool SettingsApp::popupVisible() const {
  return popup[0] != '\0' && popupUntil != 0 &&
         static_cast<long>(millis() - popupUntil) < 0;
}

void SettingsApp::draw(Adafruit_GFX &gfx) {
  if (page == Page::Input || page == Page::AppInput) {
    inputKeyboard.draw(gfx, input != nullptr ? input : "",
                       inputCapacity > 0 ? inputCapacity - 1 : 0);
    return;
  }

  if (page == Page::About) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    gfx.setCursor(4, kHeaderY);
    gfx.print("ABOUT");
    gfx.drawLine(0, kHeaderLineY, EPD_WIDTH - 1, kHeaderLineY, 1);
    drawCenteredText(gfx, "Pocket Ink OS", 66, 2);
    drawCenteredText(gfx, "Pre-release version", 92);
#if ENABLE_NETWORK_APPS
    uiDrawButton(gfx, kAboutUpdateButton, "CHECK UPDATES");
#endif
    gfx.setTextSize(1);
    return;
  }

  gfx.setTextColor(1);
  gfx.setTextSize(1);
  const Group currentGroup = static_cast<Group>(group);
  const SettingDefinition *editing = settingById(editingSetting);
  const AppConfigRegistration *appRegistration = appConfigRegistrationAt(appIndex);
  const AppConfigSetting *appSetting =
      appRegistration != nullptr && appSettingIndex < appRegistration->settingCount
          ? &appRegistration->settings[appSettingIndex]
          : nullptr;
  const char *title = groupTitle(currentGroup);
  if (page == Page::Options && editing != nullptr) {
    title = editing->label;
  } else if (page == Page::Interface) {
    title = "INTERFACE";
  } else if (page == Page::Widgets) {
    title = "WIDGETS";
  } else if (page == Page::IconPicker) {
    title = "TOP ICON";
  } else if (page == Page::SunButtonApps) {
    title = "SUN BUTTON APP";
  } else if (page == Page::AppList) {
    title = "APPS";
  } else if (page == Page::AppSettings && appRegistration != nullptr) {
    title = appRegistration->label;
  } else if (page == Page::AppOptions && appSetting != nullptr) {
    title = appSetting->label;
  }
  const char *navigation = "SPLIT";
  if (mode == Mode::Scroll) {
    navigation = "SCROLL";
  } else if (mode == Mode::Select) {
    navigation = "SELECT";
  }
  gfx.setCursor(4, kHeaderY);
  gfx.print(title);
  drawRightText(gfx, navigation, kHeaderY);
  gfx.drawLine(0, kHeaderLineY, EPD_WIDTH - 1, kHeaderLineY, 1);

  uint16_t count = 0;
  const SettingDefinition *settings = nullptr;
  if (page == Page::Root) {
    count = sizeof(kRootLabels) / sizeof(kRootLabels[0]);
  } else if (page == Page::Interface) {
    count = sizeof(kInterfaceLabels) / sizeof(kInterfaceLabels[0]);
  } else if (page == Page::Group) {
    uint8_t groupCount = 0;
    settings = settingsForGroup(currentGroup, groupCount);
    count = groupCount;
  } else if (page == Page::Options && editing != nullptr) {
    count = optionCountFor(*editing);
  } else if (page == Page::Widgets) {
    count = kWidgetOptionCount + 1; // Final row commits all toggles together.
  } else if (page == Page::IconPicker) {
    count = kIconPickerCount;
  } else if (page == Page::SunButtonApps) {
    count = appCatalogAllCount();
  } else if (page == Page::AppList) {
    count = appConfigRegistrationCount();
  } else if (page == Page::AppSettings && appRegistration != nullptr) {
    count = appRegistration->settingCount;
  } else if (page == Page::AppOptions && appSetting != nullptr) {
    count = appSetting->optionCount;
  }
  uiListClampOffset(scrollOffset, count, kVisibleRows);
  const uint8_t visible = count - scrollOffset < kVisibleRows
                              ? static_cast<uint8_t>(count - scrollOffset)
                              : kVisibleRows;

  for (uint8_t row = 0; row < visible; row++) {
    const uint16_t index = scrollOffset + row;
    const int16_t y = kListY + row * kRowHeight;
    const bool selected = selectedIndex == static_cast<int16_t>(index);
    if (selected) {
      gfx.fillRect(2, y - 3, EPD_WIDTH - 4, kRowHeight - 1, 1);
      gfx.setTextColor(0);
    }

    const char *label = "";
    const char *value = "";
    char currentValue[CONFIG_VALUE_MAX] = {};
    char optionCustomLabel[CONFIG_VALUE_MAX] = {};
    char optionCustomValue[CONFIG_VALUE_MAX] = {};
    char pickerLabel[24] = {};
    char pickerValue[2] = {};
    AppCatalogEntry catalogEntry = {};
    bool valueUsesIconFont = false;
    if (page == Page::Root) {
      label = kRootLabels[index];
      value = ">";
    } else if (page == Page::Interface) {
      label = kInterfaceLabels[index];
      value = ">";
    } else if (page == Page::Group && settings != nullptr) {
      const SettingDefinition &setting = settings[index];
      label = setting.label;
      if (setting.kind == SettingKind::Reserved) {
        value = "reserved";
      } else {
        configGet(setting.key, currentValue, sizeof(currentValue));
        value = setting.kind == SettingKind::Options
                    ? optionLabel(setting, currentValue)
                    : currentValue;
        if (setting.id == SETTING_LOCKSCREEN_INACTIVITY) {
          uint8_t minutes = 0;
          if (configParseLockscreenInactivityMinutes(currentValue, minutes)) {
            const InactivityOption *option = inactivityOptionForMinutes(minutes);
            if (option != nullptr) {
              value = option->label;
            } else {
              snprintf(pickerLabel, sizeof(pickerLabel), "%u min",
                       static_cast<unsigned>(minutes));
              value = pickerLabel;
            }
          }
        }
        valueUsesIconFont = setting.kind == SettingKind::IconPicker;
        if (setting.kind == SettingKind::AppPicker) {
          if (findAppById(currentValue, catalogEntry)) {
            value = catalogEntry.definition.label;
          }
        }
        if (setting.id == SETTING_WIFI_PASSWORD && currentValue[0] != '\0') {
          value = "hidden";
        }
      }
    } else if (page == Page::Options && editing != nullptr) {
      const char *optionValue = "";
      if (!optionForIndex(*editing, index, label, optionValue,
                          optionCustomLabel, sizeof(optionCustomLabel),
                          optionCustomValue, sizeof(optionCustomValue))) {
        continue;
      }
      char current[CONFIG_VALUE_MAX] = {};
      configGet(editing->key, current, sizeof(current));
      if (editing->id == SETTING_LOCKSCREEN_INACTIVITY) {
        uint8_t minutes = 0;
        const bool valid =
            configParseLockscreenInactivityMinutes(current, minutes);
        if (index < kInactivityOptionCount) {
          value = valid && minutes == kInactivityOptions[index].minutes ? "*" : "";
        } else {
          value = valid && inactivityOptionForMinutes(minutes) == nullptr ? "*" : "";
        }
      } else {
        value = strcmp(current, optionValue) == 0 ? "*" : "";
      }
    } else if (page == Page::Widgets && index < kWidgetOptionCount) {
      label = kWidgetOptions[index].label;
      value = (widgetMask & kWidgetOptions[index].bit) != 0 ? "'" : ",";
      valueUsesIconFont = true;
    } else if (page == Page::Widgets && index == kWidgetOptionCount) {
      label = "Save widgets";
      value = ">";
    } else if (page == Page::IconPicker) {
      const char icon = static_cast<char>(kFirstIconGlyph + index);
      configGet("lockscreen.topIcon", currentValue, sizeof(currentValue));
      snprintf(pickerLabel, sizeof(pickerLabel), "%c Icon %u",
               currentValue[0] == icon ? '*' : ' ',
               static_cast<unsigned>(static_cast<uint8_t>(icon)));
      pickerValue[0] = icon;
      label = pickerLabel;
      value = pickerValue;
      valueUsesIconFont = true;
    } else if (page == Page::SunButtonApps) {
      if (tryGetAppAtIndex(index, catalogEntry)) {
        configGet("home.sunBtnApp", currentValue, sizeof(currentValue));
        label = catalogEntry.definition.label;
        value = strcmp(currentValue, catalogEntry.definition.id) == 0 ? "*" : "";
      }
    } else if (page == Page::AppList) {
      const AppConfigRegistration *registration =
          appConfigRegistrationAt(static_cast<uint8_t>(index));
      if (registration != nullptr) {
        label = registration->label;
        value = ">";
      }
    } else if (page == Page::AppSettings && appRegistration != nullptr &&
               index < appRegistration->settingCount) {
      const AppConfigSetting &setting = appRegistration->settings[index];
      label = setting.label;
      appConfigGet(*appRegistration, setting, currentValue,
                   sizeof(currentValue));
      value = (setting.type == AppConfigType::Bool ||
               setting.type == AppConfigType::OneOf)
                  ? appOptionLabel(setting, currentValue)
                  : currentValue;
    } else if (page == Page::AppOptions && appSetting != nullptr &&
               index < appSetting->optionCount) {
      label = appSetting->options[index].label;
      appConfigGet(*appRegistration, *appSetting, currentValue,
                   sizeof(currentValue));
      value = strcmp(currentValue, appSetting->options[index].value) == 0
                  ? "*"
                  : "";
    }

    char compactLabel[26];
    char compactValue[18];
    copyShortText(compactLabel, sizeof(compactLabel), label);
    copyShortText(compactValue, sizeof(compactValue), value);
    gfx.setCursor(8, y + 4);
    gfx.print(compactLabel);
    if (compactValue[0] != '\0') {
      if (valueUsesIconFont) {
        gfx.setFont(&iconASCII12pt7b);
        drawRightText(gfx, compactValue, y + 14);
        gfx.setFont();
      } else {
        drawRightText(gfx, compactValue, y + 4);
      }
    }
    if (selected) {
      gfx.setTextColor(1);
    }
  }

  if (popupVisible()) {
    const UiRect box = {46, 84, 108, 30};
    gfx.fillRect(box.x, box.y, box.w, box.h, 0);
    gfx.drawRect(box.x, box.y, box.w, box.h, 1);
    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    gfx.getTextBounds(popup, 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor(box.x + (box.w - static_cast<int16_t>(textW)) / 2 - textX,
                  box.y + 10);
    gfx.print(popup);
  }
}

bool SettingsApp::handleTouch(const TouchPoint &point) {
  if (popupVisible()) {
    popup[0] = '\0';
    popupUntil = 0;
    return true;
  }
  if (page == Page::About) {
#if ENABLE_NETWORK_APPS
    return uiTouchContains(kAboutUpdateButton, point);
#else
    return false;
#endif
  }
  return page == Page::Input || page == Page::AppInput
             ? handleInputTouch(point)
             : handleListTouch(point);
}

bool SettingsApp::handleListTouch(const TouchPoint &point) {
  uint16_t count = 0;
  if (page == Page::Root) {
    count = sizeof(kRootLabels) / sizeof(kRootLabels[0]);
  } else if (page == Page::Interface) {
    count = sizeof(kInterfaceLabels) / sizeof(kInterfaceLabels[0]);
  } else if (page == Page::Group) {
    uint8_t groupCount = 0;
    settingsForGroup(static_cast<Group>(group), groupCount);
    count = groupCount;
  } else if (page == Page::Options) {
    const SettingDefinition *editing = settingById(editingSetting);
    count = editing != nullptr ? optionCountFor(*editing) : 0;
  } else if (page == Page::Widgets) {
    count = kWidgetOptionCount + 1;
  } else if (page == Page::IconPicker) {
    count = kIconPickerCount;
  } else if (page == Page::SunButtonApps) {
    count = appCatalogAllCount();
  } else if (page == Page::AppList) {
    count = appConfigRegistrationCount();
  } else if (page == Page::AppSettings) {
    const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
    count = registration != nullptr ? registration->settingCount : 0;
  } else if (page == Page::AppOptions) {
    const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
    count = registration != nullptr && appSettingIndex < registration->settingCount
                ? registration->settings[appSettingIndex].optionCount
                : 0;
  }

  uint16_t nextOffset = scrollOffset;
  UiListNavMode listMode = UiListNavMode::Split;
  if (mode == Mode::Scroll) {
    listMode = UiListNavMode::Scroll;
  } else if (mode == Mode::Select) {
    listMode = UiListNavMode::Select;
  }
  const UiListTouchResult result = uiListHandleTouch(
      point, count, kVisibleRows, kListY, kRowHeight, kListBottomY - kListY,
      kListSplitX, listMode, nextOffset);
  if (result.action == UiListTouchAction::Scroll) {
    if (nextOffset == scrollOffset) {
      return false;
    }
    scrollOffset = nextOffset;
    selectedIndex = -1;
    return true;
  }
  if (result.action != UiListTouchAction::Select) {
    return false;
  }
  if (page == Page::Widgets) {
    activateListIndex(result.index);
    return true;
  }
  if (selectedIndex != static_cast<int16_t>(result.index)) {
    selectedIndex = result.index;
    return true;
  }
  activateListIndex(result.index);
  return true;
}

void SettingsApp::activateListIndex(uint16_t index) {
  if (page == Page::Root) {
    if (index == ROOT_APPS) {
      openAppList();
    } else if (index == ROOT_INTERFACE) {
      openInterface();
    } else if (index == ROOT_ABOUT) {
      page = Page::About;
      resetList();
    } else if (index <= ROOT_WIFI) {
      const Group rootGroups[] = {GROUP_TIME, GROUP_CALIBRATION, GROUP_WIFI};
      openGroup(static_cast<uint8_t>(rootGroups[index]));
    }
    return;
  }

  if (page == Page::Interface &&
      index < sizeof(kInterfaceLabels) / sizeof(kInterfaceLabels[0])) {
    const Group interfaceGroups[] = {GROUP_GENERAL, GROUP_HOME,
                                     GROUP_LOCKSCREEN};
    openGroup(static_cast<uint8_t>(interfaceGroups[index]));
    return;
  }

  if (page == Page::Group) {
    uint8_t count = 0;
    const SettingDefinition *settings =
        settingsForGroup(static_cast<Group>(group), count);
    if (settings == nullptr || index >= count) {
      return;
    }
    const SettingDefinition &setting = settings[index];
    if (setting.kind == SettingKind::Reserved) {
      showPopup("RESERVED");
    } else if (setting.kind == SettingKind::Options) {
      openOptions(setting.id);
    } else if (setting.kind == SettingKind::IconPicker) {
      openIconPicker(setting.id);
    } else if (setting.kind == SettingKind::AppPicker) {
      openSunButtonApps(setting.id);
    } else if (setting.kind == SettingKind::MultiSelect) {
      openWidgets();
    } else {
      openInput(setting.id);
    }
    return;
  }

  if (page == Page::Widgets && index < kWidgetOptionCount) {
    widgetMask ^= kWidgetOptions[index].bit;
    return;
  }
  if (page == Page::Widgets && index == kWidgetOptionCount) {
    saveWidgets();
    return;
  }

  if (page == Page::IconPicker && index < kIconPickerCount) {
    const char value[2] = {static_cast<char>(kFirstIconGlyph + index), '\0'};
    saveSetting(value);
    return;
  }

  if (page == Page::SunButtonApps) {
    AppCatalogEntry app;
    if (tryGetAppAtIndex(index, app)) {
      saveSetting(app.definition.id);
    }
    return;
  }

  if (page == Page::AppList) {
    openAppSettings(static_cast<uint8_t>(index));
    return;
  }

  if (page == Page::AppSettings) {
    const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
    if (registration == nullptr || index >= registration->settingCount) {
      return;
    }
    const AppConfigSetting &setting = registration->settings[index];
    if (setting.type == AppConfigType::Bool ||
        setting.type == AppConfigType::OneOf) {
      openAppOptions(static_cast<uint8_t>(index));
    } else {
      openAppInput(static_cast<uint8_t>(index));
    }
    return;
  }

  if (page == Page::AppOptions) {
    const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
    if (registration == nullptr || appSettingIndex >= registration->settingCount) {
      return;
    }
    const AppConfigSetting &setting = registration->settings[appSettingIndex];
    if (index < setting.optionCount) {
      saveAppSetting(setting.options[index].value);
    }
    return;
  }

  const SettingDefinition *setting = settingById(editingSetting);
  if (setting != nullptr) {
    if (setting->id == SETTING_LOCKSCREEN_INACTIVITY &&
        index == setting->optionCount) {
      openInput(setting->id);
      return;
    }
    char customLabel[CONFIG_VALUE_MAX] = {};
    char customValue[CONFIG_VALUE_MAX] = {};
    const char *label = nullptr;
    const char *value = nullptr;
    if (optionForIndex(*setting, index, label, value, customLabel,
                       sizeof(customLabel), customValue, sizeof(customValue))) {
      saveSetting(value);
    }
  }
}

void SettingsApp::saveWidgets() {
  char value[CONFIG_VALUE_MAX] = {};
  size_t used = 0;
  for (const WidgetOption &option : kWidgetOptions) {
    if ((widgetMask & option.bit) == 0) {
      continue;
    }
    const int written = snprintf(value + used, sizeof(value) - used, "%s%s",
                                 used == 0 ? "" : ",", option.token);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(value) - used) {
      break;
    }
    used += static_cast<size_t>(written);
  }
  if (used == 0) {
    strncpy(value, "none", sizeof(value) - 1);
  }
  const bool saved = configSet("home.widgets", value);
  if (saved) {
    shellLayoutConfigReload();
  }
  showPopup(saved ? "SAVED" : "SAVE FAILED");
}

bool SettingsApp::handleInputTouch(const TouchPoint &point) {
  if (input == nullptr || inputCapacity <= 1) {
    return false;
  }
  const bool inputWasEmpty = input[0] == '\0';
  const KeyboardEvent event =
      inputKeyboard.hitTest(point, input, static_cast<int>(inputCapacity));
  if (event.action == KEY_NONE) {
    return false;
  }
  if (event.action == KEY_OK) {
    if (page == Page::AppInput) {
      saveAppSetting(input);
    } else {
      saveSetting(input);
    }
  } else if (event.action == KEY_BACKSPACE && inputWasEmpty) {
    page = page == Page::AppInput ? Page::AppSettings : Page::Group;
    releaseInput();
    resetList();
  }
  return true;
}

void SettingsApp::saveAppSetting(const char *value) {
  const AppConfigRegistration *registration = appConfigRegistrationAt(appIndex);
  if (registration == nullptr || appSettingIndex >= registration->settingCount) {
    return;
  }
  const AppConfigSetting &setting = registration->settings[appSettingIndex];
  const bool saved = appConfigSet(*registration, setting, value);
  releaseInput();
  page = Page::AppSettings;
  resetList();
  showPopup(saved ? "SAVED" : "SAVE FAILED");
}

void SettingsApp::saveSetting(const char *value) {
  const SettingDefinition *setting = settingById(editingSetting);
  if (setting == nullptr || setting->key == nullptr) {
    return;
  }

  bool saved = false;
  if (setting->id == SETTING_SYNC_PROTOCOL) {
    char currentProtocol[16] = {};
    configGet(setting->key, currentProtocol, sizeof(currentProtocol));
    if (strcmp(currentProtocol, value) == 0) {
      saved = true;
    } else {
      const char *server = strcmp(value, "ntp") == 0
                               ? "pool.ntp.org"
                               : "https://time.now/developer/api/ip";
      saved = configSet("time.syncServer", server) &&
              configSet(setting->key, value);
    }
  } else {
    saved = configSet(setting->key, value);
  }
  if (saved) {
    shellLayoutConfigReload();
  }
  releaseInput();
  page = Page::Group;
  resetList();
  showPopup(saved ? "SAVED" : "SAVE FAILED");
}

void SettingsApp::showPopup(const char *message) {
  const char *text = message != nullptr ? message : "";
  strncpy(popup, text, sizeof(popup) - 1);
  popup[sizeof(popup) - 1] = '\0';
  popupUntil = millis() + kPopupMs;
}

bool SettingsApp::handleMenuButton() {
  if (popupVisible()) {
    popup[0] = '\0';
    popupUntil = 0;
    return true;
  }
  if (page == Page::Input) {
    page = Page::Group;
    releaseInput();
    resetList();
    return true;
  }
  if (page == Page::AppInput || page == Page::AppOptions) {
    if (page == Page::AppInput) {
      releaseInput();
    }
    page = Page::AppSettings;
    resetList();
    return true;
  }
  if (page == Page::Options) {
    page = Page::Group;
    resetList();
    return true;
  }
  if (page == Page::About || page == Page::Interface) {
    page = Page::Root;
    group = GROUP_ROOT;
    resetList();
    return true;
  }
  if (page == Page::Widgets || page == Page::IconPicker ||
      page == Page::SunButtonApps) {
    page = Page::Group;
    resetList();
    return true;
  }
  if (page == Page::AppSettings) {
    page = Page::AppList;
    resetList();
    return true;
  }
  if (page == Page::AppList) {
    page = Page::Root;
    resetList();
    return true;
  }
  if (page == Page::Group) {
    if (groupInsideInterface(static_cast<Group>(group))) {
      openInterface();
      return true;
    }
    page = Page::Root;
    group = GROUP_ROOT;
    resetList();
    return true;
  }
  return false;
}

bool SettingsApp::handlePowerButton() {
  if (page == Page::Input || page == Page::AppInput) {
    inputKeyboard.toggleCaps();
    return true;
  }
  if (mode == Mode::Split) {
    mode = Mode::Scroll;
  } else if (mode == Mode::Scroll) {
    mode = Mode::Select;
  } else {
    mode = Mode::Split;
  }
  selectedIndex = -1;
  return true;
}

bool SettingsApp::update() {
  bool changed = (page == Page::Input || page == Page::AppInput) &&
                 inputKeyboard.update();
  if (popupUntil != 0 && static_cast<long>(millis() - popupUntil) >= 0) {
    popup[0] = '\0';
    popupUntil = 0;
    changed = true;
  }
  return changed;
}

bool SettingsApp::hasActiveSession() const {
  return page == Page::Input || page == Page::AppInput;
}
