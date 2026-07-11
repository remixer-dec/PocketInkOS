#include "sys/services/sd_storage.h"
#include "sys/global.h"

#include <Arduino.h>
#include <SD_MMC.h>

namespace {

static const unsigned long kSdMountedPollMs = 60000;
static const unsigned long kSdRetryInitialMs = 30000;
static const unsigned long kSdRetryMaxMs = 120000;
static const uint64_t kGb = 1024ULL * 1024ULL * 1024ULL;

SdStorageSnapshot snapshot;
unsigned long nextPollAt = 0;
unsigned long retryDelayMs = kSdRetryInitialMs;

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
  if (snapshot.mounted) {
    if (SD_MMC.cardType() != CARD_NONE) {
      next = readMountedSnapshot();
    } else {
      SD_MMC.end();
    }
  } else if (mountCard()) {
    next = readMountedSnapshot();
  } else {
    SD_MMC.end();
  }

  const bool changed = snapshotsDiffer(snapshot, next);
  snapshot = next;
  return changed;
}

bool pollDue(unsigned long now) {
  return static_cast<long>(now - nextPollAt) >= 0;
}

void scheduleNextPoll() {
  const unsigned long now = millis();
  if (snapshot.mounted) {
    retryDelayMs = kSdRetryInitialMs;
    nextPollAt = now + kSdMountedPollMs;
    return;
  }

  nextPollAt = now + retryDelayMs;
  if (retryDelayMs < kSdRetryMaxMs) {
    retryDelayMs *= 2;
    if (retryDelayMs > kSdRetryMaxMs) {
      retryDelayMs = kSdRetryMaxMs;
    }
  }
}

} // namespace

void sdStorageBegin() {
  nextPollAt = millis();
  retryDelayMs = kSdRetryInitialMs;
  refreshSnapshot();
  scheduleNextPoll();
}

void sdStorageEnd() {
  SD_MMC.end();
  snapshot = SdStorageSnapshot{};
  nextPollAt = 0;
  retryDelayMs = kSdRetryInitialMs;
}

void sdStorageRequestRefresh() {
  nextPollAt = millis();
}

bool sdStorageUpdate() {
  const unsigned long now = millis();
  if (!pollDue(now)) {
    return false;
  }
  const bool changed = refreshSnapshot();
  scheduleNextPoll();
  return changed;
}

bool sdStorageMounted() { return snapshot.mounted; }

const SdStorageSnapshot &currentSdStorageSnapshot() { return snapshot; }
