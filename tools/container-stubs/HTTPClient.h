#pragma once

#include "WiFi.h"

#define HTTP_CODE_OK 200

class HTTPClient {
public:
  void setTimeout(unsigned long) {}
  void useHTTP10(bool = true) {}
  void setUserAgent(const char *) {}
  bool begin(const char *) { return true; }
  bool begin(WiFiClient &, const char *) { return true; }
  void addHeader(const char *, const char *) {}
  void collectHeaders(const char *[], size_t) {}
  String header(const char *) const { return "application/dns-message"; }
  int GET() { return HTTP_CODE_OK; }
  int sendRequest(const char *, uint8_t *, size_t) { return HTTP_CODE_OK; }
  String getString() { return "1.2.3.4"; }
  int getSize() const { return 0; }
  WiFiClient *getStreamPtr() { return &client; }
  void end() {}

private:
  WiFiClient client;
};
