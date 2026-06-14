#ifndef BUILTIN_APPS_H
#define BUILTIN_APPS_H

#include "sys/app_runtime.h"
#include <stddef.h>

extern const AppDefinition apps[];
extern const size_t appCount;

ActiveApp *contactLinksRuntime();
const AppDefinition *contactLinksDefinition();
const AppDefinition *findAppById(const char *id);
void resetApps();
void resetContactLinks();
char wifiStatusIcon();
bool wifiIsOn();
bool wifiIsConnected();
void wifiTurnOn();
void wifiTurnOff();
void wifiToggle();
bool wifiUpdate();
void restoreWifiOn(bool enabled);

#endif
