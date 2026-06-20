#ifndef BUILTIN_APPS_H
#define BUILTIN_APPS_H

#include "sys/app_runtime.h"
#include <stddef.h>

struct AppCatalogEntry {
  AppDefinition definition;
  char id[24];
  char label[16];
  char icon[2];
};

ActiveApp *contactLinksRuntime();
const AppDefinition *contactLinksDefinition();
size_t appCatalogCount(MenuCategory category);
bool appCatalogAtVisibleIndex(MenuCategory category, size_t index,
                              AppCatalogEntry &out);
bool findAppById(const char *id, AppCatalogEntry &out);
void refreshAppCatalog();
bool consumeFilesPinkLaunch(char *path, size_t pathSize);
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
