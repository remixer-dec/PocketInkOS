#pragma once

#include "Arduino.h"
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
#define WIFI_AP 2
#define WIFI_AP_STA 3

class IPAddress {
public:
  IPAddress() : bytes{0, 0, 0, 0} {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : bytes{a, b, c, d} {}
  explicit IPAddress(uint32_t addr)
      : bytes{static_cast<uint8_t>(addr & 0xFF),
              static_cast<uint8_t>((addr >> 8) & 0xFF),
              static_cast<uint8_t>((addr >> 16) & 0xFF),
              static_cast<uint8_t>((addr >> 24) & 0xFF)} {}

  operator uint32_t() const {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
  }

  String toString() const {
    char out[16];
    std::snprintf(out, sizeof(out), "%u.%u.%u.%u", bytes[0], bytes[1],
                  bytes[2], bytes[3]);
    return String(out);
  }

  uint8_t operator[](int index) const { return bytes[index]; }
  uint8_t &operator[](int index) { return bytes[index]; }

private:
  uint8_t bytes[4];
};

class WiFiClient : public Stream {
public:
  using Stream::write;

  int available() override { return 0; }
  int read() override { return -1; }
  bool connect(const char *, uint16_t) { return true; }
  bool connect(IPAddress, uint16_t, int32_t = 0) { return true; }
  void stop() {}
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t size) { return size; }
  size_t write(const char *data) { return data ? std::strlen(data) : 0; }
  bool connected() { return true; }
  void setTimeout(unsigned long) {}
};

class WiFiClass {
public:
  bool mode(int value) {
    modeValue = value;
    if (value == WIFI_OFF) {
      statusValue = WL_DISCONNECTED;
      apStarted = false;
    } else if (value == WIFI_AP) {
      statusValue = WL_DISCONNECTED;
    }
    return true;
  }
  void persistent(bool) {}
  void begin(const char *ssid, const char *) {
    savedSsid = ssid ? ssid : "";
    statusValue = WL_CONNECTED;
  }
  void disconnect(bool = false, bool = false) { statusValue = WL_DISCONNECTED; }
  wl_status_t status() const { return statusValue; }
  int scanNetworks() { return 0; }
  void scanDelete() {}
  String SSID() const { return savedSsid; }
  String SSID(int) const { return String(); }
  String BSSIDstr(int) const { return String(); }
  String macAddress() const { return "02:00:00:00:00:01"; }
  int32_t RSSI(int) const { return -100; }
  IPAddress localIP() const { return IPAddress(192, 168, 1, 20); }
  IPAddress gatewayIP() const { return IPAddress(192, 168, 1, 1); }
  IPAddress subnetMask() const { return IPAddress(255, 255, 255, 0); }
  IPAddress dnsIP(uint8_t = 0) const { return IPAddress(1, 1, 1, 1); }
  bool softAP(const char *ssid, const char *) {
    savedApSsid = ssid ? ssid : "";
    apStarted = true;
    return true;
  }
  bool softAPdisconnect(bool wifiOff = false) {
    apStarted = false;
    savedApSsid = "";
    if (wifiOff) {
      statusValue = WL_DISCONNECTED;
    }
    return true;
  }
  IPAddress softAPIP() const {
    return apStarted ? IPAddress(192, 168, 4, 1) : IPAddress();
  }
  bool hostByName(const char *, IPAddress &out) {
    out = IPAddress(93, 184, 216, 34);
    return true;
  }


private:
  wl_status_t statusValue = WL_DISCONNECTED;
  int modeValue = WIFI_OFF;
  String savedSsid;
  String savedApSsid;
  bool apStarted = false;
};

inline WiFiClass WiFi;
