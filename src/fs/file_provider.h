#ifndef FILE_PROVIDER_H
#define FILE_PROVIDER_H

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

struct FileProviderCapacity {
  bool mounted = false;
  uint32_t totalGb = 0;
  uint32_t freeGb = 0;
};

struct FileProviderEntry {
  const char *name = nullptr;
  bool directory = false;
  uint32_t size = 0;
};

typedef bool (*FileProviderEntryHandler)(const FileProviderEntry &entry,
                                         void *context);

struct FileProvider {
  const char *id = nullptr;
  const char *label = nullptr;
  bool (*mounted)() = nullptr;
  FileProviderCapacity (*capacity)() = nullptr;
  File (*open)(const char *path) = nullptr;
  bool (*listDirectory)(const char *path, FileProviderEntryHandler handler,
                        void *context) = nullptr;
};

const FileProvider *fileProviderById(const char *id);
const FileProvider *fileProviderAt(size_t index);
size_t fileProviderCount();
const FileProvider *defaultFileProvider();
File openProviderPath(const FileProvider *provider, const char *path);
File openProviderPath(const char *providerId, const char *path);
bool listProviderDirectory(const FileProvider *provider, const char *path,
                           FileProviderEntryHandler handler, void *context);

#endif
