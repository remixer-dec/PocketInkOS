#ifndef BUILTIN_APPS_H
#define BUILTIN_APPS_H

#include "sys/app_runtime.h"
#include <stddef.h>

/// Runtime-facing copy of one launcher entry from the built-in and external app catalogs.
struct AppCatalogEntry {
  /// AppDefinition consumed by the launcher and app runtime.
  AppDefinition definition;
  /// Stable app identifier used for lookup, RTC restore, and app-scoped config.
  char id[24];
  /// Short display label shown in constrained launcher UI.
  char label[16];
  /// Single-glyph icon string rendered by the launcher.
  char icon[2];
};

/// Returns the runtime adapter for the contacts app.
ActiveApp *contactLinksRuntime();
/// Returns the launcher definition for the contacts app.
const AppDefinition *contactLinksDefinition();
/// Counts visible apps in one menu category after visibility filters are applied.
size_t appCatalogCount(MenuCategory category);
/// Copies the visible app at a menu-category index into out.
bool appCatalogAtVisibleIndex(MenuCategory category, size_t index,
                              AppCatalogEntry &out);
/// Looks up an app by stable id across built-in and external app catalogs.
bool findAppById(const char *id, AppCatalogEntry &out);
/// Rebuilds cached catalog state after config, storage, or external apps change.
void refreshAppCatalog();
/// Consumes a pending file-manager request to launch a .pink executable.
bool consumeFilesPinkLaunch(char *path, size_t pathSize);
/// Resets all app-owned runtime state.
void resetApps();
/// Resets only the contacts app runtime state.
void resetContactLinks();

#endif
