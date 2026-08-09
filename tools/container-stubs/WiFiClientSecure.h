#pragma once

#include "WiFi.h"

class WiFiClientSecure : public WiFiClient {
public:
  using WiFiClient::connect;
  void setInsecure() {}
  void setCACert(const char *) {}
  void setCACertBundle(const uint8_t *, size_t) {}
  void setHandshakeTimeout(unsigned long) {}
  int connect(IPAddress, uint16_t, const char *, const char *, const char *,
              const char *) {
    return 1;
  }
};
