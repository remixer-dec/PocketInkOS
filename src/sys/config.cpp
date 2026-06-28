#include "sys/config.h"
#include "sys/sd_storage.h"
#include "secrets_config.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

static const char *kConfigDir = "/cfg";
static const char *kSystemConfigPath = "/cfg/system.cfg";
static const char *kDefaultTimeSyncServer = "https://time.now/developer/api/ip";

SystemConfig currentConfig;

typedef void (*ConfigVisitHandler)(const char *key, const char *value,
                                   void *context);

bool asciiEqualIgnoreCase(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    const char ca = static_cast<char>(tolower(static_cast<unsigned char>(*a)));
    const char cb = static_cast<char>(tolower(static_cast<unsigned char>(*b)));
    if (ca != cb) {
      return false;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

void copyValue(char *dest, size_t destSize, const char *source) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  const char *text = source != nullptr ? source : "";
  strncpy(dest, text, destSize - 1);
  dest[destSize - 1] = '\0';
}

char *trim(char *text) {
  if (text == nullptr) {
    return text;
  }
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    text++;
  }
  char *end = text + strlen(text);
  while (end > text && isspace(static_cast<unsigned char>(end[-1]))) {
    *--end = '\0';
  }
  return text;
}

bool validKey(const char *key) {
  if (key == nullptr || key[0] == '\0') {
    return false;
  }
  for (const char *cursor = key; *cursor != '\0'; cursor++) {
    const char c = *cursor;
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' &&
        c != '-') {
      return false;
    }
  }
  return true;
}

bool validValue(const char *value) {
  if (value == nullptr) {
    return false;
  }
  for (const char *cursor = value; *cursor != '\0'; cursor++) {
    if (*cursor == '\n' || *cursor == '\r') {
      return false;
    }
  }
  return true;
}

bool parseBool(const char *value, bool &out) {
  if (asciiEqualIgnoreCase(value, "1") || asciiEqualIgnoreCase(value, "true") ||
      asciiEqualIgnoreCase(value, "yes") || asciiEqualIgnoreCase(value, "on")) {
    out = true;
    return true;
  }
  if (asciiEqualIgnoreCase(value, "0") || asciiEqualIgnoreCase(value, "false") ||
      asciiEqualIgnoreCase(value, "no") || asciiEqualIgnoreCase(value, "off")) {
    out = false;
    return true;
  }
  return false;
}

uint16_t cpuMhzFromValue(const char *value, uint16_t fallback) {
  const unsigned long parsed =
      strtoul(value != nullptr ? value : "", nullptr, 10);
  switch (parsed) {
  case 80:
  case 160:
  case 240:
    return static_cast<uint16_t>(parsed);
  default:
    return fallback;
  }
}

void resetSystemDefaults() {
  currentConfig = SystemConfig{};
  copyValue(currentConfig.wifi.ssid, sizeof(currentConfig.wifi.ssid),
            SECRET_WIFI_SSID);
  copyValue(currentConfig.wifi.password, sizeof(currentConfig.wifi.password),
            SECRET_WIFI_PASSWORD);
  copyValue(currentConfig.wifi.displayName,
            sizeof(currentConfig.wifi.displayName), SECRET_WIFI_DISPLAY_NAME);
  currentConfig.wifi.autoConnect = false;
  copyValue(currentConfig.home.sunBtnApp, sizeof(currentConfig.home.sunBtnApp),
            "contact_links");
  currentConfig.cpu.mhz = 240;
  currentConfig.cpu.governor = CONFIG_CPU_FIXED;
  currentConfig.time.timeFormat = CONFIG_TIME_24H;
  currentConfig.time.dateFormat = CONFIG_DATE_ISO;
  currentConfig.time.syncProtocol = CONFIG_TIME_SYNC_HTTPS;
  copyValue(currentConfig.time.timezone, sizeof(currentConfig.time.timezone),
            "auto");
  copyValue(currentConfig.time.syncServer, sizeof(currentConfig.time.syncServer),
            kDefaultTimeSyncServer);
  currentConfig.sensors.temperatureOffsetC = 0.0f;
}

void applySystemKey(const char *key, const char *value) {
  if (strcmp(key, "wifi.ssid") == 0) {
    copyValue(currentConfig.wifi.ssid, sizeof(currentConfig.wifi.ssid), value);
  } else if (strcmp(key, "wifi.password") == 0) {
    copyValue(currentConfig.wifi.password, sizeof(currentConfig.wifi.password),
              value);
  } else if (strcmp(key, "wifi.displayName") == 0) {
    copyValue(currentConfig.wifi.displayName,
              sizeof(currentConfig.wifi.displayName), value);
  } else if (strcmp(key, "wifi.autoConnect") == 0) {
    parseBool(value, currentConfig.wifi.autoConnect);
  } else if (strcmp(key, "home.sunBtnApp") == 0) {
    copyValue(currentConfig.home.sunBtnApp,
              sizeof(currentConfig.home.sunBtnApp), value);
  } else if (strcmp(key, "cpu.mhz") == 0) {
    currentConfig.cpu.mhz = cpuMhzFromValue(value, currentConfig.cpu.mhz);
  } else if (strcmp(key, "cpu.governor") == 0) {
    currentConfig.cpu.governor = asciiEqualIgnoreCase(value, "balanced")
                                     ? CONFIG_CPU_BALANCED
                                     : CONFIG_CPU_FIXED;
  } else if (strcmp(key, "time.format") == 0) {
    currentConfig.time.timeFormat =
        asciiEqualIgnoreCase(value, "12h") ? CONFIG_TIME_12H : CONFIG_TIME_24H;
  } else if (strcmp(key, "date.format") == 0) {
    if (asciiEqualIgnoreCase(value, "mdy")) {
      currentConfig.time.dateFormat = CONFIG_DATE_MDY;
    } else if (asciiEqualIgnoreCase(value, "dmy")) {
      currentConfig.time.dateFormat = CONFIG_DATE_DMY;
    } else if (asciiEqualIgnoreCase(value, "dmy-dot")) {
      currentConfig.time.dateFormat = CONFIG_DATE_DMY_DOT;
    } else if (asciiEqualIgnoreCase(value, "weekday") ||
               asciiEqualIgnoreCase(value, "readable")) {
      currentConfig.time.dateFormat = CONFIG_DATE_WEEKDAY;
    } else {
      currentConfig.time.dateFormat = CONFIG_DATE_ISO;
    }
  } else if (strcmp(key, "time.syncProtocol") == 0) {
    currentConfig.time.syncProtocol = asciiEqualIgnoreCase(value, "ntp")
                                          ? CONFIG_TIME_SYNC_NTP
                                          : CONFIG_TIME_SYNC_HTTPS;
  } else if (strcmp(key, "timezone") == 0) {
    copyValue(currentConfig.time.timezone, sizeof(currentConfig.time.timezone),
              value);
  } else if (strcmp(key, "time.syncServer") == 0) {
    copyValue(currentConfig.time.syncServer,
              sizeof(currentConfig.time.syncServer), value);
  } else if (strcmp(key, "sensor.tempOffsetC") == 0) {
    currentConfig.sensors.temperatureOffsetC =
        static_cast<float>(atof(value != nullptr ? value : "0"));
  }
}

void visitConfigFile(const char *path, ConfigVisitHandler handler,
                     void *context) {
  if (path == nullptr || handler == nullptr || !sdStorageMounted()) {
    return;
  }

  File file = SD_MMC.open(path);
  if (!file) {
    return;
  }

  char line[160];
  size_t length = 0;
  while (file.available() > 0) {
    const int raw = file.read();
    if (raw < 0) {
      break;
    }
    const char c = static_cast<char>(raw);
    if (c == '\n' || c == '\r') {
      line[length] = '\0';
      length = 0;
    } else if (length + 1 < sizeof(line)) {
      line[length++] = c;
      continue;
    } else {
      length = 0;
      continue;
    }

    char *text = trim(line);
    if (text[0] == '\0' || text[0] == '#' || text[0] == ';') {
      continue;
    }
    char *equals = strchr(text, '=');
    if (equals == nullptr) {
      continue;
    }
    *equals = '\0';
    char *key = trim(text);
    char *value = trim(equals + 1);
    if (validKey(key)) {
      handler(key, value, context);
    }
  }

  if (length > 0) {
    line[length] = '\0';
    char *text = trim(line);
    char *equals = strchr(text, '=');
    if (equals != nullptr) {
      *equals = '\0';
      char *key = trim(text);
      char *value = trim(equals + 1);
      if (validKey(key)) {
        handler(key, value, context);
      }
    }
  }
}

void applySystemVisitor(const char *key, const char *value, void *) {
  applySystemKey(key, value);
}

struct LookupContext {
  const char *key;
  char *out;
  size_t outSize;
  bool found;
};

void lookupVisitor(const char *key, const char *value, void *context) {
  LookupContext *lookup = static_cast<LookupContext *>(context);
  if (lookup == nullptr || strcmp(key, lookup->key) != 0) {
    return;
  }
  copyValue(lookup->out, lookup->outSize, value);
  lookup->found = true;
}

bool systemDefaultValue(const char *key, char *out, size_t outSize) {
  if (key == nullptr || out == nullptr || outSize == 0) {
    return false;
  }

  SystemConfig defaults;
  SystemConfig saved = currentConfig;
  resetSystemDefaults();
  defaults = currentConfig;
  currentConfig = saved;

  if (strcmp(key, "wifi.ssid") == 0) {
    copyValue(out, outSize, defaults.wifi.ssid);
  } else if (strcmp(key, "wifi.password") == 0) {
    copyValue(out, outSize, defaults.wifi.password);
  } else if (strcmp(key, "wifi.displayName") == 0) {
    copyValue(out, outSize, defaults.wifi.displayName);
  } else if (strcmp(key, "wifi.autoConnect") == 0) {
    copyValue(out, outSize, defaults.wifi.autoConnect ? "true" : "false");
  } else if (strcmp(key, "home.sunBtnApp") == 0) {
    copyValue(out, outSize, defaults.home.sunBtnApp);
  } else if (strcmp(key, "cpu.mhz") == 0) {
    snprintf(out, outSize, "%u", static_cast<unsigned>(defaults.cpu.mhz));
  } else if (strcmp(key, "cpu.governor") == 0) {
    copyValue(out, outSize, "fixed");
  } else if (strcmp(key, "time.format") == 0) {
    copyValue(out, outSize, "24h");
  } else if (strcmp(key, "date.format") == 0) {
    copyValue(out, outSize, "iso");
  } else if (strcmp(key, "time.syncProtocol") == 0) {
    copyValue(out, outSize, "https");
  } else if (strcmp(key, "timezone") == 0) {
    copyValue(out, outSize, defaults.time.timezone);
  } else if (strcmp(key, "time.syncServer") == 0) {
    copyValue(out, outSize, defaults.time.syncServer);
  } else if (strcmp(key, "sensor.tempOffsetC") == 0) {
    copyValue(out, outSize, "0");
  } else {
    return false;
  }
  return true;
}

bool appendKeyValue(const char *path, const char *key, const char *value) {
  if (!sdStorageMounted() || !validKey(key) || !validValue(value)) {
    return false;
  }

  SD_MMC.mkdir(kConfigDir);
  File file = SD_MMC.open(path, FILE_APPEND);
  if (!file) {
    return false;
  }

  const size_t keyLen = strlen(key);
  const size_t valueLen = strlen(value);
  return file.write(reinterpret_cast<const uint8_t *>(key), keyLen) == keyLen &&
         file.write(reinterpret_cast<const uint8_t *>("="), 1) == 1 &&
         file.write(reinterpret_cast<const uint8_t *>(value), valueLen) ==
             valueLen &&
         file.write(reinterpret_cast<const uint8_t *>("\n"), 1) == 1;
}

bool appConfigPath(const char *appId, char *out, size_t outSize) {
  if (appId == nullptr || appId[0] == '\0' || out == nullptr || outSize == 0) {
    return false;
  }
  char safe[40];
  size_t pos = 0;
  for (const char *cursor = appId; *cursor != '\0' && pos + 1 < sizeof(safe);
       cursor++) {
    const char c = *cursor;
    safe[pos++] = (isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                   c == '_')
                      ? c
                      : '_';
  }
  safe[pos] = '\0';
  const int written = snprintf(out, outSize, "/cfg/app-%s.cfg", safe);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

} // namespace

void configBegin() { configReload(); }

void configReload() {
  resetSystemDefaults();
  visitConfigFile(kSystemConfigPath, applySystemVisitor, nullptr);
}

const SystemConfig &systemConfig() { return currentConfig; }

bool configGet(const char *key, char *out, size_t outSize) {
  if (!validKey(key) || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  LookupContext lookup = {key, out, outSize, false};
  visitConfigFile(kSystemConfigPath, lookupVisitor, &lookup);
  return lookup.found || systemDefaultValue(key, out, outSize);
}

bool configSet(const char *key, const char *value) {
  const bool ok = appendKeyValue(kSystemConfigPath, key, value);
  if (ok) {
    configReload();
  }
  return ok;
}

bool configGetForApp(const char *appId, const char *key, char *out,
                     size_t outSize) {
  if (!validKey(key) || out == nullptr || outSize == 0) {
    return false;
  }
  char path[64];
  if (!appConfigPath(appId, path, sizeof(path))) {
    return false;
  }
  out[0] = '\0';
  LookupContext lookup = {key, out, outSize, false};
  visitConfigFile(path, lookupVisitor, &lookup);
  return lookup.found;
}

bool configSetForApp(const char *appId, const char *key, const char *value) {
  char path[64];
  if (!appConfigPath(appId, path, sizeof(path))) {
    return false;
  }
  return appendKeyValue(path, key, value);
}
