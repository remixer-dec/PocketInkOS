#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>

static const uint16_t CONFIG_VALUE_MAX = 96;

enum ConfigCpuGovernor : uint8_t {
  CONFIG_CPU_FIXED,
  CONFIG_CPU_BALANCED,
};

enum ConfigTimeFormat : uint8_t {
  CONFIG_TIME_24H,
  CONFIG_TIME_12H,
};

enum ConfigDateFormat : uint8_t {
  CONFIG_DATE_ISO,
  CONFIG_DATE_MDY,
  CONFIG_DATE_DMY,
  CONFIG_DATE_DMY_DOT,
  CONFIG_DATE_WEEKDAY,
};

enum ConfigTimeSyncProtocol : uint8_t {
  CONFIG_TIME_SYNC_HTTPS,
  CONFIG_TIME_SYNC_NTP,
};

struct SystemConfig {
  struct {
    char ssid[64];
    char password[64];
    char displayName[24];
    bool autoConnect;
  } wifi;

  struct {
    char sunBtnApp[32];
  } home;

  struct {
    uint16_t mhz;
    ConfigCpuGovernor governor;
  } cpu;

  struct {
    ConfigTimeFormat timeFormat;
    ConfigDateFormat dateFormat;
    ConfigTimeSyncProtocol syncProtocol;
    char timezone[32];
    char syncServer[96];
  } time;

  struct {
    float temperatureOffsetC;
  } sensors;

};

void configBegin();
void configReload();
const SystemConfig &systemConfig();

bool configGet(const char *key, char *out, size_t outSize);
bool configSet(const char *key, const char *value);
bool configGetForApp(const char *appId, const char *key, char *out,
                     size_t outSize);
bool configSetForApp(const char *appId, const char *key, const char *value);

#endif
