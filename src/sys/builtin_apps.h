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

/// Counts visible apps in one menu category after visibility filters are applied.
size_t appCatalogCount(MenuCategory category);
/// Looks up the app shown in a launcher slot and writes it into `out`; returns false if no app exists there.
bool tryGetVisibleAppAtIndex(MenuCategory category, size_t index,
                             AppCatalogEntry &out);
/// Looks up an app by stable id across built-in and external app catalogs, even
/// when the app is hidden from the launcher because it is the quick-access app.
bool findAppById(const char *id, AppCatalogEntry &out);
/// Rebuilds cached catalog state after config, storage, or external apps change.
void refreshAppCatalog();
/// Takes the pending .pink executable launch path from the files app.
bool takePendingPinkExecLaunchPath(char *path, size_t pathSize);
/// Resets all app-owned runtime state.
void resetApps();

#endif
