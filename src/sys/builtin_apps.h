#ifndef BUILTIN_APPS_H
#define BUILTIN_APPS_H

#include "sys/app_runtime.h"
#include <stddef.h>

extern const AppDefinition apps[];
extern const size_t appCount;

ActiveApp *contactLinksRuntime();
void resetApps();
void resetContactLinks();
char wifiStatusIcon();

#endif
