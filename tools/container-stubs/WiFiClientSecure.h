#pragma once

#include "WiFi.h"

class WiFiClientSecure : public WiFiClient {
public:
  void setInsecure() {}
};
