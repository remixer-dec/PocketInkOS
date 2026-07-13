#ifndef SYS_PINK_LIBRARY_H
#define SYS_PINK_LIBRARY_H

#include "sys/pink_executable.h"

#include <stddef.h>
#include <stdint.h>

static const uint32_t PINK_LIBRARY_MAGIC = 0x42494c50UL; // "PLIB"
static const uint16_t PINK_LIBRARY_MANIFEST_VERSION = 1;
static const uint32_t PINK_LIBRARY_TYPE_AUDIO_CODEC = 0x43445541UL; // "AUDC"
static const size_t PINK_LIBRARY_ID_CAPACITY = 24;
static const size_t PINK_LIBRARY_EXTENSIONS_CAPACITY = 48;

/// Typed metadata appended to the standard .pink header. `headerSize` covers
/// both structures, so the existing relocation and image layout stays intact.
struct PinkLibraryManifest {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t size = 0;
  uint32_t type = 0;
  uint32_t capabilities = 0;
  char id[PINK_LIBRARY_ID_CAPACITY] = {};
  char extensions[PINK_LIBRARY_EXTENSIONS_CAPACITY] = {};
};
static_assert(sizeof(PinkLibraryManifest) == 88,
              "Pink library manifest layout changed");

bool pinkLibraryReadManifest(const char *path, PinkLibraryManifest &manifest);

/// One loaded external library image. Instances do not participate in the
/// foreground .pink app session and may be used by built-in services.
class PinkLoadedLibrary {
public:
  PinkLoadedLibrary() = default;
  ~PinkLoadedLibrary();

  PinkLoadedLibrary(const PinkLoadedLibrary &) = delete;
  PinkLoadedLibrary &operator=(const PinkLoadedLibrary &) = delete;

  bool load(const char *path, uint32_t expectedType, char *error,
            size_t errorSize);
  void unload();
  void *entryPoint() const { return image.entryPoint; }
  const PinkLibraryManifest &libraryManifest() const { return manifest; }
  bool loaded() const { return image.entryPoint != nullptr; }

private:
  PinkExecutableImage image;
  PinkLibraryManifest manifest = {};
};

#endif
