/// Optional launcher metadata embedded in a .pink file header by the build
/// tool. Firmware that predates this extension safely ignores it and derives
/// the label and icon from the filename.
#ifndef SYS_PINK_APP_METADATA_H
#define SYS_PINK_APP_METADATA_H

#include <stddef.h>
#include <stdint.h>

static const uint32_t PINK_APP_METADATA_MAGIC = 0x444d4150UL; // "PAMD"
static const uint16_t PINK_APP_METADATA_VERSION = 1;
static const size_t PINK_APP_LABEL_CAPACITY = 16;
static const size_t PINK_APP_ICON_CAPACITY = 8;

struct PinkAppMetadata {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  char label[PINK_APP_LABEL_CAPACITY];
  char icon[PINK_APP_ICON_CAPACITY];
};

static_assert(sizeof(PinkAppMetadata) == 32,
              "Pink app metadata layout changed");

/// Declares metadata at global scope. The linker keeps this build-only section
/// out of the runtime image and the packer copies it into the file header.
#define PINK_APP_METADATA(labelValue, iconValue)                              \
  extern "C" __attribute__((section(".pink_app_metadata"), used)) const     \
      PinkAppMetadata pinkAppMetadata = {                                    \
          PINK_APP_METADATA_MAGIC, PINK_APP_METADATA_VERSION,                \
          sizeof(PinkAppMetadata), labelValue, iconValue}

/// Marks a launcher glyph for the system icon font without changing the
/// fixed-size metadata layout. The host strips the leading marker.
#define PINK_APP_METADATA_ICON_FONT(labelValue, iconValue)                    \
  PINK_APP_METADATA(labelValue, "@" iconValue)

#endif
