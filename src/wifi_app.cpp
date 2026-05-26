#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "wifi_app.h"
#include "device_clock.h"
#include "secrets_config.h"
#include "ui_helpers.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Stream.h>
#include <cstring>

const char *WifiApp::SSID = SECRET_WIFI_SSID;
const char *WifiApp::PASSWORD = SECRET_WIFI_PASSWORD;
// Keep this ASCII; the actual SSID may contain UTF-8 that the font cannot draw.
const char *WifiApp::DISPLAY_NAME = SECRET_WIFI_DISPLAY_NAME;

static const char *TIME_URL = "https://time.now/developer/api/ip";
static const UiRect CONNECT_BUTTON = {20, 126, 76, 28};
static const UiRect DISCONNECT_BUTTON = {104, 126, 76, 28};
static const int MAX_TIME_RESPONSE = 512;

void WifiApp::reset() {
  if (WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
  } else {
    state = STATE_IDLE;
  }
  timeState = TIME_IDLE;
  datetime[0] = '\0';
  timeStatus[0] = '\0';
}

void WifiApp::connect() {
  state = STATE_CONNECTING;
  timeState = TIME_IDLE;
  datetime[0] = '\0';
  timeStatus[0] = '\0';
  startedAt = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
}

void WifiApp::disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = STATE_IDLE;
  timeState = TIME_IDLE;
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

  gfx.getTextBounds(DISPLAY_NAME, 0, 0, &statusX, &statusY, &statusW,
                    &statusH);
  gfx.setCursor((200 - static_cast<int>(statusW)) / 2 - statusX, 106);
  gfx.print(DISPLAY_NAME);

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
    timeState = TIME_FETCHING;
    bool ok = fetchCurrentTime();
    timeState = ok ? TIME_READY : TIME_FAILED;
    return true;
  }

  if (state == STATE_CONNECTING && millis() - startedAt > CONNECT_TIMEOUT_MS) {
    state = STATE_FAILED;
    setTimeStatus("Connect timeout");
    return true;
  }

  if (state != STATE_CONNECTED && WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
    return true;
  }

  if (state == STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
    state = STATE_FAILED;
    setTimeStatus("Disconnected");
    return true;
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

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(TIME_TIMEOUT_MS);
  if (!http.begin(client, TIME_URL)) {
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
  setTimeStatus("Time synced");
  return true;
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
