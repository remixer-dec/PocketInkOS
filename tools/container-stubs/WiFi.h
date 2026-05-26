#pragma once

#include "Stream.h"
#include <cstring>

enum wl_status_t {
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6,
};

#define WIFI_STA 1
#define WIFI_OFF 0

class WiFiClient : public Stream {
public:
  using Stream::write;

  int available() override { return 0; }
  int read() override { return -1; }
  bool connect(const char *, uint16_t) { return true; }
  void stop() {}
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t size) { return size; }
  size_t write(const char *data) { return data ? std::strlen(data) : 0; }
  bool connected() { return true; }
  void setTimeout(unsigned long) {}
};

class WiFiClass {
public:
  void mode(int) {}
  void begin(const char *, const char *) { statusValue = WL_CONNECTED; }
  void disconnect(bool = false) { statusValue = WL_DISCONNECTED; }
  wl_status_t status() const { return statusValue; }

private:
  wl_status_t statusValue = WL_DISCONNECTED;
};

inline WiFiClass WiFi;
