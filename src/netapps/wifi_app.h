#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <Adafruit_GFX.h>
#include "sys/touch_input.h"

enum WifiDisplayState {
  WIFI_DISPLAY_OFF,
  WIFI_DISPLAY_ON,
  WIFI_DISPLAY_CONNECTED
};

class WifiApp {
public:
  void reset();
  void connect();
  void disconnect();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;
  WifiDisplayState displayState() const;
  void drawStatusIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const;

private:
  enum State { STATE_IDLE, STATE_CONNECTING, STATE_CONNECTED, STATE_FAILED };
  enum TimeState {
    TIME_IDLE,
    TIME_FETCHING,
    TIME_READY,
    TIME_FAILED,
  };

  static const char *SSID;
  static const char *PASSWORD;
  static const char *DISPLAY_NAME;
  static const unsigned long CONNECT_TIMEOUT_MS = 15000;
  static const unsigned long TIME_TIMEOUT_MS = 6000;
  static const unsigned long TIME_RETRY_MS = 60000;
  static const int DATETIME_SIZE = 32;
  static const int TIME_STATUS_SIZE = 24;

  State state = STATE_IDLE;
  TimeState timeState = TIME_IDLE;
  unsigned long startedAt = 0;
  unsigned long lastTimeAttemptAt = 0;
  char datetime[DATETIME_SIZE] = "";
  char timeStatus[TIME_STATUS_SIZE] = "";
  int64_t unixTime = 0;
  int32_t utcOffsetSeconds = 0;

  bool fetchCurrentTime();
  bool parseTimePayload(const char *json);
  bool parseInt64Field(const char *json, const char *key, int64_t &out) const;
  bool parseUtcOffset(const char *json, int32_t &out) const;
  void setTimeStatus(const char *status);
  void drawWifiIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const;
};

#endif
