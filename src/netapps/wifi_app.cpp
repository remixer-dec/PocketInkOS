#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "netapps/wifi_app.h"
#include "sys/config.h"
#include "sys/device_clock.h"
#include "sys/pcf85063_clock.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Stream.h>
#include <ctype.h>
#include <cstring>

static const UiRect CONNECT_BUTTON = {20, 126, 76, 28};
static const UiRect DISCONNECT_BUTTON = {104, 126, 76, 28};
static const int MAX_TIME_RESPONSE = 512;
static const int NTP_PACKET_SIZE = 48;
static const uint16_t NTP_PORT = 123;
static const uint16_t NTP_LOCAL_PORT = 2390;
static const uint32_t NTP_TO_UNIX_SECONDS = 2208988800UL;
static const char *DEFAULT_NTP_SERVER = "pool.ntp.org";

static bool hasHttpScheme(const char *value) {
  return value != nullptr &&
         (strncmp(value, "https://", 8) == 0 || strncmp(value, "http://", 7) == 0);
}

void WifiApp::reset() {
  if (WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
  } else {
    state = STATE_IDLE;
  }
  timeState = TIME_IDLE;
  datetime[0] = '\0';
  timeStatus[0] = '\0';
  if (state == STATE_IDLE && systemConfig().wifi.autoConnect &&
      systemConfig().wifi.ssid[0] != '\0') {
    connect();
  }
}

void WifiApp::connect() {
  state = STATE_CONNECTING;
  timeState = TIME_IDLE;
  datetime[0] = '\0';
  timeStatus[0] = '\0';
  startedAt = millis();
  lastTimeAttemptAt = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(systemConfig().wifi.ssid, systemConfig().wifi.password);
}

void WifiApp::disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = STATE_IDLE;
  timeState = TIME_IDLE;
  lastTimeAttemptAt = 0;
  datetime[0] = '\0';
  setTimeStatus("WiFi off");
}

bool WifiApp::hasActiveSession() const {
  return state == STATE_CONNECTING || timeState == TIME_FETCHING;
}

WifiDisplayState WifiApp::displayState() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WIFI_DISPLAY_CONNECTED;
  }
  if (state == STATE_IDLE) {
    return WIFI_DISPLAY_OFF;
  }
  return WIFI_DISPLAY_ON;
}

void WifiApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  gfx.getTextBounds("WIFI", 0, 0, &titleX, &titleY, &titleW, &titleH);
  gfx.setCursor((200 - static_cast<int>(titleW)) / 2 - titleX, 16);
  gfx.print("WIFI");

  drawStatusIcon(gfx, 86, 48);

  gfx.setTextSize(1);
  const char *status = "Off";
  if (WiFi.status() == WL_CONNECTED) {
    status = "Connected";
  } else if (state == STATE_CONNECTING) {
    status = "Connecting";
  } else if (state == STATE_FAILED) {
    status = "Connect failed";
  }

  int16_t statusX;
  int16_t statusY;
  uint16_t statusW;
  uint16_t statusH;
  gfx.getTextBounds(status, 0, 0, &statusX, &statusY, &statusW, &statusH);
  gfx.setCursor((200 - static_cast<int>(statusW)) / 2 - statusX, 90);
  gfx.print(status);

  const char *displayName = systemConfig().wifi.displayName;
  gfx.getTextBounds(displayName, 0, 0, &statusX, &statusY, &statusW, &statusH);
  gfx.setCursor((200 - static_cast<int>(statusW)) / 2 - statusX, 106);
  gfx.print(displayName);

  const char *timeText = timeStatus;
  if (timeState == TIME_READY) {
    timeText = datetime;
  } else if (timeState == TIME_FETCHING) {
    timeText = "Fetching time";
  } else if (timeText[0] == '\0') {
    timeText = "Time not synced";
  }
  gfx.getTextBounds(timeText, 0, 0, &statusX, &statusY, &statusW, &statusH);
  gfx.setCursor((200 - static_cast<int>(statusW)) / 2 - statusX, 166);
  gfx.print(timeText);

  const bool connected = WiFi.status() == WL_CONNECTED;
  const char *connectLabel = "CONNECT";
  if (state == STATE_CONNECTING) {
    connectLabel = "WAIT";
  } else if (connected) {
    connectLabel = "ONLINE";
  }
  uiDrawButton(gfx, CONNECT_BUTTON, connectLabel,
               state == STATE_CONNECTING || connected);
  uiDrawButton(gfx, DISCONNECT_BUTTON, "WIFI OFF");
}

bool WifiApp::update() {
  if (state == STATE_CONNECTING && WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
  }

  if (state == STATE_CONNECTING && millis() - startedAt > CONNECT_TIMEOUT_MS) {
    state = STATE_FAILED;
    setTimeStatus("Connect timeout");
    return true;
  }

  if (state != STATE_CONNECTED && WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
  }

  if (state == STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
    state = STATE_FAILED;
    setTimeStatus("Disconnected");
    return true;
  }

  if (state == STATE_CONNECTED && timeState != TIME_READY &&
      timeState != TIME_FETCHING) {
    unsigned long now = millis();
    if (lastTimeAttemptAt == 0 || timeState == TIME_IDLE ||
        now - lastTimeAttemptAt >= TIME_RETRY_MS) {
      lastTimeAttemptAt = now;
      timeState = TIME_FETCHING;
      bool ok = fetchCurrentTime();
      timeState = ok ? TIME_READY : TIME_FAILED;
      return true;
    }
  }

  return false;
}

bool WifiApp::handleTouch(const TouchPoint &point) {
  if (uiContains(DISCONNECT_BUTTON, point)) {
    disconnect();
    return true;
  }
  if (!uiContains(CONNECT_BUTTON, point)) {
    return false;
  }

  if (state == STATE_CONNECTING || WiFi.status() == WL_CONNECTED) {
    return false;
  }
  connect();
  return true;
}

bool WifiApp::fetchCurrentTime() {
  if (WiFi.status() != WL_CONNECTED) {
    setTimeStatus("No WiFi");
    return false;
  }

  datetime[0] = '\0';
  setTimeStatus("");

  if (systemConfig().time.syncProtocol == CONFIG_TIME_SYNC_NTP) {
    return fetchNtpTime();
  }
  return fetchHttpsTime();
}

bool WifiApp::fetchHttpsTime() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(TIME_TIMEOUT_MS);
  if (!http.begin(client, systemConfig().time.syncServer)) {
    setTimeStatus("HTTP setup fail");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setTimeStatus("HTTP error");
    http.end();
    return false;
  }

  int size = http.getSize();
  if (size > MAX_TIME_RESPONSE) {
    setTimeStatus("Reply too large");
    http.end();
    return false;
  }

  char response[MAX_TIME_RESPONSE + 1] = {};
  Stream *stream = http.getStreamPtr();
  if (stream == nullptr) {
    setTimeStatus("HTTP no stream");
    http.end();
    return false;
  }
  unsigned long readStartedAt = millis();
  int count = 0;
  while (millis() - readStartedAt < TIME_TIMEOUT_MS &&
         count < MAX_TIME_RESPONSE) {
    while (stream != nullptr && stream->available() > 0 &&
           count < MAX_TIME_RESPONSE) {
      int value = stream->read();
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
    setTimeStatus("Empty reply");
    return false;
  }
  if (count >= MAX_TIME_RESPONSE) {
    setTimeStatus("Reply clipped");
    return false;
  }

  if (!parseTimePayload(response)) {
    setTimeStatus("Bad time data");
    return false;
  }
  deviceClock.set(unixTime, utcOffsetSeconds);
#if ENABLE_RTC_CLOCK
  rtcClock.writeFromUnix(unixTime, utcOffsetSeconds);
#endif
  setTimeStatus("Time synced");
  return true;
}

bool WifiApp::fetchNtpTime() {
  const char *server = configuredNtpServer();
  if (server == nullptr || server[0] == '\0') {
    setTimeStatus("No NTP host");
    return false;
  }

  uint8_t packet[NTP_PACKET_SIZE] = {};
  packet[0] = 0x1B; // LI=0, VN=3, mode=client.

  WiFiUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    setTimeStatus("NTP setup fail");
    return false;
  }
  if (!udp.beginPacket(server, NTP_PORT)) {
    udp.stop();
    setTimeStatus("NTP host fail");
    return false;
  }
  if (udp.write(packet, sizeof(packet)) != sizeof(packet) || !udp.endPacket()) {
    udp.stop();
    setTimeStatus("NTP send fail");
    return false;
  }

  const unsigned long startedAt = millis();
  while (millis() - startedAt < TIME_TIMEOUT_MS) {
    const int size = udp.parsePacket();
    if (size > 0 && size < NTP_PACKET_SIZE) {
      udp.read(packet, static_cast<size_t>(size));
      udp.stop();
      setTimeStatus("NTP short reply");
      return false;
    }
    if (size >= NTP_PACKET_SIZE) {
      if (udp.read(packet, sizeof(packet)) != sizeof(packet)) {
        udp.stop();
        setTimeStatus("NTP read fail");
        return false;
      }

      const uint8_t mode = packet[0] & 0x07;
      const uint8_t leap = packet[0] >> 6;
      const uint8_t stratum = packet[1];
      if ((mode != 4 && mode != 5) || leap == 3 || stratum == 0) {
        udp.stop();
        setTimeStatus("NTP invalid");
        return false;
      }

      const uint32_t secondsSince1900 =
          (static_cast<uint32_t>(packet[40]) << 24) |
          (static_cast<uint32_t>(packet[41]) << 16) |
          (static_cast<uint32_t>(packet[42]) << 8) |
          static_cast<uint32_t>(packet[43]);
      if (secondsSince1900 < NTP_TO_UNIX_SECONDS) {
        udp.stop();
        setTimeStatus("NTP bad time");
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
      setTimeStatus("Time synced");
      udp.stop();
      return true;
    }
    delay(10);
  }

  udp.stop();
  setTimeStatus("NTP timeout");
  return false;
}

const char *WifiApp::configuredNtpServer() const {
  const char *server = systemConfig().time.syncServer;
  if (server == nullptr || server[0] == '\0') {
    return server;
  }
  return hasHttpScheme(server) ? DEFAULT_NTP_SERVER : server;
}

bool WifiApp::parseTimePayload(const char *json) {
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

bool WifiApp::parseInt64Field(const char *json, const char *key,
                              int64_t &out) const {
  const char *cursor = strstr(json, key);
  if (cursor == nullptr) {
    return false;
  }
  cursor += strlen(key);
  bool negative = *cursor == '-';
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

bool WifiApp::parseUtcOffset(const char *json, int32_t &out) const {
  static const char *KEY = "\"utc_offset\":\"";
  const char *cursor = strstr(json, KEY);
  if (cursor == nullptr) {
    return false;
  }
  cursor += strlen(KEY);
  int sign = *cursor == '-' ? -1 : 1;
  if (*cursor != '+' && *cursor != '-') {
    return false;
  }
  cursor++;
  if (cursor[0] < '0' || cursor[0] > '9' || cursor[1] < '0' ||
      cursor[1] > '9' || cursor[2] != ':' || cursor[3] < '0' ||
      cursor[3] > '9' || cursor[4] < '0' || cursor[4] > '9') {
    return false;
  }
  int hours = (cursor[0] - '0') * 10 + (cursor[1] - '0');
  int minutes = (cursor[3] - '0') * 10 + (cursor[4] - '0');
  if (hours > 23 || minutes > 59) {
    return false;
  }
  out = sign * (hours * 3600 + minutes * 60);
  return true;
}

bool WifiApp::parseConfiguredUtcOffset(int32_t &out) const {
  const char *cursor = systemConfig().time.timezone;
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

void WifiApp::setTimeStatus(const char *status) {
  if (status == nullptr) {
    timeStatus[0] = '\0';
    return;
  }
  strncpy(timeStatus, status, TIME_STATUS_SIZE - 1);
  timeStatus[TIME_STATUS_SIZE - 1] = '\0';
}

void WifiApp::drawStatusIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const {
  drawWifiIcon(gfx, x, y);
}

void WifiApp::drawWifiIcon(Adafruit_GFX &gfx, int16_t x, int16_t y) const {
  WifiDisplayState iconState = displayState();
  gfx.drawCircle(x + 14, y + 28, 2, 1);
  gfx.drawLine(x + 4, y + 18, x + 14, y + 8, 1);
  gfx.drawLine(x + 14, y + 8, x + 24, y + 18, 1);
  gfx.drawLine(x + 8, y + 22, x + 14, y + 16, 1);
  gfx.drawLine(x + 14, y + 16, x + 20, y + 22, 1);

  if (iconState == WIFI_DISPLAY_CONNECTED) {
    gfx.drawLine(x + 31, y + 24, x + 35, y + 28, 1);
    gfx.drawLine(x + 35, y + 28, x + 43, y + 16, 1);
    return;
  }

  if (iconState == WIFI_DISPLAY_ON) {
    gfx.fillRect(x + 32, y + 24, 4, 4, 1);
    return;
  }

  gfx.drawLine(x + 31, y + 16, x + 43, y + 28, 1);
  gfx.drawLine(x + 43, y + 16, x + 31, y + 28, 1);
}

#endif
