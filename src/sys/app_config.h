/// Declares the schema registry used by built-in apps to expose their settings
/// in the system Settings app. Schemas stay in flash; values are validated and
/// read from or written to app-scoped configuration files on demand.
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sys/system_config.h"

#include <stddef.h>
#include <stdint.h>

/// Storage and editor representation for one app-owned setting.
enum class AppConfigType : uint8_t { Bool, Int, Float, String, OneOf };

/// One labelled value for an AppConfigType::OneOf setting.
struct AppConfigOption {
  /// Human-readable choice shown by Settings.
  const char *label;
  /// Canonical string persisted when this choice is selected.
  const char *value;
};

/// Read-only metadata that an app registers with the Settings app.
///
/// Definitions should be const so they remain in flash. Values themselves are
/// persisted lazily in /cfg/app-<id>.cfg and are never cached here.
struct AppConfigSetting {
  /// App-local key used in persistent configuration.
  const char *key;
  /// Human-readable setting name shown by Settings.
  const char *label;
  /// Validation and editor behavior for the stored string value.
  AppConfigType type;
  /// Value used when storage is absent or invalid.
  const char *defaultValue;
  /// Choice table used only by OneOf settings.
  const AppConfigOption *options;
  /// Number of entries in options.
  uint8_t optionCount;
  /// Maximum editor buffer size for free-form string values.
  uint8_t inputCapacity;
};

/// Settings metadata registered by one built-in app.
struct AppConfigRegistration {
  /// Stable app identifier used to scope its config file.
  const char *appId;
  /// App name shown as the Settings section label.
  const char *label;
  /// Flash-resident array of setting schemas.
  const AppConfigSetting *settings;
  /// Number of entries in settings.
  uint8_t settingCount;
};

/// Clears registrations before built-in apps register their static schemas.
void appConfigRegistryReset();
/// Adds one static schema. Duplicate registrations are ignored.
bool appConfigRegister(const AppConfigRegistration &registration);
/// Returns the number of registered app schemas.
uint8_t appConfigRegistrationCount();
/// Returns a registered schema by index, or nullptr when out of range.
const AppConfigRegistration *appConfigRegistrationAt(uint8_t index);

/// Reads an app value, falling back to the registered default when absent or
/// invalid. No per-app values are retained in RAM.
bool appConfigGet(const AppConfigRegistration &registration,
                  const AppConfigSetting &setting, char *out, size_t outSize);
/// Reads a registered boolean as a native value, including its default.
bool appConfigGetBool(const AppConfigRegistration &registration,
                      const AppConfigSetting &setting);
/// Validates and persists an app value through the existing app-scoped config.
bool appConfigSet(const AppConfigRegistration &registration,
                  const AppConfigSetting &setting, const char *value);

#endif
