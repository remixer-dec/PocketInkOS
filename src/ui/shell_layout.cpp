#include "ui/shell_layout.h"
#include "sys/global.h"
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

size_t countCategoryApps(const AppDefinition *apps, size_t appCount,
                         MenuCategory category) {
  size_t count = 0;
  for (size_t i = 0; i < appCount; i++) {
    if (apps[i].category == category) {
      count++;
    }
  }
  return count;
}

uint8_t pageCountForCategory(const AppDefinition *apps, size_t appCount,
                             MenuCategory category) {
  const size_t count = countCategoryApps(apps, appCount, category);
  if (count == 0) {
    return 1;
  }
  return static_cast<uint8_t>((count + MENU_PAGE_SIZE - 1) / MENU_PAGE_SIZE);
}

const AppDefinition *appAtVisibleSlot(const AppDefinition *apps,
                                      size_t appCount,
                                      const MenuState &state, uint8_t slot) {
  const size_t targetIndex =
      static_cast<size_t>(state.page) * MENU_PAGE_SIZE + slot;
  size_t visibleIndex = 0;
  for (size_t i = 0; i < appCount; i++) {
    if (apps[i].category != state.category) {
      continue;
    }
    if (visibleIndex == targetIndex) {
      return &apps[i];
    }
    visibleIndex++;
  }
  return nullptr;
}

void drawMenuTitle(AppDisplay &display, const MenuState &state,
                   const AppDefinition *apps, size_t appCount) {
  char title[24];
  const uint8_t pages = pageCountForCategory(apps, appCount, state.category);
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

void drawMenuItem(AppDisplay &display, const AppDefinition &app, uint8_t slot) {
  const uint8_t row = slot / MENU_COLUMNS;
  const uint8_t col = slot % MENU_COLUMNS;
  const int16_t x = MENU_X + col * MENU_CELL_W;
  const int16_t y = MENU_Y + row * MENU_CELL_H;

  display.drawRect(x, y, MENU_ICON_W, MENU_ICON_H, 1);
  display.setTextSize(2);
  display.setCursor(x + 14, y + 8);
  display.print(app.icon);

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

void clampMenuState(MenuState &state, const AppDefinition *apps,
                    size_t appCount) {
  const uint8_t pages = pageCountForCategory(apps, appCount, state.category);
  if (state.page >= pages) {
    state.page = pages - 1;
  }
}

void moveMenuPrevious(MenuState &state, const AppDefinition *apps,
                      size_t appCount) {
  clampMenuState(state, apps, appCount);
  if (state.page > 0) {
    state.page--;
    return;
  }
  state.category = previousMenuCategory(state.category);
  state.page = pageCountForCategory(apps, appCount, state.category) - 1;
}

void moveMenuNext(MenuState &state, const AppDefinition *apps,
                  size_t appCount) {
  clampMenuState(state, apps, appCount);
  const uint8_t pages = pageCountForCategory(apps, appCount, state.category);
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

  drawCenteredText(display, "SUN: apps", 182);
  drawCenteredText(display, "PWR: contact", 192);
}

void drawAppMenu(AppDisplay &display, const MenuState &state,
                 const AppDefinition *apps, size_t appCount) {
  display.setTextColor(1);
  display.setTextSize(2);
  display.setCursor(8, 10);
  display.print("<");
  display.setCursor(184, 10);
  display.print(">");
  drawMenuTitle(display, state, apps, appCount);

  for (uint8_t slot = 0; slot < MENU_PAGE_SIZE; slot++) {
    const AppDefinition *app = appAtVisibleSlot(apps, appCount, state, slot);
    if (app != nullptr) {
      drawMenuItem(display, *app, slot);
    }
  }

  display.setTextSize(1);
}

const AppDefinition *hitTestAppMenu(const TouchPoint &point, MenuState &state,
                                    const AppDefinition *apps,
                                    size_t appCount, bool &stateChanged) {
  stateChanged = false;
  if (point.y < MENU_HEADER_H) {
    if (point.x < 48) {
      moveMenuPrevious(state, apps, appCount);
      stateChanged = true;
    } else if (point.x > 152) {
      moveMenuNext(state, apps, appCount);
      stateChanged = true;
    }
    return nullptr;
  }

  if (point.x < MENU_X || point.x >= EPD_WIDTH || point.y < MENU_Y ||
      point.y >= MENU_Y + MENU_ROWS * MENU_CELL_H) {
    return nullptr;
  }

  const uint8_t col = (point.x - MENU_X) / MENU_CELL_W;
  const uint8_t row = (point.y - MENU_Y) / MENU_CELL_H;
  if (col >= MENU_COLUMNS || row >= MENU_ROWS) {
    return nullptr;
  }
  return appAtVisibleSlot(apps, appCount, state, row * MENU_COLUMNS + col);
}

void drawQuitDialog(AppDisplay &display) {
  display.fillRect(24, 62, 152, 86, 0);
  display.drawRect(24, 62, 152, 86, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  display.setCursor(38, 78);
  display.print("Do you really");
  display.setCursor(50, 92);
  display.print("want to quit?");
  uiDrawButton(display, QUIT_YES_BUTTON, "YES");
  uiDrawButton(display, QUIT_NO_BUTTON, "NO");
}

bool quitDialogHitYes(const TouchPoint &point) {
  return uiContains(QUIT_YES_BUTTON, point);
}

bool quitDialogHitNo(const TouchPoint &point) {
  return uiContains(QUIT_NO_BUTTON, point);
}
