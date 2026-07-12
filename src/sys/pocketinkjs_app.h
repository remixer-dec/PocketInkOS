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

/// Stops callbacks and releases all source/runtime PSRAM owned by PocketInkJS.
void PocketInkJSAppStop();

#endif
