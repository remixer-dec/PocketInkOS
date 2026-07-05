#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdint.h>

class TimeSyncService {
public:
  void reset();
  bool update(bool wifiConnected);
  bool hasActiveSession() const;
  const char *statusText() const { return status; }
  const char *datetimeText() const { return datetime; }
  void setStatus(const char *value);

private:
  enum State : uint8_t {
    STATE_IDLE,
    STATE_FETCHING,
    STATE_READY,
    STATE_FAILED,
  };

  static const unsigned long TIME_TIMEOUT_MS = 6000;
  static const unsigned long TIME_RETRY_MS = 60000;
  static const int DATETIME_SIZE = 32;
  static const int STATUS_SIZE = 24;

  State state = STATE_IDLE;
  unsigned long lastAttemptAt = 0;
  char datetime[DATETIME_SIZE] = "";
  char status[STATUS_SIZE] = "";
  int64_t unixTime = 0;
  int32_t utcOffsetSeconds = 0;

  bool fetchCurrentTime();
  bool fetchHttpsTime();
  bool fetchNtpTime();
  const char *configuredNtpServer() const;
  bool parseTimePayload(const char *json);
  bool parseInt64Field(const char *json, const char *key, int64_t &out) const;
  bool parseUtcOffset(const char *json, int32_t &out) const;
  bool parseConfiguredUtcOffset(int32_t &out) const;
};

extern TimeSyncService timeSync;

#endif
