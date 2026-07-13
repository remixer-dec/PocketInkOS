#ifndef PINK_LIBRARY_API_H
#define PINK_LIBRARY_API_H

#include <stddef.h>
#include <stdint.h>

/// Additive metadata stored between PinkExecutableHeader and its relocations.
/// Normal .pink apps do not contain this structure.
static const uint32_t PINK_LIBRARY_MAGIC = 0x42494c50UL; // "PLIB"
static const uint16_t PINK_LIBRARY_MANIFEST_VERSION = 1;
static const uint32_t PINK_LIBRARY_TYPE_AUDIO_CODEC = 0x43445541UL; // "AUDC"
static const size_t PINK_LIBRARY_ID_CAPACITY = 24;
static const size_t PINK_LIBRARY_EXTENSIONS_CAPACITY = 48;

struct PinkLibraryManifest {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t type;
  uint32_t capabilities;
  char id[PINK_LIBRARY_ID_CAPACITY];
  /// Lower-case, comma-separated filename extensions without leading dots.
  char extensions[PINK_LIBRARY_EXTENSIONS_CAPACITY];
};

static_assert(sizeof(PinkLibraryManifest) == 88,
              "Pink library manifest layout changed");

#endif
