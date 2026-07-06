#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdint.h>

/// Last known SD-card mount and capacity state.
struct SdStorageSnapshot {
  /// True when SD storage is mounted and usable.
  bool mounted = false;
  /// Total card capacity rounded to whole GiB for UI display.
  uint32_t totalGb = 0;
  /// Free card capacity rounded to whole GiB for UI display.
  uint32_t freeGb = 0;
};

/// Initializes SD storage support.
void sdStorageBegin();
/// Unmounts SD storage and releases related resources.
void sdStorageEnd();
/// Marks SD storage state stale so the next update probes the card.
void sdStorageRequestRefresh();
/// Refreshes mount and capacity state; returns true when the snapshot changed.
bool sdStorageUpdate();
/// Returns true when SD storage is currently mounted.
bool sdStorageMounted();
/// Returns the last cached SD storage snapshot.
const SdStorageSnapshot &currentSdStorageSnapshot();

#endif
