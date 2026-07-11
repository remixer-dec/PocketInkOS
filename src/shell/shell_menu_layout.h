#ifndef SHELL_MENU_LAYOUT_H
#define SHELL_MENU_LAYOUT_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/builtin_apps.h"
#include "sys/drivers/touch_input.h"
#include <stdint.h>

/// Walks categories backward for button navigation.
MenuCategory previousMenuCategory(MenuCategory category);
/// Walks categories forward for button navigation.
MenuCategory nextMenuCategory(MenuCategory category);
/// Provides the short category label used by the app menu.
const char *menuCategoryTitle(MenuCategory category);

/// Keeps menu state valid after app catalog or category contents change.
void clampMenuState(MenuState &state);
/// Applies one backward menu step with category-aware wrapping.
void moveMenuPrevious(MenuState &state);
/// Applies one forward menu step with category-aware wrapping.
void moveMenuNext(MenuState &state);

/// Renders the category app menu and optional pressed-slot feedback.
void drawAppMenu(AppDisplay &display, const MenuState &state,
                 int8_t pressedSlot = -1);
/// Converts a touch into menu movement or an app selection.
bool hitTestAppMenu(const TouchPoint &point, MenuState &state,
                    AppCatalogEntry &selected, bool &stateChanged,
                    int8_t *hitSlot = nullptr);

#endif
