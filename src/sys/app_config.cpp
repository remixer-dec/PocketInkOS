#include "sys/app_config.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint8_t kMaxRegisteredApps = 8;
const AppConfigRegistration *registrations[kMaxRegisteredApps] = {};
uint8_t registrationCount = 0;

void copyValue(char *out, size_t outSize, const char *value) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const char *source = value != nullptr ? value : "";
  strncpy(out, source, outSize - 1);
  out[outSize - 1] = '\0';
}

bool equalIgnoreCase(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    char left = *a;
    char right = *b;
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

bool validValue(const AppConfigSetting &setting, const char *value) {
  if (value == nullptr) {
    return false;
  }
  switch (setting.type) {
  case AppConfigType::Bool:
    return equalIgnoreCase(value, "true") || equalIgnoreCase(value, "false") ||
           strcmp(value, "1") == 0 || strcmp(value, "0") == 0;
  case AppConfigType::Int: {
    char *end = nullptr;
    strtol(value, &end, 10);
    return end != value && end != nullptr && *end == '\0';
  }
  case AppConfigType::Float: {
    char *end = nullptr;
    const float parsed = strtof(value, &end);
    return end != value && end != nullptr && *end == '\0' &&
           std::isfinite(parsed);
  }
  case AppConfigType::String:
    return strlen(value) < setting.inputCapacity;
  case AppConfigType::OneOf:
    for (uint8_t i = 0; i < setting.optionCount; i++) {
      if (strcmp(setting.options[i].value, value) == 0) {
        return true;
      }
    }
    return false;
  }
  return false;
}

} // namespace

void appConfigRegistryReset() {
  registrationCount = 0;
  memset(registrations, 0, sizeof(registrations));
}

bool appConfigRegister(const AppConfigRegistration &registration) {
  if (registration.appId == nullptr || registration.appId[0] == '\0' ||
      registration.settings == nullptr || registration.settingCount == 0) {
    return false;
  }
  for (uint8_t i = 0; i < registrationCount; i++) {
    if (strcmp(registrations[i]->appId, registration.appId) == 0) {
      return true;
    }
  }
  if (registrationCount >= kMaxRegisteredApps) {
    return false;
  }
  registrations[registrationCount++] = &registration;
  return true;
}

uint8_t appConfigRegistrationCount() { return registrationCount; }

const AppConfigRegistration *appConfigRegistrationAt(uint8_t index) {
  return index < registrationCount ? registrations[index] : nullptr;
}

bool appConfigGet(const AppConfigRegistration &registration,
                  const AppConfigSetting &setting, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  char stored[CONFIG_VALUE_MAX] = {};
  if (configGetForApp(registration.appId, setting.key, stored,
                      sizeof(stored)) &&
      validValue(setting, stored)) {
    copyValue(out, outSize, stored);
    return true;
  }
  copyValue(out, outSize, setting.defaultValue);
  return false;
}

bool appConfigGetBool(const AppConfigRegistration &registration,
                      const AppConfigSetting &setting) {
  char value[8] = {};
  appConfigGet(registration, setting, value, sizeof(value));
  return equalIgnoreCase(value, "true") || strcmp(value, "1") == 0;
}

bool appConfigSet(const AppConfigRegistration &registration,
                  const AppConfigSetting &setting, const char *value) {
  return validValue(setting, value) &&
         configSetForApp(registration.appId, setting.key, value);
}
