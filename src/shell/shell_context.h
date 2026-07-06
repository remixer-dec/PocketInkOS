#ifndef SHELL_CONTEXT_H
#define SHELL_CONTEXT_H

#include "sys/app_runtime.h"

/// Restore hooks that let retained context code resume shell state safely.
///
/// The context module persists only small identifiers. ShellController supplies
/// these callbacks so restore can relaunch apps or switch screens without owning
/// the full controller.
struct ShellContextRestoreActions {
  void *context = nullptr;
  void (*launchApp)(void *context, const AppDefinition &app) = nullptr;
  void (*switchTo)(void *context, Screen screen) = nullptr;
  const AppDefinition *(*activeDefinition)(void *context) = nullptr;
};

/// Collapses transient screens into the screen value that should survive sleep.
Screen shellContextScreenForSave(Screen screen, bool textInputScreen,
                                 const AppDefinition *activeDefinition);
/// Persists the current shell/app/menu position into RTC-retained state.
void shellContextSaveForSleep(Screen screen, bool textInputScreen,
                              const AppDefinition *activeDefinition,
                              const MenuState &menuState);
/// Replays retained shell state after wake; returns true when a restore happened.
bool shellContextRestoreAfterSleep(MenuState &menuState,
                                   const ShellContextRestoreActions &actions);

#endif
