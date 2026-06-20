#include "ui/shell_layout.h"
#include "sys/global.h"
#include "ui/icon_ascii_font.h"
#include "ui/ui_helpers.h"
#include <stdio.h>

namespace {

static const uint8_t MENU_COLUMNS = 3;
static const uint8_t MENU_ROWS = 3;
static const uint8_t MENU_PAGE_SIZE = MENU_COLUMNS * MENU_ROWS;
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
  return static_cast<uint8_t>((count + MENU_PAGE_SIZE - 1) / MENU_PAGE_SIZE);
}

bool appAtVisibleSlot(const MenuState &state, uint8_t slot,
                      AppCatalogEntry &out) {
  const size_t targetIndex =
      static_cast<size_t>(state.page) * MENU_PAGE_SIZE + slot;
  return appCatalogAtVisibleIndex(state.category, targetIndex, out);
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

} // namespace

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
  display.setTextSize(2);

  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  display.getTextBounds("Pocket Ink", 0, 0, &titleX, &titleY, &titleW,
                        &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2 - titleX, 72);
  display.print("Pocket Ink");

  display.setTextSize(1);
  int16_t osX;
  int16_t osY;
  uint16_t osW;
  uint16_t osH;
  display.getTextBounds("os", 0, 0, &osX, &osY, &osW, &osH);
  display.setCursor((EPD_WIDTH - titleW) / 2 - titleX + titleW - osW - osX,
                    94);
  display.print("os");

  drawStatusBar(display, data.status);

  char batteryText[20];
  if (data.battery != nullptr && data.battery->valid) {
    snprintf(batteryText, sizeof(batteryText), "BAT %d%% %.2fV",
             data.battery->percentage, data.battery->voltage);
  } else {
    snprintf(batteryText, sizeof(batteryText), "BAT --%% --.--V");
  }
  display.setCursor(4, 22);
  display.print(batteryText);

  char sdText[24];
  if (data.sd != nullptr && data.sd->mounted) {
    snprintf(sdText, sizeof(sdText), "[SD] %lu/%luGB free",
             static_cast<unsigned long>(data.sd->freeGb),
             static_cast<unsigned long>(data.sd->totalGb));
  } else {
    snprintf(sdText, sizeof(sdText), "SD no card");
  }
  display.setCursor(4, 34);
  display.print(sdText);

  const EnvironmentSnapshot *env = data.environment;
  char ambientText[12];
  char humidityText[12];
  char chipText[14];
  if (env != nullptr && env->ambientTemperatureValid) {
    snprintf(ambientText, sizeof(ambientText), "%.1fC", env->ambientTemperatureC);
  } else {
    snprintf(ambientText, sizeof(ambientText), "--C");
  }
  if (env != nullptr && env->ambientHumidityValid) {
    snprintf(humidityText, sizeof(humidityText), "%.1f%%",
             env->ambientHumidityPct);
  } else {
    snprintf(humidityText, sizeof(humidityText), "--%%");
  }
  if (env != nullptr && env->chipTemperatureValid) {
    snprintf(chipText, sizeof(chipText), "C %.1fC", env->chipTemperatureC);
  } else {
    snprintf(chipText, sizeof(chipText), "C --C");
  }
  drawRightAlignedText(display, ambientText, 22);
  drawRightAlignedText(display, humidityText, 34);
  drawRightAlignedText(display, chipText, 46);

  drawCenteredText(display, "SUN: contact", 182);
  drawCenteredText(display, "PWR: apps", 192);
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

  for (uint8_t slot = 0; slot < MENU_PAGE_SIZE; slot++) {
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
    drawTextStateButton(display, POWER_MIDDLE_BUTTON, "BT",
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
  drawCenteredIcon(display, 'S', 114, 2);
}

void drawDeepSleepClockScreen(AppDisplay &display, const char *timeText,
                              const char *dateText) {
  display.fillScreen(0);
  drawCenteredIcon(display, 'S', 42, 1);

  display.setFont();
  display.setTextColor(1);
  display.setTextSize(4);
  drawCenteredText(display, timeText, 104);

  display.setTextSize(1);
  drawCenteredText(display, dateText, 140);
  display.setTextSize(1);
}
