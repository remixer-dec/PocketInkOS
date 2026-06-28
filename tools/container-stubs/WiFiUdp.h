#pragma once

#include <cstddef>
#include <cstdint>

class WiFiUDP {
public:
  bool begin(uint16_t) { return true; }
  bool beginPacket(const char *, uint16_t) { return true; }
  size_t write(const uint8_t *, size_t size) { return size; }
  bool endPacket() { return true; }
  int parsePacket() { return 0; }
  int read(uint8_t *, size_t) { return 0; }
  void stop() {}
};
