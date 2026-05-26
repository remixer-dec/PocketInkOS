#pragma once

#include "WiFi.h"

#define HTTP_CODE_OK 200

class HTTPClient {
public:
  void setTimeout(unsigned long) {}
  bool begin(const char *) { return true; }
  bool begin(WiFiClient &, const char *) { return true; }
  void addHeader(const char *, const char *) {}
  int GET() { return HTTP_CODE_OK; }
  int getSize() const { return 0; }
  WiFiClient *getStreamPtr() { return &client; }
  void end() {}

private:
  WiFiClient client;
};
