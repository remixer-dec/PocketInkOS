#pragma once

#include "FS.h"
#include <cstdint>

#define CARD_NONE 0

class SDMMCFS : public fs::FS {
public:
  bool setPins(int, int, int) { return true; }
  bool begin(const char * = "/sdcard", bool = false, bool = false) {
    return false;
  }
  void end() {}
  uint8_t cardType() { return CARD_NONE; }
  uint64_t totalBytes() { return 0; }
  uint64_t usedBytes() { return 0; }
};

inline SDMMCFS SD_MMC;
