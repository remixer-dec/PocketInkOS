#include "sys/sd_storage.h"
#include "sys/global.h"

#include <Arduino.h>
#include <SD_MMC.h>

namespace {

static const unsigned long kSdPollMs = 3000;
static const uint64_t kGb = 1024ULL * 1024ULL * 1024ULL;

SdStorageSnapshot snapshot;
unsigned long lastPollAt = 0;

uint32_t roundedGb(uint64_t bytes) {
  return static_cast<uint32_t>((bytes + kGb / 2) / kGb);
}

SdStorageSnapshot readMountedSnapshot() {
  SdStorageSnapshot next;
  next.mounted = true;
  const uint64_t total = SD_MMC.totalBytes();
  const uint64_t used = SD_MMC.usedBytes();
  const uint64_t free = used < total ? total - used : 0;
  next.totalGb = roundedGb(total);
  next.freeGb = roundedGb(free);
  return next;
}

bool snapshotsDiffer(const SdStorageSnapshot &a, const SdStorageSnapshot &b) {
  return a.mounted != b.mounted || a.totalGb != b.totalGb ||
         a.freeGb != b.freeGb;
}

bool mountCard() {
  SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN);
  if (!SD_MMC.begin("/sdcard", true)) {
    return false;
  }
  return SD_MMC.cardType() != CARD_NONE;
}

bool refreshSnapshot() {
  SdStorageSnapshot next;
  if (snapshot.mounted && SD_MMC.cardType() != CARD_NONE) {
    next = readMountedSnapshot();
  } else if (!snapshot.mounted && mountCard()) {
    next = readMountedSnapshot();
  } else {
    SD_MMC.end();
  }

  const bool changed = snapshotsDiffer(snapshot, next);
  snapshot = next;
  return changed;
}

} // namespace

void sdStorageBegin() {
  lastPollAt = millis();
  refreshSnapshot();
}

void sdStorageEnd() {
  SD_MMC.end();
  snapshot = SdStorageSnapshot{};
}

bool sdStorageUpdate() {
  const unsigned long now = millis();
  if (now - lastPollAt < kSdPollMs) {
    return false;
  }
  lastPollAt = now;
  return refreshSnapshot();
}

bool sdStorageMounted() { return snapshot.mounted; }

const SdStorageSnapshot &sdStorageSnapshot() { return snapshot; }
