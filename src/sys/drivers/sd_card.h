#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>

/// Raw SD-card capacity reported by the board storage driver.
struct SdCardCapacity {
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
};

/// Mounts the board SD card in one-bit SD_MMC mode.
bool sdCardMount();
/// Unmounts the board SD card.
void sdCardUnmount();
/// Reports whether hardware still exposes a mounted card.
bool sdCardPresent();
/// Reads raw card capacity counters.
SdCardCapacity sdCardCapacity();

#endif
