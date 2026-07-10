#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sys/system_config.h"

#include <stddef.h>
#include <stdint.h>

/// Storage and editor representation for one app-owned setting.
enum class AppConfigType : uint8_t { Bool, Int, Float, String, OneOf };

/// One labelled value for an AppConfigType::OneOf setting.
struct AppConfigOption {
  const char *label;
  const char *value;
};

/// Read-only metadata that an app registers with the Settings app.
///
/// Definitions should be const so they remain in flash. Values themselves are
/// persisted lazily in /cfg/app-<id>.cfg and are never cached here.
struct AppConfigSetting {
  const char *key;
  const char *label;
  AppConfigType type;
  const char *defaultValue;
  const AppConfigOption *options;
  uint8_t optionCount;
  uint8_t inputCapacity;
};

/// Settings metadata registered by one built-in app.
struct AppConfigRegistration {
  const char *appId;
  const char *label;
  const AppConfigSetting *settings;
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
