#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "sys/wifi_manager.h"
#include "sys/system_config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>

static const size_t WIFI_START_FREE_HEAP_MIN = 48U * 1024U;
static const size_t WIFI_START_LARGEST_BLOCK_MIN = 8U * 1024U;

WifiManager wifiManager;

static bool wifiStartHeapAvailable() {
  const size_t freeHeap =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largestBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return freeHeap >= WIFI_START_FREE_HEAP_MIN &&
         largestBlock >= WIFI_START_LARGEST_BLOCK_MIN;
}

static void configKey(char *out, size_t outSize, const char *prefix,
                      uint8_t index, const char *field) {
  snprintf(out, outSize, "%s.%u.%s", prefix, static_cast<unsigned>(index),
           field);
}

void WifiManager::reset() {
  if (WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
    staStarted = true;
  } else {
    state = STATE_IDLE;
    staStarted = false;
  }
  timeSync.reset();
  autoConnectPending = state == STATE_IDLE && currentSystemConfig().wifi.autoConnect &&
                        currentSystemConfig().wifi.ssid[0] != '\0';
}

bool WifiManager::startSta() {
  if (staStarted || WiFi.status() == WL_CONNECTED) {
    staStarted = true;
    return true;
  }
  if (!wifiStartHeapAvailable()) {
    state = STATE_FAILED;
    timeSync.setStatus("Low WiFi heap");
    return false;
  }
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA)) {
    state = STATE_FAILED;
    timeSync.setStatus("WiFi start fail");
    return false;
  }
  staStarted = true;
  return true;
}

void WifiManager::connect() {
  connectTo(currentSystemConfig().wifi.ssid, currentSystemConfig().wifi.password);
}

void WifiManager::connectTo(const char *ssid, const char *password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    state = STATE_FAILED;
    timeSync.setStatus("No SSID");
    return;
  }
  if (!startSta()) {
    return;
  }
  delay(100);
  state = STATE_CONNECTING;
  timeSync.reset();
  startedAt = millis();
  WiFi.begin(ssid, password != nullptr ? password : "");
}

void WifiManager::disconnect() {
  if (staStarted || WiFi.status() == WL_CONNECTED) {
    WiFi.scanDelete();
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect(true, false);
    }
    WiFi.mode(WIFI_OFF);
  }
  staStarted = false;
  state = STATE_IDLE;
  timeSync.reset();
  timeSync.setStatus("WiFi off");
}

void WifiManager::toggle() {
  if (displayState() == WIFI_DISPLAY_OFF) {
    connect();
  } else {
    disconnect();
  }
}

void WifiManager::restoreOn(bool enabled) {
  if (enabled) {
    connect();
  }
}

bool WifiManager::isOn() const {
  return displayState() != WIFI_DISPLAY_OFF;
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::isConnecting() const {
  return state == STATE_CONNECTING;
}

bool WifiManager::hasActiveSession() const {
  return state == STATE_CONNECTING || timeSync.hasActiveSession();
}

WifiDisplayState WifiManager::displayState() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WIFI_DISPLAY_CONNECTED;
  }
  if (state == STATE_IDLE || state == STATE_FAILED) {
    return WIFI_DISPLAY_OFF;
  }
  return WIFI_DISPLAY_ON;
}

bool WifiManager::update() {
  if (autoConnectPending && state == STATE_IDLE) {
    autoConnectPending = false;
    connect();
    return true;
  }

  if (state == STATE_CONNECTING && WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
    return true;
  }

  if (state == STATE_CONNECTING && millis() - startedAt > CONNECT_TIMEOUT_MS) {
    disconnect();
    state = STATE_FAILED;
    timeSync.setStatus("Connect timeout");
    return true;
  }

  if (state != STATE_CONNECTED && WiFi.status() == WL_CONNECTED) {
    state = STATE_CONNECTED;
    return true;
  }

  if (state == STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
    state = STATE_FAILED;
    timeSync.setStatus("Disconnected");
    return true;
  }

  if (timeSync.update(state == STATE_CONNECTED &&
                      WiFi.status() == WL_CONNECTED)) {
    return true;
  }

  return false;
}

bool WifiManager::preferredMatches(const char *ssid) const {
  return ssid != nullptr && strcmp(currentSystemConfig().wifi.ssid, ssid) == 0;
}

void WifiManager::saveNetworkSlot(uint8_t slot, const char *ssid,
                                  const char *password, const char *bssid) {
  if (slot >= CONFIG_WIFI_SAVED_MAX) {
    return;
  }
  char key[28];
  configKey(key, sizeof(key), "wifi.saved", slot, "ssid");
  configSet(key, ssid != nullptr ? ssid : "");
  configKey(key, sizeof(key), "wifi.saved", slot, "password");
  configSet(key, password != nullptr ? password : "");
  configKey(key, sizeof(key), "wifi.saved", slot, "bssid");
  configSet(key, bssid != nullptr ? bssid : "");
}

void WifiManager::savePreferred(const char *ssid, const char *password) {
  configSet("wifi.ssid", ssid != nullptr ? ssid : "");
  configSet("wifi.password", password != nullptr ? password : "");
}

void WifiManager::clearPreferredIfMatches(const char *ssid) {
  if (!preferredMatches(ssid)) {
    return;
  }
  configRemove("wifi.ssid");
  configRemove("wifi.password");
  configRemove("wifi.displayName");
}

#endif
