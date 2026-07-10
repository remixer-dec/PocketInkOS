#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/// Maximum stored length for a single string config value, including terminator.
static const uint16_t CONFIG_VALUE_MAX = 96;
/// Number of Wi-Fi network slots persisted for automatic reconnect.
static const uint8_t CONFIG_WIFI_SAVED_MAX = 3;
/// Largest supported inactivity timeout for the uint8_t setting, in minutes.
static const uint8_t CONFIG_LOCKSCREEN_INACTIVITY_MAX_MINUTES = UINT8_MAX;

/// CPU frequency policy stored in system config.
enum ConfigCpuGovernor : uint8_t {
  /// Keep the configured CPU frequency fixed.
  CONFIG_CPU_FIXED,
  /// Allow firmware to adjust CPU frequency for foreground work.
  CONFIG_CPU_BALANCED,
};

/// User-selected clock display format.
enum ConfigTimeFormat : uint8_t {
  /// Display time using 24-hour notation.
  CONFIG_TIME_24H,
  /// Display time using 12-hour notation.
  CONFIG_TIME_12H,
};

/// User-selected date display format.
enum ConfigDateFormat : uint8_t {
  /// ISO-style year-month-day display.
  CONFIG_DATE_ISO,
  /// Month-day-year display.
  CONFIG_DATE_MDY,
  /// Day-month-year display.
  CONFIG_DATE_DMY,
  /// Day.month.year display.
  CONFIG_DATE_DMY_DOT,
  /// Weekday-oriented compact display.
  CONFIG_DATE_WEEKDAY,
};

/// Network protocol used to set the local clock.
enum ConfigTimeSyncProtocol : uint8_t {
  /// Fetch time from the configured HTTPS endpoint.
  CONFIG_TIME_SYNC_HTTPS,
  /// Fetch time from the configured NTP server.
  CONFIG_TIME_SYNC_NTP,
};

/// Persisted Wi-Fi credentials for one remembered network.
struct ConfigWifiSavedNetwork {
  /// Network SSID.
  char ssid[33];
  /// Network password.
  char password[64];
  /// Optional BSSID used to prefer a specific access point.
  char bssid[18];
};

/// Complete persisted system configuration loaded at boot.
struct SystemConfig {
  struct {
    char ssid[64];
    char password[64];
    char displayName[24];
    bool autoConnect;
    ConfigWifiSavedNetwork saved[CONFIG_WIFI_SAVED_MAX];
  } wifi;

  struct {
    char sunBtnApp[32];
    char widgets[CONFIG_VALUE_MAX];
  } home;

  char layout[16];

  struct {
    char topIcon[2];
    bool showDate;
    uint8_t inactivityMinutes;
  } lockscreen;

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
    char unit[2];
  } sensors;

};

/// Initializes config storage and loads the active configuration.
void configBegin();
/// Replaces active configuration with built-in defaults.
void configLoadDefaults();
/// Reloads config from persistent storage.
void configReload();
/// Returns the active read-only system configuration.
const SystemConfig &currentSystemConfig();
/// Parses a whole-minute inactivity timeout in the supported 0–255 range.
bool configParseLockscreenInactivityMinutes(const char *value,
                                            uint8_t &out);

/// Reads a raw config value by key into out.
bool configGet(const char *key, char *out, size_t outSize);
/// Writes a raw config value by key.
bool configSet(const char *key, const char *value);
/// Removes a raw config value by key.
bool configRemove(const char *key);
/// Reads a value scoped to a single app id.
bool configGetForApp(const char *appId, const char *key, char *out,
                     size_t outSize);
/// Writes a value scoped to a single app id.
bool configSetForApp(const char *appId, const char *key, const char *value);

#endif
