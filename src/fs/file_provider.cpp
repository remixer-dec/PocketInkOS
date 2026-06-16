#include "fs/file_provider.h"
#include "sys/sd_storage.h"

#include <SD_MMC.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace {

bool sdMounted() { return sdStorageMounted(); }

FileProviderCapacity sdCapacity() {
  const SdStorageSnapshot &sd = sdStorageSnapshot();
  FileProviderCapacity capacity;
  capacity.mounted = sd.mounted;
  capacity.totalGb = sd.totalGb;
  capacity.freeGb = sd.freeGb;
  return capacity;
}

File sdOpen(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return File();
  }
  return SD_MMC.open(path);
}

bool sdVfsPath(const char *path, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0 || path == nullptr || path[0] != '/') {
    return false;
  }
  const int written = snprintf(out, outSize, "/sdcard%s", path);
  return written >= 0 && static_cast<size_t>(written) < outSize;
}

bool sdListDirectory(const char *path, FileProviderEntryHandler handler,
                     void *context) {
  if (handler == nullptr) {
    return false;
  }

  char directoryPath[320];
  if (!sdVfsPath(path, directoryPath, sizeof(directoryPath))) {
    return false;
  }

  DIR *dir = opendir(directoryPath);
  if (dir == nullptr) {
    return false;
  }

  bool ok = true;
  while (ok) {
    dirent *raw = readdir(dir);
    if (raw == nullptr) {
      break;
    }
    if (raw->d_name[0] == '\0' || strcmp(raw->d_name, ".") == 0 ||
        strcmp(raw->d_name, "..") == 0) {
      continue;
    }

    char entryPath[576];
    const int written = snprintf(entryPath, sizeof(entryPath), "%s/%s",
                                 directoryPath, raw->d_name);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(entryPath)) {
      continue;
    }

    struct stat st;
    if (stat(entryPath, &st) != 0) {
      continue;
    }

    FileProviderEntry entry;
    entry.name = raw->d_name;
    entry.directory = S_ISDIR(st.st_mode);
    entry.size = entry.directory ? 0 : static_cast<uint32_t>(st.st_size);
    ok = handler(entry, context);
  }

  closedir(dir);
  return ok;
}

const FileProvider PROVIDERS[] = {
    {"sd", "SD CARD", sdMounted, sdCapacity, sdOpen, sdListDirectory},
};

const size_t PROVIDER_COUNT = sizeof(PROVIDERS) / sizeof(PROVIDERS[0]);

} // namespace

const FileProvider *fileProviderById(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return nullptr;
  }
  for (size_t i = 0; i < PROVIDER_COUNT; i++) {
    if (PROVIDERS[i].id != nullptr && strcmp(PROVIDERS[i].id, id) == 0) {
      return &PROVIDERS[i];
    }
  }
  return nullptr;
}

const FileProvider *fileProviderAt(size_t index) {
  if (index >= PROVIDER_COUNT) {
    return nullptr;
  }
  return &PROVIDERS[index];
}

size_t fileProviderCount() { return PROVIDER_COUNT; }

const FileProvider *defaultFileProvider() {
  return PROVIDER_COUNT > 0 ? &PROVIDERS[0] : nullptr;
}

File openProviderPath(const FileProvider *provider, const char *path) {
  if (provider == nullptr || provider->open == nullptr) {
    return File();
  }
  return provider->open(path);
}

File openProviderPath(const char *providerId, const char *path) {
  return openProviderPath(fileProviderById(providerId), path);
}

bool listProviderDirectory(const FileProvider *provider, const char *path,
                           FileProviderEntryHandler handler, void *context) {
  if (provider == nullptr || provider->listDirectory == nullptr) {
    return false;
  }
  return provider->listDirectory(path, handler, context);
}
