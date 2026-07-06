#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "sys/time_sync.h"
#include "sys/device_clock.h"
#include "sys/pcf85063_clock.h"
#include "sys/system_config.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Stream.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ctype.h>
#include <cstring>

static const int MAX_TIME_RESPONSE = 512;
static const int NTP_PACKET_SIZE = 48;
static const uint16_t NTP_PORT = 123;
static const uint16_t NTP_LOCAL_PORT = 2390;
static const uint32_t NTP_TO_UNIX_SECONDS = 2208988800UL;
static const char *DEFAULT_NTP_SERVER = "pool.ntp.org";

TimeSyncService timeSync;

static bool hasHttpScheme(const char *value) {
  return value != nullptr &&
         (strncmp(value, "https://", 8) == 0 ||
          strncmp(value, "http://", 7) == 0);
}

void TimeSyncService::reset() {
  state = STATE_IDLE;
  lastAttemptAt = 0;
  datetime[0] = '\0';
  status[0] = '\0';
}

bool TimeSyncService::update(bool wifiConnected) {
  if (!wifiConnected) {
    if (state == STATE_FETCHING) {
      state = STATE_FAILED;
      setStatus("No WiFi");
      return true;
    }
    return false;
  }

  if (state == STATE_READY || state == STATE_FETCHING) {
    return false;
  }

  const unsigned long now = millis();
  if (lastAttemptAt != 0 && state != STATE_IDLE &&
      now - lastAttemptAt < TIME_RETRY_MS) {
    return false;
  }

  lastAttemptAt = now;
  state = STATE_FETCHING;
  const bool ok = fetchCurrentTime();
  state = ok ? STATE_READY : STATE_FAILED;
  return true;
}

bool TimeSyncService::hasActiveSession() const {
  return state == STATE_FETCHING;
}

bool TimeSyncService::fetchCurrentTime() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("No WiFi");
    return false;
  }

  datetime[0] = '\0';
  setStatus("");

  if (currentSystemConfig().time.syncProtocol == CONFIG_TIME_SYNC_NTP) {
    return fetchNtpTime();
  }
  return fetchHttpsTime();
}

bool TimeSyncService::fetchHttpsTime() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(TIME_TIMEOUT_MS);
  if (!http.begin(client, currentSystemConfig().time.syncServer)) {
    setStatus("HTTP setup fail");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setStatus("HTTP error");
    http.end();
    return false;
  }

  const int size = http.getSize();
  if (size > MAX_TIME_RESPONSE) {
    setStatus("Reply too large");
    http.end();
    return false;
  }

  char response[MAX_TIME_RESPONSE + 1] = {};
  Stream *stream = http.getStreamPtr();
  if (stream == nullptr) {
    setStatus("HTTP no stream");
    http.end();
    return false;
  }

  const unsigned long readStartedAt = millis();
  int count = 0;
  while (millis() - readStartedAt < TIME_TIMEOUT_MS &&
         count < MAX_TIME_RESPONSE) {
    while (stream != nullptr && stream->available() > 0 &&
           count < MAX_TIME_RESPONSE) {
      const int value = stream->read();
      if (value < 0) {
        break;
      }
      response[count++] = static_cast<char>(value);
    }
    if (size >= 0 && count >= size) {
      break;
    }
    if (size < 0 && count > 0 && stream != nullptr && !stream->available()) {
      break;
    }
    delay(1);
  }
  response[count] = '\0';
  http.end();

  if (count <= 0) {
    setStatus("Empty reply");
    return false;
  }
  if (count >= MAX_TIME_RESPONSE) {
    setStatus("Reply clipped");
    return false;
  }

  if (!parseTimePayload(response)) {
    setStatus("Bad time data");
    return false;
  }
  deviceClock.set(unixTime, utcOffsetSeconds);
#if ENABLE_RTC_CLOCK
  rtcClock.writeFromUnix(unixTime, utcOffsetSeconds);
#endif
  setStatus("Time synced");
  return true;
}

bool TimeSyncService::fetchNtpTime() {
  const char *server = configuredNtpServer();
  if (server == nullptr || server[0] == '\0') {
    setStatus("No NTP host");
    return false;
  }

  uint8_t packet[NTP_PACKET_SIZE] = {};
  packet[0] = 0x1B;

  WiFiUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    setStatus("NTP setup fail");
    return false;
  }
  if (!udp.beginPacket(server, NTP_PORT)) {
    udp.stop();
    setStatus("NTP host fail");
    return false;
  }
  if (udp.write(packet, sizeof(packet)) != sizeof(packet) || !udp.endPacket()) {
    udp.stop();
    setStatus("NTP send fail");
    return false;
  }

  const unsigned long startedAt = millis();
  while (millis() - startedAt < TIME_TIMEOUT_MS) {
    const int size = udp.parsePacket();
    if (size > 0 && size < NTP_PACKET_SIZE) {
      udp.read(packet, static_cast<size_t>(size));
      udp.stop();
      setStatus("NTP short reply");
      return false;
    }
    if (size >= NTP_PACKET_SIZE) {
      if (udp.read(packet, sizeof(packet)) != sizeof(packet)) {
        udp.stop();
        setStatus("NTP read fail");
        return false;
      }

      const uint8_t mode = packet[0] & 0x07;
      const uint8_t leap = packet[0] >> 6;
      const uint8_t stratum = packet[1];
      if ((mode != 4 && mode != 5) || leap == 3 || stratum == 0) {
        udp.stop();
        setStatus("NTP invalid");
        return false;
      }

      const uint32_t secondsSince1900 =
          (static_cast<uint32_t>(packet[40]) << 24) |
          (static_cast<uint32_t>(packet[41]) << 16) |
          (static_cast<uint32_t>(packet[42]) << 8) |
          static_cast<uint32_t>(packet[43]);
      if (secondsSince1900 < NTP_TO_UNIX_SECONDS) {
        udp.stop();
        setStatus("NTP bad time");
        return false;
      }

      unixTime = static_cast<int64_t>(secondsSince1900 - NTP_TO_UNIX_SECONDS);
      if (!parseConfiguredUtcOffset(utcOffsetSeconds)) {
        utcOffsetSeconds = 0;
      }
      deviceClock.set(unixTime, utcOffsetSeconds);
#if ENABLE_RTC_CLOCK
      rtcClock.writeFromUnix(unixTime, utcOffsetSeconds);
#endif
      strncpy(datetime, "NTP synced", DATETIME_SIZE - 1);
      datetime[DATETIME_SIZE - 1] = '\0';
      setStatus("Time synced");
      udp.stop();
      return true;
    }
    delay(10);
  }

  udp.stop();
  setStatus("NTP timeout");
  return false;
}

const char *TimeSyncService::configuredNtpServer() const {
  const char *server = currentSystemConfig().time.syncServer;
  if (server == nullptr || server[0] == '\0') {
    return server;
  }
  return hasHttpScheme(server) ? DEFAULT_NTP_SERVER : server;
}

bool TimeSyncService::parseTimePayload(const char *json) {
  static const char *KEY = "\"datetime\":\"";
  const char *start = strstr(json, KEY);
  if (start == nullptr) {
    return false;
  }
  start += strlen(KEY);
  const char *end = strchr(start, '"');
  if (end == nullptr || end <= start) {
    return false;
  }

  size_t len = static_cast<size_t>(end - start);
  if (len >= DATETIME_SIZE) {
    len = DATETIME_SIZE - 1;
  }
  memcpy(datetime, start, len);
  datetime[len] = '\0';

  if (!parseInt64Field(json, "\"unixtime\":", unixTime)) {
    return false;
  }
  if (!parseUtcOffset(json, utcOffsetSeconds)) {
    utcOffsetSeconds = 0;
  }
  return true;
}

bool TimeSyncService::parseInt64Field(const char *json, const char *key,
                                      int64_t &out) const {
  const char *cursor = strstr(json, key);
  if (cursor == nullptr) {
    return false;
  }
  cursor += strlen(key);
  const bool negative = *cursor == '-';
  if (negative) {
    cursor++;
  }
  if (*cursor < '0' || *cursor > '9') {
    return false;
  }
  int64_t value = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    if (value > 922337203685477580LL) {
      return false;
    }
    value = value * 10 + (*cursor - '0');
    cursor++;
  }
  out = negative ? -value : value;
  return true;
}

bool TimeSyncService::parseUtcOffset(const char *json, int32_t &out) const {
  static const char *KEY = "\"utc_offset\":\"";
  const char *cursor = strstr(json, KEY);
  if (cursor == nullptr) {
    return false;
  }
  cursor += strlen(KEY);
  const int sign = *cursor == '-' ? -1 : 1;
  if (*cursor != '+' && *cursor != '-') {
    return false;
  }
  cursor++;
  if (cursor[0] < '0' || cursor[0] > '9' || cursor[1] < '0' ||
      cursor[1] > '9' || cursor[2] != ':' || cursor[3] < '0' ||
      cursor[3] > '9' || cursor[4] < '0' || cursor[4] > '9') {
    return false;
  }
  const int hours = (cursor[0] - '0') * 10 + (cursor[1] - '0');
  const int minutes = (cursor[3] - '0') * 10 + (cursor[4] - '0');
  if (hours > 23 || minutes > 59) {
    return false;
  }
  out = sign * (hours * 3600 + minutes * 60);
  return true;
}

bool TimeSyncService::parseConfiguredUtcOffset(int32_t &out) const {
  const char *cursor = currentSystemConfig().time.timezone;
  if (cursor == nullptr || cursor[0] == '\0' ||
      strcasecmp(cursor, "auto") == 0 || strcasecmp(cursor, "utc") == 0 ||
      strcasecmp(cursor, "z") == 0) {
    out = 0;
    return true;
  }
  if ((cursor[0] == 'U' || cursor[0] == 'u') &&
      (cursor[1] == 'T' || cursor[1] == 't') &&
      (cursor[2] == 'C' || cursor[2] == 'c')) {
    cursor += 3;
  }

  const int sign = *cursor == '-' ? -1 : 1;
  if (*cursor != '+' && *cursor != '-') {
    return false;
  }
  cursor++;
  if (!isdigit(static_cast<unsigned char>(cursor[0]))) {
    return false;
  }

  int hours = cursor[0] - '0';
  cursor++;
  if (isdigit(static_cast<unsigned char>(*cursor))) {
    hours = hours * 10 + (*cursor - '0');
    cursor++;
  }

  int minutes = 0;
  if (*cursor == ':') {
    cursor++;
  }
  if (isdigit(static_cast<unsigned char>(cursor[0])) &&
      isdigit(static_cast<unsigned char>(cursor[1]))) {
    minutes = (cursor[0] - '0') * 10 + (cursor[1] - '0');
  } else if (*cursor != '\0') {
    return false;
  }

  if (hours > 23 || minutes > 59) {
    return false;
  }
  out = sign * (hours * 3600 + minutes * 60);
  return true;
}

void TimeSyncService::setStatus(const char *value) {
  if (value == nullptr) {
    status[0] = '\0';
    return;
  }
  strncpy(status, value, STATUS_SIZE - 1);
  status[STATUS_SIZE - 1] = '\0';
}

#endif
