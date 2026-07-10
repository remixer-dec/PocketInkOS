/// Public catalog API for built-in and external launcher apps.
///
/// Built-in app definitions are assembled in `builtin_apps.cpp`. Each entry
/// sets its launcher metadata directly, including optional icon-font rendering
/// and visibility conditions. The behavior builder below keeps callback
/// composition consistent without growing one-off behavior factories.
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

/// Converts bool-returning app handlers into AppEventResult values for catalog behavior callbacks.
inline AppEventResult dirtyIfHandled(bool handled) {
  return handled ? AppEventResult::Dirty : AppEventResult::Unhandled;
}

/// Saves app-owned context through a concrete app instance.
template <typename T, T &App>
size_t saveAppContext(uint8_t *buffer, size_t capacity) {
  return App.saveContext(buffer, capacity);
}

/// Restores app-owned context through a concrete app instance.
template <typename T, T &App>
void restoreAppContext(const uint8_t *buffer, size_t length) {
  App.restoreContext(buffer, length);
}

/// Adapts a bool-returning app method to an AppEventResult callback.
template <typename T, T &App, bool (T::*Handler)()>
AppEventResult dirtyIfHandledMethod() {
  return dirtyIfHandled((App.*Handler)());
}

/// Fluent builder for AppBehavior values used by the built-in app catalog.
struct BehaviorBuilder {
  AppBehavior behavior = {};

  /// Sets the active-app enter callback.
  BehaviorBuilder withEnter(AppCallback handler) const {
    BehaviorBuilder next = *this;
    next.behavior.onEnter = handler;
    return next;
  }

  /// Sets the active-app exit callback.
  BehaviorBuilder withExit(AppCallback handler) const {
    BehaviorBuilder next = *this;
    next.behavior.onExit = handler;
    return next;
  }

  /// Sets menu-button callbacks for single, double, and long presses.
  BehaviorBuilder withMenuBtn(AppEventHandler menu,
                              AppEventHandler menuDouble = nullptr,
                              AppEventHandler menuLong = nullptr) const {
    BehaviorBuilder next = *this;
    next.behavior.onMenu = menu;
    next.behavior.onMenuDouble = menuDouble;
    next.behavior.onMenuLong = menuLong;
    return next;
  }

  /// Sets the power-button callback.
  BehaviorBuilder withPowerBtn(AppEventHandler handler) const {
    BehaviorBuilder next = *this;
    next.behavior.onPower = handler;
    return next;
  }

  /// Sets the raw touch callback.
  BehaviorBuilder withRawTouch(AppRawTouchHandler handler) const {
    BehaviorBuilder next = *this;
    next.behavior.onRawTouch = handler;
    return next;
  }

  /// Sets app context save and restore callbacks.
  BehaviorBuilder withContext(AppSaveContextHandler save,
                              AppRestoreContextHandler restore) const {
    BehaviorBuilder next = *this;
    next.behavior.onSaveContext = save;
    next.behavior.onRestoreContext = restore;
    return next;
  }

  /// Sets the release callback used when app-owned resources should be dropped.
  BehaviorBuilder withRelease(AppCallback handler) const {
    BehaviorBuilder next = *this;
    next.behavior.onRelease = handler;
    return next;
  }

  /// Returns the built behavior or an explicit override when one is supplied.
  AppBehavior build(const AppBehavior *behaviorOverride = nullptr) const {
    return behaviorOverride != nullptr ? *behaviorOverride : behavior;
  }
};

/// Starts behavior composition with the runtime defaults.
inline BehaviorBuilder defaultBehavior() { return BehaviorBuilder(); }

/// Starts behavior composition with save and restore callbacks for one app instance.
template <typename T, T &App>
BehaviorBuilder defaultContextBehavior() {
  return defaultBehavior().withContext(saveAppContext<T, App>,
                                       restoreAppContext<T, App>);
}

/// Counts visible apps in one menu category after visibility filters are applied.
size_t appCatalogCount(MenuCategory category);
/// Looks up the app shown in a launcher slot and writes it into `out`; returns false if no app exists there.
bool tryGetVisibleAppAtIndex(MenuCategory category, size_t index,
                             AppCatalogEntry &out);
/// Counts every installed app, including the SUN-button app hidden from the launcher.
size_t appCatalogAllCount();
/// Looks up an installed app by stable catalog order without visibility filtering.
bool tryGetAppAtIndex(size_t index, AppCatalogEntry &out);
/// Looks up an app by stable id across built-in and external app catalogs, even
/// when the app is hidden from the launcher because it is the quick-access app.
bool findAppById(const char *id, AppCatalogEntry &out);
/// Rebuilds cached catalog state after config, storage, or external apps change.
void refreshAppCatalog();
/// Takes the pending .pink executable launch path from the files app.
bool takePendingPinkExecLaunchPath(char *path, size_t pathSize);
/// Takes the pending PinkJS source launch path from the files app.
bool takePendingPinkJsLaunchPath(char *path, size_t pathSize);
/// Resets all app-owned runtime state.
void resetApps();

#endif
