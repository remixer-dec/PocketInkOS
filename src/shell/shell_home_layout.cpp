#include "shell/shell_home_layout.h"
#include "shell/shell_lockscreen_layout.h"
#include "shell/wallpaper.h"
#include "sys/global.h"
#include "sys/system_config.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

static const uint16_t SHELL_WIDGETS_DEFAULT =
    SHELL_WIDGET_TEMP | SHELL_WIDGET_HUM | SHELL_WIDGET_CPU_TEMP |
    SHELL_WIDGET_SD | SHELL_WIDGET_BATTERY | SHELL_WIDGET_LOGO_TEXT |
    SHELL_WIDGET_BUTTON_HELP;
static const uint16_t SHELL_WIDGETS_ALL =
    SHELL_WIDGETS_DEFAULT | SHELL_WIDGET_NOTIFICATIONS |
    SHELL_WIDGET_WALLPAPER | SHELL_WIDGET_WEATHER;

ShellLayoutConfig shellConfig = {
    SHELL_LAYOUT_DEFAULT,
    SHELL_TEMP_CELSIUS,
    SHELL_WIDGETS_DEFAULT,
};

bool asciiEqualIgnoreCase(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    const char ca = static_cast<char>(tolower(static_cast<unsigned char>(*a)));
    const char cb = static_cast<char>(tolower(static_cast<unsigned char>(*b)));
    if (ca != cb) {
      return false;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

void copyValue(char *dest, size_t destSize, const char *source) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  const char *text = source != nullptr ? source : "";
  strncpy(dest, text, destSize - 1);
  dest[destSize - 1] = '\0';
}

char *trim(char *text) {
  if (text == nullptr) {
    return text;
  }
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    text++;
  }
  char *end = text + strlen(text);
  while (end > text && isspace(static_cast<unsigned char>(end[-1]))) {
    *--end = '\0';
  }
  return text;
}

bool widgetToken(const char *token, uint16_t &widget) {
  if (asciiEqualIgnoreCase(token, "temp")) {
    widget = SHELL_WIDGET_TEMP;
  } else if (asciiEqualIgnoreCase(token, "hum") ||
             asciiEqualIgnoreCase(token, "humidity")) {
    widget = SHELL_WIDGET_HUM;
  } else if (asciiEqualIgnoreCase(token, "cpuTemp") ||
             asciiEqualIgnoreCase(token, "cpu-temp")) {
    widget = SHELL_WIDGET_CPU_TEMP;
  } else if (asciiEqualIgnoreCase(token, "sd") ||
             asciiEqualIgnoreCase(token, "sdCapacity")) {
    widget = SHELL_WIDGET_SD;
  } else if (asciiEqualIgnoreCase(token, "battery")) {
    widget = SHELL_WIDGET_BATTERY;
  } else if (asciiEqualIgnoreCase(token, "notifications")) {
    widget = SHELL_WIDGET_NOTIFICATIONS;
  } else if (asciiEqualIgnoreCase(token, "logoText")) {
    widget = SHELL_WIDGET_LOGO_TEXT;
  } else if (asciiEqualIgnoreCase(token, "buttonHelp") ||
             asciiEqualIgnoreCase(token, "buttons")) {
    widget = SHELL_WIDGET_BUTTON_HELP;
  } else if (asciiEqualIgnoreCase(token, "wallpaper")) {
    widget = SHELL_WIDGET_WALLPAPER;
  } else if (asciiEqualIgnoreCase(token, "weather")) {
    widget = SHELL_WIDGET_WEATHER;
  } else {
    return false;
  }
  return true;
}

uint16_t parseWidgetMask(const char *value, uint16_t fallback) {
  if (asciiEqualIgnoreCase(value, "all")) {
    return SHELL_WIDGETS_ALL;
  }
  if (asciiEqualIgnoreCase(value, "none") || value == nullptr ||
      value[0] == '\0') {
    return 0;
  }

  char copy[CONFIG_VALUE_MAX];
  copyValue(copy, sizeof(copy), value);
  uint16_t mask = 0;
  char *cursor = copy;
  bool any = false;
  while (cursor != nullptr && *cursor != '\0') {
    char *comma = strchr(cursor, ',');
    if (comma != nullptr) {
      *comma = '\0';
    }
    char *token = trim(cursor);
    uint16_t widget = 0;
    if (widgetToken(token, widget)) {
      mask |= widget;
      any = true;
    }
    cursor = comma != nullptr ? comma + 1 : nullptr;
  }
  return any ? mask : fallback;
}

void resetShellConfigDefaults() {
  shellConfig.layout = SHELL_LAYOUT_DEFAULT;
  shellConfig.temperatureUnit = SHELL_TEMP_CELSIUS;
  shellConfig.homeWidgets = SHELL_WIDGETS_DEFAULT;
}

float displayTemperature(float celsius) {
  if (shellConfig.temperatureUnit == SHELL_TEMP_FAHRENHEIT) {
    return celsius * 9.0f / 5.0f + 32.0f;
  }
  return celsius;
}

char displayTemperatureSuffix() {
  return shellConfig.temperatureUnit == SHELL_TEMP_FAHRENHEIT ? 'F' : 'C';
}

void drawCenteredText(AppDisplay &display, const char *text, int16_t y) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  display.setCursor((EPD_WIDTH - textW) / 2 - textX, y);
  display.print(text);
}

/// Text widgets share a vertical rhythm per side, so hiding one naturally
/// closes the gap before every following widget.
struct HomeTextColumn {
  int16_t x;
  int16_t nextY;
  int16_t rowHeight;
  bool rightAligned;

  void add(AppDisplay &display, const char *text) {
    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    display.getTextBounds(text, 0, nextY, &textX, &textY, &textW, &textH);
    const int16_t cursorX =
        rightAligned ? x - static_cast<int16_t>(textW) - textX : x;
    display.setCursor(cursorX, nextY);
    display.print(text);
    nextY += rowHeight;
  }
};

} // namespace

void shellHomeLayoutConfigReload() {
  resetShellConfigDefaults();

  char value[CONFIG_VALUE_MAX];
  if (configGet("layout", value, sizeof(value))) {
    shellConfig.layout = asciiEqualIgnoreCase(value, "ultra-large")
                             ? SHELL_LAYOUT_ULTRA_LARGE
                             : SHELL_LAYOUT_DEFAULT;
  }
  if (configGet("temp.unit", value, sizeof(value))) {
    shellConfig.temperatureUnit =
        asciiEqualIgnoreCase(value, "f") ||
                asciiEqualIgnoreCase(value, "fahrenheit")
            ? SHELL_TEMP_FAHRENHEIT
            : SHELL_TEMP_CELSIUS;
  }
  if (configGet("home.widgets", value, sizeof(value))) {
    shellConfig.homeWidgets = parseWidgetMask(value, shellConfig.homeWidgets);
  }
}

void shellLayoutConfigReload() {
  shellHomeLayoutConfigReload();
  lockscreenLayoutConfigReload();
}

const ShellLayoutConfig &shellLayoutConfig() { return shellConfig; }

bool shellHomeWidgetEnabled(uint16_t widget) {
  return (shellConfig.homeWidgets & widget) != 0;
}

void drawHomeScreen(AppDisplay &display, const ShellData &data) {
  drawWallpaper(display, WallpaperTarget::Home);
  display.setTextColor(1);

  int16_t titleX = 0;
  int16_t titleY = 0;
  uint16_t titleW = 0;
  uint16_t titleH = 0;
  if (shellHomeWidgetEnabled(SHELL_WIDGET_LOGO_TEXT)) {
    display.setTextSize(shellConfig.layout == SHELL_LAYOUT_ULTRA_LARGE ? 3 : 2);
    display.getTextBounds("Pocket Ink", 0, 0, &titleX, &titleY, &titleW,
                          &titleH);
    display.setCursor((EPD_WIDTH - titleW) / 2 - titleX,
                      shellConfig.layout == SHELL_LAYOUT_ULTRA_LARGE ? 68 : 72);
    display.print("Pocket Ink");

    display.setTextSize(1);
    int16_t osX;
    int16_t osY;
    uint16_t osW;
    uint16_t osH;
    display.getTextBounds("os", 0, 0, &osX, &osY, &osW, &osH);
    const int16_t logoRight =
        static_cast<int16_t>((EPD_WIDTH - titleW) / 2 - titleX + titleW);
    display.setCursor(logoRight - osW - osX, 94);
    display.print("os");
  }

  drawStatusBar(display, data.status);

  HomeTextColumn leftColumn = {4, 22, 12, false};
  HomeTextColumn rightColumn = {EPD_WIDTH - 4, 22, 12, true};

  if (shellHomeWidgetEnabled(SHELL_WIDGET_BATTERY)) {
    char batteryText[20];
    if (data.battery != nullptr && data.battery->valid) {
      snprintf(batteryText, sizeof(batteryText), "BAT %d%% %.2fV",
               data.battery->percentage, data.battery->voltage);
    } else {
      snprintf(batteryText, sizeof(batteryText), "BAT --%% --.--V");
    }
    leftColumn.add(display, batteryText);
  }

  if (shellHomeWidgetEnabled(SHELL_WIDGET_SD)) {
    char sdText[24];
    if (data.sd != nullptr && data.sd->mounted) {
      snprintf(sdText, sizeof(sdText), "SD %lu/%luGB free",
               static_cast<unsigned long>(data.sd->freeGb),
               static_cast<unsigned long>(data.sd->totalGb));
    } else {
      snprintf(sdText, sizeof(sdText), "SD no card");
    }
    leftColumn.add(display, sdText);
  }

  const EnvironmentSnapshot *env = data.environment;
  char ambientText[12];
  char humidityText[12];
  char chipText[14];
  if (env != nullptr && env->ambientTemperatureValid) {
    snprintf(ambientText, sizeof(ambientText), "%.1f%c",
             displayTemperature(env->ambientTemperatureC),
             displayTemperatureSuffix());
  } else {
    snprintf(ambientText, sizeof(ambientText), "--%c",
             displayTemperatureSuffix());
  }
  if (env != nullptr && env->ambientHumidityValid) {
    snprintf(humidityText, sizeof(humidityText), "%.1f%%",
             env->ambientHumidityPct);
  } else {
    snprintf(humidityText, sizeof(humidityText), "--%%");
  }
  if (env != nullptr && env->chipTemperatureValid) {
    snprintf(chipText, sizeof(chipText), "CPU %.1f%c",
             displayTemperature(env->chipTemperatureC),
             displayTemperatureSuffix());
  } else {
    snprintf(chipText, sizeof(chipText), "CPU --%c",
             displayTemperatureSuffix());
  }
  if (shellHomeWidgetEnabled(SHELL_WIDGET_TEMP)) {
    rightColumn.add(display, ambientText);
  }
  if (shellHomeWidgetEnabled(SHELL_WIDGET_HUM)) {
    rightColumn.add(display, humidityText);
  }
  if (shellHomeWidgetEnabled(SHELL_WIDGET_CPU_TEMP)) {
    rightColumn.add(display, chipText);
  }

  if (shellHomeWidgetEnabled(SHELL_WIDGET_BUTTON_HELP)) {
    drawCenteredText(display, "SUN: contact", 182);
    drawCenteredText(display, "PWR: apps", 192);
  }
}
