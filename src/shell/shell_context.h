#ifndef SHELL_CONTEXT_H
#define SHELL_CONTEXT_H

#include "sys/app_runtime.h"

struct ShellContextRestoreActions {
  void *context = nullptr;
  void (*launchApp)(void *context, const AppDefinition &app) = nullptr;
  void (*switchTo)(void *context, Screen screen) = nullptr;
  const AppDefinition *(*activeDefinition)(void *context) = nullptr;
};

Screen shellContextScreenForSave(Screen screen, bool textInputScreen,
                                 const AppDefinition *activeDefinition);
void shellContextSaveForSleep(Screen screen, bool textInputScreen,
                              const AppDefinition *activeDefinition,
                              const MenuState &menuState);
bool shellContextRestoreAfterSleep(MenuState &menuState,
                                   const ShellContextRestoreActions &actions);

#endif
