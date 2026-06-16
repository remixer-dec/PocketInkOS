#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdint.h>

struct SdStorageSnapshot {
  bool mounted = false;
  uint32_t totalGb = 0;
  uint32_t freeGb = 0;
};

void sdStorageBegin();
void sdStorageEnd();
bool sdStorageUpdate();
bool sdStorageMounted();
const SdStorageSnapshot &sdStorageSnapshot();

#endif
