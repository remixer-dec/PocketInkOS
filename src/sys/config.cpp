#include "sys/system_config.h"
#include "sys/services/sd_storage.h"
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

bool wifiSavedKey(const char *key, uint8_t &index, const char *&field) {
  static const char *prefix = "wifi.saved.";
  const size_t prefixLen = strlen(prefix);
  if (strncmp(key, prefix, prefixLen) != 0) {
    return false;
  }
  const char *cursor = key + prefixLen;
  if (cursor[0] < '0' || cursor[0] > '2' || cursor[1] != '.') {
    return false;
  }
  index = static_cast<uint8_t>(cursor[0] - '0');
  field = cursor + 2;
  return index < CONFIG_WIFI_SAVED_MAX;
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

bool parseInactivityMinutes(const char *value, uint8_t &out) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  uint16_t minutes = 0;
  for (const char *cursor = value; *cursor != '\0'; cursor++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    const uint16_t digit = static_cast<uint16_t>(*cursor - '0');
    if (minutes >
        (static_cast<uint16_t>(CONFIG_LOCKSCREEN_INACTIVITY_MAX_MINUTES) -
         digit) /
            10U) {
      return false;
    }
    minutes = minutes * 10U + digit;
  }
  out = static_cast<uint8_t>(minutes);
  return true;
}

bool parseUnsignedRange(const char *value, uint16_t minimum, uint16_t maximum,
                        uint16_t &out) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  uint32_t parsed = 0;
  for (const char *cursor = value; *cursor != '\0'; cursor++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<uint32_t>(*cursor - '0');
    if (parsed > maximum) {
      return false;
    }
  }
  if (parsed < minimum) {
    return false;
  }
  out = static_cast<uint16_t>(parsed);
  return true;
}

bool parseSyncIntervalHours(const char *value, uint8_t &out) {
  uint16_t hours = 0;
  if (!parseUnsignedRange(value, 0, 24, hours) ||
      (hours != 0 && hours != 1 && hours != 3 && hours != 6 && hours != 12 &&
       hours != 24)) {
    return false;
  }
  out = static_cast<uint8_t>(hours);
  return true;
}

bool parseStoredUtcOffset(const char *value, int32_t &out) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  char *end = nullptr;
  const long parsed = strtol(value, &end, 10);
  if (end == value || end == nullptr || *end != '\0' || parsed < -86399L ||
      parsed > 86399L) {
    return false;
  }
  out = static_cast<int32_t>(parsed);
  return true;
}

bool validSystemValue(const char *key, const char *value) {
  if (key == nullptr || value == nullptr) {
    return false;
  }
  if (strcmp(key, "lockscreen.inactivityMinutes") == 0) {
    uint8_t minutes = 0;
    return parseInactivityMinutes(value, minutes);
  }
  if (strcmp(key, "wifi.autoOffMinutes") == 0) {
    uint8_t minutes = 0;
    return parseInactivityMinutes(value, minutes);
  }
  if (strcmp(key, "time.lastUtcOffsetSeconds") == 0) {
    int32_t offset = 0;
    return parseStoredUtcOffset(value, offset);
  }
  if (strcmp(key, "time.syncIntervalHours") == 0) {
    uint8_t hours = 0;
    return parseSyncIntervalHours(value, hours);
  }
  if (strcmp(key, "periodic.minIntervalMinutes") == 0) {
    uint16_t minutes = 0;
    return parseUnsignedRange(value, 0,
                              CONFIG_PERIODIC_INTERVAL_MAX_MINUTES, minutes);
  }
  if (strcmp(key, "periodic.disableOnLowBattery") == 0) {
    bool enabled = false;
    return parseBool(value, enabled);
  }
  if (strcmp(key, "notifications.mode") == 0) {
    return asciiEqualIgnoreCase(value, "off") ||
           asciiEqualIgnoreCase(value, "silent") ||
           asciiEqualIgnoreCase(value, "on");
  }
  if (strcmp(key, "notifications.ringtone") == 0) {
    return asciiEqualIgnoreCase(value, "chime") ||
           asciiEqualIgnoreCase(value, "droplet") ||
           asciiEqualIgnoreCase(value, "pulse") ||
           asciiEqualIgnoreCase(value, "ascend") ||
           asciiEqualIgnoreCase(value, "knock");
  }
  if (strcmp(key, "cpu.mhz") == 0) {
    uint16_t mhz = 0;
    return parseUnsignedRange(value, 80, 240, mhz) &&
           (mhz == 80 || mhz == 160 || mhz == 240);
  }
  if (strcmp(key, "cpu.governor") == 0) {
    return asciiEqualIgnoreCase(value, "fixed") ||
           asciiEqualIgnoreCase(value, "balanced");
  }
  return true;
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
  currentConfig.wifi.autoOffMinutes = 0;
  copyValue(currentConfig.home.sunBtnApp, sizeof(currentConfig.home.sunBtnApp),
            "contact_links");
  copyValue(currentConfig.home.widgets, sizeof(currentConfig.home.widgets),
            "temp,hum,cpuTemp,sd,battery,logoText,buttonHelp");
  copyValue(currentConfig.layout, sizeof(currentConfig.layout), "default");
  copyValue(currentConfig.audioPlayerLayout,
            sizeof(currentConfig.audioPlayerLayout), "controls");
  copyValue(currentConfig.lockscreen.widgetApp,
            sizeof(currentConfig.lockscreen.widgetApp), "none");
  copyValue(currentConfig.lockscreen.topIcon,
            sizeof(currentConfig.lockscreen.topIcon), "S");
  currentConfig.lockscreen.showDate = true;
  currentConfig.lockscreen.inactivityMinutes = 2;
  currentConfig.notifications.mode = CONFIG_NOTIFICATIONS_SILENT;
  currentConfig.notifications.ringtone = CONFIG_NOTIFICATION_CHIME;
  currentConfig.cpu.mhz = 240;
  currentConfig.cpu.governor = CONFIG_CPU_FIXED;
  currentConfig.time.timeFormat = CONFIG_TIME_24H;
  currentConfig.time.dateFormat = CONFIG_DATE_ISO;
  currentConfig.time.syncProtocol = CONFIG_TIME_SYNC_HTTPS;
  currentConfig.time.syncIntervalHours = 24;
  copyValue(currentConfig.time.timezone, sizeof(currentConfig.time.timezone),
            "auto");
  copyValue(currentConfig.time.syncServer, sizeof(currentConfig.time.syncServer),
            kDefaultTimeSyncServer);
  currentConfig.sensors.temperatureOffsetC = 0.0f;
  copyValue(currentConfig.sensors.unit, sizeof(currentConfig.sensors.unit), "c");
  currentConfig.periodic.minIntervalMinutes = 1;
  currentConfig.periodic.disableOnLowBattery = true;
}

struct StringConfigField {
  const char *key;
  char *value;
  size_t capacity;
};

struct BoolConfigField {
  const char *key;
  bool *value;
};

const StringConfigField *stringConfigField(const char *key) {
  static const StringConfigField fields[] = {
      {"wifi.ssid", currentConfig.wifi.ssid, sizeof(currentConfig.wifi.ssid)},
      {"wifi.password", currentConfig.wifi.password,
       sizeof(currentConfig.wifi.password)},
      {"wifi.displayName", currentConfig.wifi.displayName,
       sizeof(currentConfig.wifi.displayName)},
      {"home.sunBtnApp", currentConfig.home.sunBtnApp,
       sizeof(currentConfig.home.sunBtnApp)},
      {"home.widgets", currentConfig.home.widgets,
       sizeof(currentConfig.home.widgets)},
      {"layout", currentConfig.layout, sizeof(currentConfig.layout)},
      {"interface.audioPlayerLayout", currentConfig.audioPlayerLayout,
       sizeof(currentConfig.audioPlayerLayout)},
      {"lockscreen.widget", currentConfig.lockscreen.widgetApp,
       sizeof(currentConfig.lockscreen.widgetApp)},
      {"lockscreen.topIcon", currentConfig.lockscreen.topIcon,
       sizeof(currentConfig.lockscreen.topIcon)},
      {"timezone", currentConfig.time.timezone,
       sizeof(currentConfig.time.timezone)},
      {"time.syncServer", currentConfig.time.syncServer,
       sizeof(currentConfig.time.syncServer)},
      {"temp.unit", currentConfig.sensors.unit,
       sizeof(currentConfig.sensors.unit)},
  };
  for (const StringConfigField &field : fields) {
    if (strcmp(key, field.key) == 0) {
      return &field;
    }
  }
  return nullptr;
}

const BoolConfigField *boolConfigField(const char *key) {
  static const BoolConfigField fields[] = {
      {"wifi.autoConnect", &currentConfig.wifi.autoConnect},
      {"lockscreen.date", &currentConfig.lockscreen.showDate},
      {"periodic.disableOnLowBattery",
       &currentConfig.periodic.disableOnLowBattery},
  };
  for (const BoolConfigField &field : fields) {
    if (strcmp(key, field.key) == 0) {
      return &field;
    }
  }
  return nullptr;
}

void applySystemKey(const char *key, const char *value) {
  uint8_t savedIndex = 0;
  const char *savedField = nullptr;
  if (wifiSavedKey(key, savedIndex, savedField)) {
    ConfigWifiSavedNetwork &saved = currentConfig.wifi.saved[savedIndex];
    if (strcmp(savedField, "ssid") == 0) {
      copyValue(saved.ssid, sizeof(saved.ssid), value);
    } else if (strcmp(savedField, "password") == 0) {
      copyValue(saved.password, sizeof(saved.password), value);
    } else if (strcmp(savedField, "bssid") == 0) {
      copyValue(saved.bssid, sizeof(saved.bssid), value);
    }
  } else if (const StringConfigField *field = stringConfigField(key)) {
    copyValue(field->value, field->capacity, value);
  } else if (const BoolConfigField *field = boolConfigField(key)) {
    parseBool(value, *field->value);
  } else if (strcmp(key, "lockscreen.inactivityMinutes") == 0) {
    uint8_t minutes = 0;
    if (parseInactivityMinutes(value, minutes)) {
      currentConfig.lockscreen.inactivityMinutes = minutes;
    }
  } else if (strcmp(key, "wifi.autoOffMinutes") == 0) {
    uint8_t minutes = 0;
    if (parseInactivityMinutes(value, minutes)) {
      currentConfig.wifi.autoOffMinutes = minutes;
    }
  } else if (strcmp(key, "cpu.mhz") == 0) {
    currentConfig.cpu.mhz = cpuMhzFromValue(value, currentConfig.cpu.mhz);
  } else if (strcmp(key, "cpu.governor") == 0) {
    currentConfig.cpu.governor = asciiEqualIgnoreCase(value, "balanced")
                                     ? CONFIG_CPU_BALANCED
                                     : CONFIG_CPU_FIXED;
  } else if (strcmp(key, "time.format") == 0) {
    currentConfig.time.timeFormat =
        asciiEqualIgnoreCase(value, "12h") ? CONFIG_TIME_12H : CONFIG_TIME_24H;
  } else if (strcmp(key, "time.lastUtcOffsetSeconds") == 0) {
    int32_t offset = 0;
    if (parseStoredUtcOffset(value, offset)) {
      currentConfig.time.lastUtcOffsetSeconds = offset;
    }
  } else if (strcmp(key, "date.format") == 0) {
    if (asciiEqualIgnoreCase(value, "mdy")) {
      currentConfig.time.dateFormat = CONFIG_DATE_MDY;
    } else if (asciiEqualIgnoreCase(value, "dmy")) {
      currentConfig.time.dateFormat = CONFIG_DATE_DMY;
    } else if (asciiEqualIgnoreCase(value, "dmy-dot")) {
      currentConfig.time.dateFormat = CONFIG_DATE_DMY_DOT;
    } else if (asciiEqualIgnoreCase(value, "weekday")) {
      currentConfig.time.dateFormat = CONFIG_DATE_WEEKDAY;
    } else {
      currentConfig.time.dateFormat = CONFIG_DATE_ISO;
    }
  } else if (strcmp(key, "time.syncProtocol") == 0) {
    currentConfig.time.syncProtocol = asciiEqualIgnoreCase(value, "ntp")
                                          ? CONFIG_TIME_SYNC_NTP
                                          : CONFIG_TIME_SYNC_HTTPS;
  } else if (strcmp(key, "time.syncIntervalHours") == 0) {
    uint8_t hours = 0;
    if (parseSyncIntervalHours(value, hours)) {
      currentConfig.time.syncIntervalHours = hours;
    }
  } else if (strcmp(key, "sensor.tempOffsetC") == 0) {
    currentConfig.sensors.temperatureOffsetC =
        static_cast<float>(atof(value != nullptr ? value : "0"));
  } else if (strcmp(key, "periodic.minIntervalMinutes") == 0) {
    uint16_t minutes = 0;
    if (parseUnsignedRange(value, 0,
                           CONFIG_PERIODIC_INTERVAL_MAX_MINUTES, minutes)) {
      currentConfig.periodic.minIntervalMinutes = minutes;
    }
  } else if (strcmp(key, "notifications.mode") == 0) {
    if (asciiEqualIgnoreCase(value, "off")) {
      currentConfig.notifications.mode = CONFIG_NOTIFICATIONS_OFF;
    } else if (asciiEqualIgnoreCase(value, "on")) {
      currentConfig.notifications.mode = CONFIG_NOTIFICATIONS_ON;
    } else {
      currentConfig.notifications.mode = CONFIG_NOTIFICATIONS_SILENT;
    }
  } else if (strcmp(key, "notifications.ringtone") == 0) {
    if (asciiEqualIgnoreCase(value, "droplet")) {
      currentConfig.notifications.ringtone = CONFIG_NOTIFICATION_DROPLET;
    } else if (asciiEqualIgnoreCase(value, "pulse")) {
      currentConfig.notifications.ringtone = CONFIG_NOTIFICATION_PULSE;
    } else if (asciiEqualIgnoreCase(value, "ascend")) {
      currentConfig.notifications.ringtone = CONFIG_NOTIFICATION_ASCEND;
    } else if (asciiEqualIgnoreCase(value, "knock")) {
      currentConfig.notifications.ringtone = CONFIG_NOTIFICATION_KNOCK;
    } else {
      currentConfig.notifications.ringtone = CONFIG_NOTIFICATION_CHIME;
    }
  }
}

bool visitConfigFile(const char *path, ConfigVisitHandler handler,
                     void *context) {
  if (path == nullptr || handler == nullptr || !sdStorageMounted()) {
    return false;
  }

  File file = SD_MMC.open(path);
  if (!file) {
    return false;
  }

  char line[128];
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
  return true;
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

bool systemCurrentValue(const char *key, char *out, size_t outSize) {
  if (key == nullptr || out == nullptr || outSize == 0) {
    return false;
  }

  uint8_t savedIndex = 0;
  const char *savedField = nullptr;
  if (wifiSavedKey(key, savedIndex, savedField)) {
    const ConfigWifiSavedNetwork &saved = currentConfig.wifi.saved[savedIndex];
    if (strcmp(savedField, "ssid") == 0) {
      copyValue(out, outSize, saved.ssid);
    } else if (strcmp(savedField, "password") == 0) {
      copyValue(out, outSize, saved.password);
    } else if (strcmp(savedField, "bssid") == 0) {
      copyValue(out, outSize, saved.bssid);
    } else {
      return false;
    }
  } else if (const StringConfigField *field = stringConfigField(key)) {
    copyValue(out, outSize, field->value);
  } else if (const BoolConfigField *field = boolConfigField(key)) {
    copyValue(out, outSize, *field->value ? "true" : "false");
  } else if (strcmp(key, "lockscreen.inactivityMinutes") == 0) {
    snprintf(out, outSize, "%u",
             static_cast<unsigned>(currentConfig.lockscreen.inactivityMinutes));
  } else if (strcmp(key, "wifi.autoOffMinutes") == 0) {
    snprintf(out, outSize, "%u",
             static_cast<unsigned>(currentConfig.wifi.autoOffMinutes));
  } else if (strcmp(key, "cpu.mhz") == 0) {
    snprintf(out, outSize, "%u", static_cast<unsigned>(currentConfig.cpu.mhz));
  } else if (strcmp(key, "cpu.governor") == 0) {
    copyValue(out, outSize,
              currentConfig.cpu.governor == CONFIG_CPU_BALANCED ? "balanced"
                                                                : "fixed");
  } else if (strcmp(key, "time.format") == 0) {
    copyValue(out, outSize,
              currentConfig.time.timeFormat == CONFIG_TIME_12H ? "12h" : "24h");
  } else if (strcmp(key, "time.lastUtcOffsetSeconds") == 0) {
    snprintf(out, outSize, "%ld",
             static_cast<long>(currentConfig.time.lastUtcOffsetSeconds));
  } else if (strcmp(key, "date.format") == 0) {
    const char *value = "iso";
    if (currentConfig.time.dateFormat == CONFIG_DATE_MDY) {
      value = "mdy";
    } else if (currentConfig.time.dateFormat == CONFIG_DATE_DMY) {
      value = "dmy";
    } else if (currentConfig.time.dateFormat == CONFIG_DATE_DMY_DOT) {
      value = "dmy-dot";
    } else if (currentConfig.time.dateFormat == CONFIG_DATE_WEEKDAY) {
      value = "weekday";
    }
    copyValue(out, outSize, value);
  } else if (strcmp(key, "time.syncProtocol") == 0) {
    copyValue(out, outSize,
              currentConfig.time.syncProtocol == CONFIG_TIME_SYNC_NTP ? "ntp"
                                                                      : "https");
  } else if (strcmp(key, "time.syncIntervalHours") == 0) {
    snprintf(out, outSize, "%u", static_cast<unsigned>(
                                      currentConfig.time.syncIntervalHours));
  } else if (strcmp(key, "sensor.tempOffsetC") == 0) {
    snprintf(out, outSize, "%.2f", currentConfig.sensors.temperatureOffsetC);
  } else if (strcmp(key, "periodic.minIntervalMinutes") == 0) {
    snprintf(out, outSize, "%u", static_cast<unsigned>(
                                      currentConfig.periodic.minIntervalMinutes));
  } else if (strcmp(key, "notifications.mode") == 0) {
    const char *value = "silent";
    if (currentConfig.notifications.mode == CONFIG_NOTIFICATIONS_OFF) {
      value = "off";
    } else if (currentConfig.notifications.mode == CONFIG_NOTIFICATIONS_ON) {
      value = "on";
    }
    copyValue(out, outSize, value);
  } else if (strcmp(key, "notifications.ringtone") == 0) {
    const char *value = "chime";
    switch (currentConfig.notifications.ringtone) {
    case CONFIG_NOTIFICATION_DROPLET:
      value = "droplet";
      break;
    case CONFIG_NOTIFICATION_PULSE:
      value = "pulse";
      break;
    case CONFIG_NOTIFICATION_ASCEND:
      value = "ascend";
      break;
    case CONFIG_NOTIFICATION_KNOCK:
      value = "knock";
      break;
    case CONFIG_NOTIFICATION_CHIME:
      break;
    }
    copyValue(out, outSize, value);
  } else {
    return false;
  }
  return true;
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

void configLoadDefaults() { resetSystemDefaults(); }

bool configParseLockscreenInactivityMinutes(const char *value, uint8_t &out) {
  return parseInactivityMinutes(value, out);
}

bool configParseUtcOffset(const char *value, int32_t &out) {
  const char *cursor = value;
  if (cursor == nullptr || cursor[0] == '\0' ||
      asciiEqualIgnoreCase(cursor, "auto") ||
      asciiEqualIgnoreCase(cursor, "utc") ||
      asciiEqualIgnoreCase(cursor, "z")) {
    out = 0;
    return true;
  }
  if (strlen(cursor) >= 3 &&
      tolower(static_cast<unsigned char>(cursor[0])) == 'u' &&
      tolower(static_cast<unsigned char>(cursor[1])) == 't' &&
      tolower(static_cast<unsigned char>(cursor[2])) == 'c') {
    cursor += 3;
  }

  const int sign = *cursor == '-' ? -1 : 1;
  if (*cursor != '+' && *cursor != '-') {
    return false;
  }
  cursor++;
  if (!isdigit(static_cast<unsigned char>(*cursor))) {
    return false;
  }

  int hours = *cursor++ - '0';
  if (isdigit(static_cast<unsigned char>(*cursor))) {
    hours = hours * 10 + (*cursor++ - '0');
  }
  int minutes = 0;
  if (*cursor == ':') {
    cursor++;
  }
  if (*cursor != '\0') {
    if (!isdigit(static_cast<unsigned char>(cursor[0])) ||
        !isdigit(static_cast<unsigned char>(cursor[1])) ||
        cursor[2] != '\0') {
      return false;
    }
    minutes = (cursor[0] - '0') * 10 + (cursor[1] - '0');
  }
  if (hours > 23 || minutes > 59) {
    return false;
  }
  out = sign * (hours * 3600 + minutes * 60);
  return true;
}

int32_t configEffectiveUtcOffset() {
  if (asciiEqualIgnoreCase(currentConfig.time.timezone, "auto")) {
    return currentConfig.time.lastUtcOffsetSeconds;
  }
  int32_t offset = 0;
  configParseUtcOffset(currentConfig.time.timezone, offset);
  return offset;
}

bool configRememberUtcOffset(int32_t offsetSeconds) {
  if (offsetSeconds < -86399 || offsetSeconds > 86399) {
    return false;
  }
  if (currentConfig.time.lastUtcOffsetSeconds == offsetSeconds) {
    return true;
  }
  char value[16];
  snprintf(value, sizeof(value), "%ld", static_cast<long>(offsetSeconds));
  return configSet("time.lastUtcOffsetSeconds", value);
}

void configReload() {
  resetSystemDefaults();
  visitConfigFile(kSystemConfigPath, applySystemVisitor, nullptr);
}

const SystemConfig &currentSystemConfig() { return currentConfig; }

bool configGet(const char *key, char *out, size_t outSize) {
  if (!validKey(key) || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  return systemCurrentValue(key, out, outSize);
}

static bool configLineHasKey(const char *line, const char *key) {
  const char *start = line;
  while (*start != '\0' && isspace(static_cast<unsigned char>(*start))) {
    start++;
  }
  const char *equals = strchr(start, '=');
  if (equals == nullptr) {
    return false;
  }
  const char *end = equals;
  while (end > start && isspace(static_cast<unsigned char>(end[-1]))) {
    end--;
  }
  const size_t length = static_cast<size_t>(end - start);
  return strlen(key) == length && strncmp(start, key, length) == 0;
}

static bool rewriteConfigFile(const char *path, const char *key,
                              const char *value) {
  if (path == nullptr || !sdStorageMounted() || !validKey(key) ||
      (value != nullptr && !validValue(value))) {
    return false;
  }

  const size_t keyLen = strlen(key);
  char lineBuf[128];
  char *lines = nullptr;
  size_t linesLen = 0;
  size_t linesCap = 0;

  File readFile = SD_MMC.open(path);
  if (readFile) {
    while (readFile.available() > 0) {
      size_t len = 0;
      while (readFile.available() > 0 && len + 1 < sizeof(lineBuf)) {
        const int raw = readFile.read();
        if (raw < 0) break;
        const char c = static_cast<char>(raw);
        if (c == '\n' || c == '\r') break;
        lineBuf[len++] = c;
      }
      lineBuf[len] = '\0';

      if (len == 0) {
        continue;
      }

      if (configLineHasKey(lineBuf, key)) {
        continue;
      }

      if (linesLen + len + 2 > linesCap) {
        linesCap = linesCap == 0 ? 256 : linesCap * 2;
        char *bigger = static_cast<char *>(realloc(lines, linesCap));
        if (bigger == nullptr) {
          free(lines);
          readFile.close();
          return false;
        }
        lines = bigger;
      }
      memcpy(lines + linesLen, lineBuf, len);
      linesLen += len;
      lines[linesLen++] = '\n';
    }
    readFile.close();
  }

  SD_MMC.mkdir(kConfigDir);
  File writeFile = SD_MMC.open(path, FILE_WRITE);
  if (!writeFile) {
    free(lines);
    return false;
  }

  bool ok = true;
  if (lines != nullptr && linesLen > 0) {
    ok = writeFile.write(reinterpret_cast<const uint8_t *>(lines), linesLen) == linesLen;
  }
  free(lines);

  if (value != nullptr) {
    const size_t valueLen = strlen(value);
    ok = ok &&
         writeFile.write(reinterpret_cast<const uint8_t *>(key), keyLen) == keyLen &&
         writeFile.write(reinterpret_cast<const uint8_t *>("="), 1) == 1 &&
         writeFile.write(reinterpret_cast<const uint8_t *>(value), valueLen) == valueLen &&
         writeFile.write(reinterpret_cast<const uint8_t *>("\n"), 1) == 1;
  }

  writeFile.close();
  return ok;
}

bool configSet(const char *key, const char *value) {
  if (!validSystemValue(key, value)) {
    return false;
  }
  const bool written = rewriteConfigFile(kSystemConfigPath, key, value);
  if (written) {
    configReload();
  }
  return written;
}

bool configRemove(const char *key) {
  const bool removed = rewriteConfigFile(kSystemConfigPath, key, nullptr);
  if (removed) {
    configReload();
  }
  return removed;
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
  return rewriteConfigFile(path, key, value);
}
