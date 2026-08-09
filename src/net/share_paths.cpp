#include "net/share_paths.h"

#include <SD_MMC.h>
#include <esp_random.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace net {
namespace {

std::atomic_flag shareSetupLock = ATOMIC_FLAG_INIT;

class ShareSetupGuard {
public:
  ShareSetupGuard() {
    while (shareSetupLock.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~ShareSetupGuard() { shareSetupLock.clear(std::memory_order_release); }
};

bool equalExtension(const char *value, const char *expected) {
  while (*value != '\0' && *expected != '\0') {
    char a = *value++;
    char b = *expected++;
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return *value == '\0' && *expected == '\0';
}

bool staleUploadName(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const char *hex = nullptr;
  if (strncmp(name, ".ftp-", 5U) == 0 && strlen(name) == 17U) {
    hex = name + 5U;
  } else if (strncmp(name, ".http-", 6U) == 0 && strlen(name) == 18U) {
    hex = name + 6U;
  } else {
    return false;
  }
  for (size_t i = 0; i < 8U; i++) {
    const char c = hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return strcmp(hex + 8U, ".tmp") == 0;
}

void cleanupStaleUploads() {
  File directory = SD_MMC.open(kShareRoot);
  if (!directory || !directory.isDirectory()) {
    directory.close();
    return;
  }
  for (File entry = directory.openNextFile(); entry;
       entry = directory.openNextFile()) {
    const char *path = entry.name();
    const char *slash = path != nullptr ? strrchr(path, '/') : nullptr;
    const char *name = slash != nullptr ? slash + 1U : path;
    const bool remove = !entry.isDirectory() && staleUploadName(name);
    char checkedPath[96] = {};
    if (remove) {
      const int written = snprintf(checkedPath, sizeof(checkedPath), "%s/%s",
                                   kShareRoot, name);
      if (written <= 0 || static_cast<size_t>(written) >= sizeof(checkedPath)) {
        checkedPath[0] = '\0';
      }
    }
    entry.close();
    if (checkedPath[0] != '\0') {
      SD_MMC.remove(checkedPath);
    }
  }
  directory.close();
}

} // namespace

bool validShareName(const char *name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '.') {
    return false;
  }
  const size_t length = strnlen(name, kShareNameCapacity);
  if (length == 0 || length >= kShareNameCapacity ||
      name[length - 1U] == '.') {
    return false;
  }
  const char *extension = nullptr;
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    const bool safe = (c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                      c == '.';
    if (!safe) {
      return false;
    }
    if (c == '.') {
      extension = name + i + 1U;
    }
  }
  if (extension == nullptr || extension[0] == '\0') {
    return false;
  }
  // Active browser content is deliberately excluded from the shared origin.
  static constexpr const char *kAllowed[] = {
      "jpg", "jpeg", "png", "webp", "gif", "pdf", "txt",
      "json", "csv", "zip",  "bin",  "epub", "wav", "mp3",
  };
  for (const char *allowed : kAllowed) {
    if (equalExtension(extension, allowed)) {
      return true;
    }
  }
  return false;
}

bool sharePath(const char *name, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0 || !validShareName(name)) {
    return false;
  }
  const int written = snprintf(out, outSize, "%s/%s", kShareRoot, name);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool temporaryShareUploadPath(ShareUploadKind kind, char *out,
                              size_t outSize) {
  if (out == nullptr || outSize == 0U) {
    return false;
  }
  const char *prefix = nullptr;
  switch (kind) {
  case ShareUploadKind::Ftp:
    prefix = ".ftp";
    break;
  case ShareUploadKind::Http:
    prefix = ".http";
    break;
  }
  if (prefix == nullptr) {
    out[0] = '\0';
    return false;
  }
  for (uint8_t attempt = 0; attempt < 8U; attempt++) {
    const unsigned long nonce = static_cast<unsigned long>(esp_random());
    const int written = snprintf(out, outSize, "%s/%s-%08lx.tmp", kShareRoot,
                                 prefix, nonce);
    if (written > 0 && static_cast<size_t>(written) < outSize &&
        !SD_MMC.exists(out)) {
      return true;
    }
  }
  out[0] = '\0';
  return false;
}

bool ensureShareRoot() {
  static bool cleanupAttempted = false;
  ShareSetupGuard guard;
  const bool ready = SD_MMC.exists(kShareRoot) || SD_MMC.mkdir(kShareRoot);
  if (ready && !cleanupAttempted) {
    cleanupAttempted = true;
    cleanupStaleUploads();
  }
  return ready;
}

} // namespace net
