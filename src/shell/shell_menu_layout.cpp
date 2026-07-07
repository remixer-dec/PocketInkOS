#include "shell/shell_menu_layout.h"
#include "sys/global.h"
#include "ui/components/touch_button.h"
#include "ui/icon_ascii_font.h"
#include <stdio.h>

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
  return tryGetVisibleAppAtIndex(state.category, targetIndex, out);
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

  UiRect iconRect = {x, y, MENU_ICON_W, MENU_ICON_H};
  TouchButton(iconRect).draw(display, selected);
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

  UiRect iconRect = {x, y, MENU_ICON_W, MENU_ICON_H};
  TouchButton(iconRect).draw(display, selected);
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
