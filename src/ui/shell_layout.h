#ifndef SHELL_LAYOUT_H
#define SHELL_LAYOUT_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/battery_monitor.h"
#include "sys/environment_monitor.h"
#include "sys/touch_input.h"
#include "ui/status_bar.h"
#include <stddef.h>

struct ShellData {
  StatusBarSnapshot status;
  const BatterySnapshot *battery;
  const EnvironmentSnapshot *environment;
};

MenuCategory previousMenuCategory(MenuCategory category);
MenuCategory nextMenuCategory(MenuCategory category);
const char *menuCategoryTitle(MenuCategory category);

void clampMenuState(MenuState &state, const AppDefinition *apps,
                    size_t appCount);
void moveMenuPrevious(MenuState &state, const AppDefinition *apps,
                      size_t appCount);
void moveMenuNext(MenuState &state, const AppDefinition *apps,
                  size_t appCount);

void drawHomeScreen(AppDisplay &display, const ShellData &data);
void drawAppMenu(AppDisplay &display, const MenuState &state,
                 const AppDefinition *apps, size_t appCount);
const AppDefinition *hitTestAppMenu(const TouchPoint &point, MenuState &state,
                                    const AppDefinition *apps,
                                    size_t appCount, bool &stateChanged);

void drawQuitDialog(AppDisplay &display);
bool quitDialogHitYes(const TouchPoint &point);
bool quitDialogHitNo(const TouchPoint &point);

#endif
