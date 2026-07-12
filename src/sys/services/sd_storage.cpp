#include "sys/services/sd_storage.h"
#include "sys/drivers/sd_card.h"

#include <Arduino.h>

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
  const SdCardCapacity capacity = sdCardCapacity();
  const uint64_t total = capacity.totalBytes;
  const uint64_t used = capacity.usedBytes;
  const uint64_t free = used < total ? total - used : 0;
  next.totalGb = roundedGb(total);
  next.freeGb = roundedGb(free);
  return next;
}

bool snapshotsDiffer(const SdStorageSnapshot &a, const SdStorageSnapshot &b) {
  return a.mounted != b.mounted || a.totalGb != b.totalGb ||
         a.freeGb != b.freeGb;
}

bool refreshSnapshot() {
  SdStorageSnapshot next;
  if (snapshot.mounted) {
    if (sdCardPresent()) {
      next = readMountedSnapshot();
    } else {
      sdCardUnmount();
    }
  } else if (sdCardMount()) {
    next = readMountedSnapshot();
  } else {
    sdCardUnmount();
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
  sdCardUnmount();
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
