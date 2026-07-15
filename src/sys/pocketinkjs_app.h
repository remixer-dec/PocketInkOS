/// Adapts a JavaScript file on SD storage to the firmware's ActiveApp lifecycle.
/// Launch creates a fresh PocketInkJS VM and metadata; stop tears down all VM and
/// source memory so relaunching a file provides a clean hot-reload boundary.
#ifndef POCKETINKJS_APP_H
#define POCKETINKJS_APP_H

#include "sys/app_runtime.h"
#include <stddef.h>

/// Returns the ActiveApp adapter used for a JavaScript file launched by Files.
ActiveApp *PocketInkJSAppRuntime();

/// Loads and evaluates the current contents of a .js file from SD storage.
/// Re-launching the path is the hot-reload boundary: no old VM state survives.
bool PocketInkJSAppLaunchPath(const char *path, AppDefinition *out, char *id,
                         size_t idSize, char *label, size_t labelSize,
                         char *icon, size_t iconSize);

/// Validated launch variant: loads, evaluates, and runs the initial lifecycle.
/// It returns false and releases partial VM state on any initial error.
bool PocketInkJSAppLaunchTestPath(const char *path, AppDefinition *out,
                                  char *id, size_t idSize, char *label,
                                  size_t labelSize, char *icon,
                                  size_t iconSize, char *error,
                                  size_t errorSize);

/// Stops callbacks and releases all source/runtime PSRAM owned by PocketInkJS.
void PocketInkJSAppStop();

#endif
