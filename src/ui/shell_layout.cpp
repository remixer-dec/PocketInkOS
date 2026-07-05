#include "ui/shell_layout.h"
#include "sys/system_config.h"
#include "sys/global.h"
#include "ui/icon_ascii_font.h"
#include "ui/ui_helpers.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

static const uint8_t MENU_COLUMNS = 3;
static const uint8_t MENU_ROWS = 3;
static const uint8_t MENU_GRID_SIZE = MENU_COLUMNS * MENU_ROWS;
static const uint8_t MENU_PAGE_SIZE = MENU_GRID_SIZE - 1;
static const uint8_t MENU_NEXT_SLOT = MENU_GRID_SIZE - 1;
static const int16_t MENU_X = 20;
static const int16_t MENU_Y = 38;
static const int16_t MENU_CELL_W = 60;
static const int16_t MENU_CELL_H = 52;
static const int16_t MENU_ICON_W = 40;
static const int16_t MENU_ICON_H = 32;
static const int16_t MENU_HEADER_H = 34;

static const UiRect QUIT_YES_BUTTON = {44, 112, 48, 24};
static const UiRect QUIT_NO_BUTTON = {108, 112, 48, 24};
static const UiRect POWER_PREV_PAGE_BUTTON = {14, 56, 54, 44};
static const UiRect POWER_NEXT_PAGE_BUTTON = {132, 56, 54, 44};
static const UiRect POWER_LEFT_BUTTON = {20, 100, 46, 44};
static const UiRect POWER_MIDDLE_BUTTON = {77, 100, 46, 44};
static const UiRect POWER_RIGHT_BUTTON = {134, 100, 46, 44};
static const int16_t DIALOG_BACKDROP_TOP_Y = 20;
static const int16_t DIALOG_HATCH_STEP = 6;

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
    'S',
    true,
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

void drawDialogBackdropHatch(AppDisplay &display) {
  for (int16_t x = -EPD_HEIGHT; x < EPD_WIDTH; x += DIALOG_HATCH_STEP) {
    const int16_t diagonal = x + EPD_HEIGHT - 1;
    int16_t x0 = x;
    int16_t y0 = EPD_HEIGHT - 1;
    int16_t x1 = diagonal - DIALOG_BACKDROP_TOP_Y;
    int16_t y1 = DIALOG_BACKDROP_TOP_Y;
    if (x0 < 0) {
      y0 = diagonal;
      x0 = 0;
    }
    if (x1 >= EPD_WIDTH) {
      x1 = EPD_WIDTH - 1;
      y1 = diagonal - x1;
    }
    if (y0 >= DIALOG_BACKDROP_TOP_Y && y1 >= DIALOG_BACKDROP_TOP_Y &&
        y0 < EPD_HEIGHT && y1 < EPD_HEIGHT) {
      display.drawLine(x0, y0, x1, y1, 1);
    }
  }
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

bool parseBoolValue(const char *value, bool fallback) {
  if (asciiEqualIgnoreCase(value, "1") || asciiEqualIgnoreCase(value, "true") ||
      asciiEqualIgnoreCase(value, "yes") || asciiEqualIgnoreCase(value, "on")) {
    return true;
  }
  if (asciiEqualIgnoreCase(value, "0") ||
      asciiEqualIgnoreCase(value, "false") ||
      asciiEqualIgnoreCase(value, "no") ||
      asciiEqualIgnoreCase(value, "off")) {
    return false;
  }
  return fallback;
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
  shellConfig.lockscreenTopIcon = 'S';
  shellConfig.lockscreenDate = true;
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

void drawRightAlignedText(AppDisplay &display, const char *text, int16_t y) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  display.setCursor(EPD_WIDTH - textW - 4 - textX, y);
  display.print(text);
}

void drawIconGlyph(AppDisplay &display, const UiRect &rect, char icon,
                   bool selected = false, int16_t yOffset = 0) {
  char text[2] = {icon, '\0'};
  display.setFont(&iconASCII12pt7b);
  display.setTextSize(1);
  display.setTextColor(selected ? 0 : 1);

  int16_t iconX;
  int16_t iconY;
  uint16_t iconW;
  uint16_t iconH;
  display.getTextBounds(text, 0, 0, &iconX, &iconY, &iconW, &iconH);
  display.setCursor(rect.x + (rect.w - static_cast<int16_t>(iconW)) / 2 -
                        iconX,
                    rect.y + (rect.h - static_cast<int16_t>(iconH)) / 2 -
                        iconY + 1 + yOffset);
  display.print(text);
  display.setFont();
  display.setTextSize(1);
  display.setTextColor(1);
}

void drawIconButton(AppDisplay &display, const UiRect &rect, char icon,
                    bool selected = false) {
  display.drawRect(rect.x, rect.y, rect.w, rect.h, 1);
  if (selected && rect.w > 2 && rect.h > 2) {
    display.fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 1);
  }
  drawIconGlyph(display, rect, icon, selected);
}

void drawCenteredButtonText(AppDisplay &display, const UiRect &rect,
                            const char *text, int16_t yOffset,
                            uint8_t textSize = 1) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.setFont();
  display.setTextSize(textSize);
  display.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  display.setCursor(rect.x + (rect.w - static_cast<int16_t>(textW)) / 2 -
                        textX,
                    rect.y + yOffset);
  display.print(text);
}

void drawIconStateButton(AppDisplay &display, const UiRect &rect, char icon,
                         const char *state, bool selected = false) {
  display.drawRect(rect.x, rect.y, rect.w, rect.h, 1);
  if (selected && rect.w > 2 && rect.h > 2) {
    display.fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 1);
  }
  UiRect iconRect = {rect.x, static_cast<int16_t>(rect.y + 1), rect.w, 26};
  drawIconGlyph(display, iconRect, icon, selected, -1);
  display.setTextColor(selected ? 0 : 1);
  drawCenteredButtonText(display, rect, state, 33);
  display.setTextColor(1);
}

void drawTextStateButton(AppDisplay &display, const UiRect &rect,
                         const char *text, const char *state,
                         bool selected = false) {
  display.drawRect(rect.x, rect.y, rect.w, rect.h, 1);
  if (selected && rect.w > 2 && rect.h > 2) {
    display.fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 1);
  }
  display.setTextColor(selected ? 0 : 1);
  drawCenteredButtonText(display, rect, text, 9, 2);
  drawCenteredButtonText(display, rect, state, 33);
  display.setTextColor(1);
  display.setTextSize(1);
}

void drawPowerDialogFrame(AppDisplay &display, const char *title) {
  display.fillRect(14, 56, 172, 94, 0);
  display.drawRect(14, 56, 172, 94, 1);
  display.drawLine(14, 150, 185, 150, 1);
  display.drawLine(14, 151, 185, 151, 1);
  display.drawLine(14, 152, 185, 152, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  drawCenteredText(display, title, 76);
  drawIconGlyph(display, POWER_PREV_PAGE_BUTTON, '<');
  drawIconGlyph(display, POWER_NEXT_PAGE_BUTTON, '>');
}

const char *powerPageTitle(PowerDialogPage page) {
  switch (page) {
  case PowerDialogPage::Power:
    return "Power";
  case PowerDialogPage::Device:
    return "Device";
  case PowerDialogPage::Volume:
    return "Volume";
  }
  return "Power";
}

void drawCenteredIcon(AppDisplay &display, char icon, int16_t y,
                      uint8_t textSize = 1) {
  char text[2] = {icon, '\0'};
  display.setFont(&iconASCII12pt7b);
  display.setTextSize(textSize);
  display.setTextColor(1);

  int16_t iconX;
  int16_t iconY;
  uint16_t iconW;
  uint16_t iconH;
  display.getTextBounds(text, 0, 0, &iconX, &iconY, &iconW, &iconH);
  display.setCursor((EPD_WIDTH - static_cast<int16_t>(iconW)) / 2 - iconX, y);
  display.print(text);
  display.setFont();
  display.setTextSize(1);
}

uint8_t pageCountForCategory(MenuCategory category) {
  const size_t count = appCatalogCount(category);
  if (count == 0) {
    return 1;
  }
  if (count <= MENU_GRID_SIZE) {
    return 1;
  }
  return static_cast<uint8_t>((count + MENU_PAGE_SIZE - 1) / MENU_PAGE_SIZE);
}

bool appAtVisibleSlot(const MenuState &state, uint8_t slot,
                      AppCatalogEntry &out) {
  const size_t targetIndex =
      static_cast<size_t>(state.page) * MENU_PAGE_SIZE + slot;
  return appCatalogAtVisibleIndex(state.category, targetIndex, out);
}

bool hasMenuNextPage(const MenuState &state) {
  return state.page + 1 < pageCountForCategory(state.category);
}

void drawMenuTitle(AppDisplay &display, const MenuState &state) {
  char title[24];
  const uint8_t pages = pageCountForCategory(state.category);
  if (pages > 1) {
    snprintf(title, sizeof(title), "%s %u/%u", menuCategoryTitle(state.category),
             static_cast<unsigned>(state.page + 1), static_cast<unsigned>(pages));
  } else {
    snprintf(title, sizeof(title), "%s", menuCategoryTitle(state.category));
  }

  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  display.getTextBounds(title, 0, 10, &titleX, &titleY, &titleW, &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2, 10);
  display.print(title);
}

void drawMenuItem(AppDisplay &display, const AppDefinition &app, uint8_t slot,
                  bool selected) {
  const uint8_t row = slot / MENU_COLUMNS;
  const uint8_t col = slot % MENU_COLUMNS;
  const int16_t x = MENU_X + col * MENU_CELL_W;
  const int16_t y = MENU_Y + row * MENU_CELL_H;

  if (selected) {
    display.fillRect(x + 1, y + 1, MENU_ICON_W - 2, MENU_ICON_H - 2, 1);
  }
  display.drawRect(x, y, MENU_ICON_W, MENU_ICON_H, 1);
  display.setTextColor(selected ? 0 : 1);
  if (app.iconFont && app.icon != nullptr && app.icon[0] != '\0') {
    display.setFont(&iconASCII12pt7b);
    display.setTextSize(1);
    int16_t iconX;
    int16_t iconY;
    uint16_t iconW;
    uint16_t iconH;
    display.getTextBounds(app.icon, 0, 0, &iconX, &iconY, &iconW, &iconH);
    display.setCursor(x + (MENU_ICON_W - static_cast<int16_t>(iconW)) / 2 -
                          iconX,
                      y + (MENU_ICON_H - static_cast<int16_t>(iconH)) / 2 -
                          iconY + 1);
    display.print(app.icon);
    display.setFont();
  } else {
    display.setTextSize(2);
    display.setCursor(x + 14, y + 8);
    display.print(app.icon);
  }

  display.setTextColor(1);
  display.setTextSize(1);
  int16_t labelX;
  int16_t labelY;
  uint16_t labelW;
  uint16_t labelH;
  display.getTextBounds(app.label, 0, 0, &labelX, &labelY, &labelW, &labelH);
  display.setCursor(x + (MENU_ICON_W - static_cast<int>(labelW)) / 2 - labelX,
                    y + 35);
  display.print(app.label);
}

void drawMenuNextPage(AppDisplay &display, uint8_t slot, bool selected) {
  const uint8_t row = slot / MENU_COLUMNS;
  const uint8_t col = slot % MENU_COLUMNS;
  const int16_t x = MENU_X + col * MENU_CELL_W;
  const int16_t y = MENU_Y + row * MENU_CELL_H;

  display.drawRect(x, y, MENU_ICON_W, MENU_ICON_H, 1);
  if (selected) {
    display.fillRect(x + 1, y + 1, MENU_ICON_W - 2, MENU_ICON_H - 2, 1);
  }

  display.setTextColor(selected ? 0 : 1);
  display.setTextSize(2);
  int16_t arrowX;
  int16_t arrowY;
  uint16_t arrowW;
  uint16_t arrowH;
  display.getTextBounds(">", 0, 0, &arrowX, &arrowY, &arrowW, &arrowH);
  display.setCursor(x + (MENU_ICON_W - static_cast<int16_t>(arrowW)) / 2 -
                        arrowX,
                    y + (MENU_ICON_H - static_cast<int16_t>(arrowH)) / 2 -
                        arrowY);
  display.print(">");

  display.setTextColor(1);
  display.setTextSize(1);
  display.setCursor(x + 10, y + 35);
  display.print("next");
}

} // namespace

void shellLayoutConfigReload() {
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
  if (configGet("lockscreen.topIcon", value, sizeof(value)) &&
      value[0] != '\0') {
    shellConfig.lockscreenTopIcon = value[0];
  }
  if (configGet("lockscreen.date", value, sizeof(value))) {
    shellConfig.lockscreenDate =
        parseBoolValue(value, shellConfig.lockscreenDate);
  }
}

const ShellLayoutConfig &shellLayoutConfig() { return shellConfig; }

bool shellHomeWidgetEnabled(uint16_t widget) {
  return (shellConfig.homeWidgets & widget) != 0;
}

MenuCategory previousMenuCategory(MenuCategory category) {
  switch (category) {
  case MENU_GAMES:
#if ENABLE_NETWORK_APPS
    return MENU_NETWORK;
#else
    return MENU_APPS;
#endif
  case MENU_APPS:
    return MENU_GAMES;
#if ENABLE_NETWORK_APPS
  case MENU_NETWORK:
    return MENU_APPS;
#endif
  }
  return MENU_GAMES;
}

MenuCategory nextMenuCategory(MenuCategory category) {
  switch (category) {
  case MENU_GAMES:
    return MENU_APPS;
  case MENU_APPS:
#if ENABLE_NETWORK_APPS
    return MENU_NETWORK;
#else
    return MENU_GAMES;
#endif
#if ENABLE_NETWORK_APPS
  case MENU_NETWORK:
    return MENU_GAMES;
#endif
  }
  return MENU_GAMES;
}

const char *menuCategoryTitle(MenuCategory category) {
  switch (category) {
  case MENU_GAMES:
    return "GAMES";
  case MENU_APPS:
    return "APPS";
#if ENABLE_NETWORK_APPS
  case MENU_NETWORK:
    return "NETWORK";
#endif
  }
  return "GAMES";
}

void clampMenuState(MenuState &state) {
  const uint8_t pages = pageCountForCategory(state.category);
  if (state.page >= pages) {
    state.page = pages - 1;
  }
}

void moveMenuPrevious(MenuState &state) {
  clampMenuState(state);
  if (state.page > 0) {
    state.page--;
    return;
  }
  state.category = previousMenuCategory(state.category);
  state.page = pageCountForCategory(state.category) - 1;
}

void moveMenuNext(MenuState &state) {
  clampMenuState(state);
  const uint8_t pages = pageCountForCategory(state.category);
  if (state.page + 1 < pages) {
    state.page++;
    return;
  }
  state.category = nextMenuCategory(state.category);
  state.page = 0;
}

void drawHomeScreen(AppDisplay &display, const ShellData &data) {
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

  if (shellHomeWidgetEnabled(SHELL_WIDGET_BATTERY)) {
    char batteryText[20];
    if (data.battery != nullptr && data.battery->valid) {
      snprintf(batteryText, sizeof(batteryText), "BAT %d%% %.2fV",
               data.battery->percentage, data.battery->voltage);
    } else {
      snprintf(batteryText, sizeof(batteryText), "BAT --%% --.--V");
    }
    display.setCursor(4, 22);
    display.print(batteryText);
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
    display.setCursor(4, 34);
    display.print(sdText);
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
    drawRightAlignedText(display, ambientText, 22);
  }
  if (shellHomeWidgetEnabled(SHELL_WIDGET_HUM)) {
    drawRightAlignedText(display, humidityText, 34);
  }
  if (shellHomeWidgetEnabled(SHELL_WIDGET_CPU_TEMP)) {
    drawRightAlignedText(display, chipText, 46);
  }

  if (shellHomeWidgetEnabled(SHELL_WIDGET_BUTTON_HELP)) {
    drawCenteredText(display, "SUN: contact", 182);
    drawCenteredText(display, "PWR: apps", 192);
  }
}

void drawAppMenu(AppDisplay &display, const MenuState &state,
                 int8_t pressedSlot) {
  display.setTextColor(1);
  display.setTextSize(2);
  display.setCursor(8, 10);
  display.print("<");
  display.setCursor(184, 10);
  display.print(">");
  drawMenuTitle(display, state);

  for (uint8_t slot = 0; slot < MENU_GRID_SIZE; slot++) {
    if (slot == MENU_NEXT_SLOT && hasMenuNextPage(state)) {
      drawMenuNextPage(display, slot, pressedSlot == slot);
      continue;
    }

    AppCatalogEntry app;
    if (appAtVisibleSlot(state, slot, app)) {
      drawMenuItem(display, app.definition, slot, pressedSlot == slot);
    }
  }

  display.setTextSize(1);
}

bool hitTestAppMenu(const TouchPoint &point, MenuState &state,
                    AppCatalogEntry &selected, bool &stateChanged,
                    int8_t *hitSlot) {
  stateChanged = false;
  if (hitSlot != nullptr) {
    *hitSlot = -1;
  }
  if (point.y < MENU_HEADER_H) {
    if (point.x < 48) {
      moveMenuPrevious(state);
      stateChanged = true;
    } else if (point.x > 152) {
      moveMenuNext(state);
      stateChanged = true;
    }
    return false;
  }

  if (point.x < MENU_X || point.x >= EPD_WIDTH || point.y < MENU_Y ||
      point.y >= MENU_Y + MENU_ROWS * MENU_CELL_H) {
    return false;
  }

  const uint8_t col = (point.x - MENU_X) / MENU_CELL_W;
  const uint8_t row = (point.y - MENU_Y) / MENU_CELL_H;
  if (col >= MENU_COLUMNS || row >= MENU_ROWS) {
    return false;
  }
  const uint8_t slot = row * MENU_COLUMNS + col;
  if (slot == MENU_NEXT_SLOT && hasMenuNextPage(state)) {
    state.page++;
    stateChanged = true;
    if (hitSlot != nullptr) {
      *hitSlot = static_cast<int8_t>(slot);
    }
    return false;
  }

  if (!appAtVisibleSlot(state, slot, selected)) {
    return false;
  }
  if (hitSlot != nullptr) {
    *hitSlot = static_cast<int8_t>(slot);
  }
  return true;
}

void drawQuitDialog(AppDisplay &display, const StatusBarSnapshot &status) {
  drawStatusBar(display, status);

  drawDialogBackdropHatch(display);

  display.fillRect(24, 62, 152, 86, 0);
  display.drawRect(24, 62, 152, 86, 1);
  display.drawLine(24, 148, 175, 148, 1);
  display.drawLine(24, 149, 175, 149, 1);
  display.drawLine(24, 150, 175, 150, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  drawCenteredText(display, "Do you really", 80);
  drawCenteredText(display, "want to quit?", 96);
  uiDrawButton(display, QUIT_YES_BUTTON, "YES");
  uiDrawButton(display, QUIT_NO_BUTTON, "NO");
}

bool quitDialogHitYes(const TouchPoint &point) {
  return uiContains(QUIT_YES_BUTTON, point);
}

bool quitDialogHitNo(const TouchPoint &point) {
  return uiContains(QUIT_NO_BUTTON, point);
}

void drawPowerDialog(AppDisplay &display, const StatusBarSnapshot &status,
                     const PowerDialogSnapshot &snapshot,
                     PowerDialogAction pressed) {
  drawStatusBar(display, status);
  drawDialogBackdropHatch(display);

  drawPowerDialogFrame(display, powerPageTitle(snapshot.page));
  if (snapshot.page == PowerDialogPage::Power) {
    drawIconButton(display, POWER_LEFT_BUTTON, 'Z',
                   pressed == PowerDialogAction::Reboot);
    drawIconButton(display, POWER_MIDDLE_BUTTON, 'j',
                   pressed == PowerDialogAction::PowerOff);
    drawIconButton(display, POWER_RIGHT_BUTTON, 'S',
                   pressed == PowerDialogAction::DeepSleep);
    return;
  }

  if (snapshot.page == PowerDialogPage::Device) {
    const char wifiIcon = snapshot.wifiConnected ? 'g' : 'f';
    const char *wifiState =
        snapshot.wifiConnected ? "OK" : (snapshot.wifiOn ? "ON" : "OFF");
    drawIconStateButton(display, POWER_LEFT_BUTTON, wifiIcon, wifiState,
                        pressed == PowerDialogAction::WifiToggle);
    drawIconStateButton(display, POWER_MIDDLE_BUTTON, '0',
                        snapshot.bluetoothOn ? "ON" : "OFF",
                        pressed == PowerDialogAction::BluetoothToggle);
    char cpuText[8];
    snprintf(cpuText, sizeof(cpuText), "%u",
             static_cast<unsigned>(snapshot.cpuMhz));
    drawIconStateButton(display, POWER_RIGHT_BUTTON, ':', cpuText,
                        pressed == PowerDialogAction::CpuCycle);
    return;
  }

  char volumeText[12];
  snprintf(volumeText, sizeof(volumeText), "%u%%",
           static_cast<unsigned>(snapshot.volume));
  drawCenteredText(display, snapshot.muted ? "MUTED" : volumeText, 90);
  drawIconButton(display, POWER_LEFT_BUTTON, 'b',
                 pressed == PowerDialogAction::VolumeDown);
  drawIconButton(display, POWER_MIDDLE_BUTTON, snapshot.muted ? '"' : '!',
                 pressed == PowerDialogAction::VolumeMute);
  drawIconButton(display, POWER_RIGHT_BUTTON, 'X',
                 pressed == PowerDialogAction::VolumeUp);
}

PowerDialogAction powerDialogHitAction(const TouchPoint &point,
                                       PowerDialogPage page) {
  if (uiContains(POWER_PREV_PAGE_BUTTON, point)) {
    return PowerDialogAction::PreviousPage;
  }
  if (uiContains(POWER_NEXT_PAGE_BUTTON, point)) {
    return PowerDialogAction::NextPage;
  }

  if (page == PowerDialogPage::Power && uiContains(POWER_LEFT_BUTTON, point)) {
    return PowerDialogAction::Reboot;
  }
  if (page == PowerDialogPage::Power && uiContains(POWER_MIDDLE_BUTTON, point)) {
    return PowerDialogAction::PowerOff;
  }
  if (page == PowerDialogPage::Power && uiContains(POWER_RIGHT_BUTTON, point)) {
    return PowerDialogAction::DeepSleep;
  }
  if (page == PowerDialogPage::Device && uiContains(POWER_LEFT_BUTTON, point)) {
    return PowerDialogAction::WifiToggle;
  }
  if (page == PowerDialogPage::Device && uiContains(POWER_MIDDLE_BUTTON, point)) {
    return PowerDialogAction::BluetoothToggle;
  }
  if (page == PowerDialogPage::Device && uiContains(POWER_RIGHT_BUTTON, point)) {
    return PowerDialogAction::CpuCycle;
  }
  if (page == PowerDialogPage::Volume && uiContains(POWER_LEFT_BUTTON, point)) {
    return PowerDialogAction::VolumeDown;
  }
  if (page == PowerDialogPage::Volume && uiContains(POWER_MIDDLE_BUTTON, point)) {
    return PowerDialogAction::VolumeMute;
  }
  if (page == PowerDialogPage::Volume && uiContains(POWER_RIGHT_BUTTON, point)) {
    return PowerDialogAction::VolumeUp;
  }
  return PowerDialogAction::None;
}

void drawPowerOffScreen(AppDisplay &display) {
  display.fillScreen(0);
}

void drawLowBatteryScreen(AppDisplay &display) {
  display.fillScreen(0);
  drawCenteredIcon(display, 'E', 114, 2);
}

void drawDeepSleepScreen(AppDisplay &display) {
  display.fillScreen(0);
  drawCenteredIcon(display, shellConfig.lockscreenTopIcon, 114, 2);
}

void drawDeepSleepClockScreen(AppDisplay &display, const char *timeText,
                              const char *dateText) {
  display.fillScreen(0);
  if (shellConfig.lockscreenTopIcon != '\0') {
    drawCenteredIcon(display, shellConfig.lockscreenTopIcon, 42, 1);
  }

  display.setFont();
  display.setTextColor(1);
  display.setTextSize(4);
  drawCenteredText(display, timeText, 104);

  if (shellConfig.lockscreenDate) {
    display.setTextSize(1);
    drawCenteredText(display, dateText, 140);
  }
  display.setTextSize(1);
}
