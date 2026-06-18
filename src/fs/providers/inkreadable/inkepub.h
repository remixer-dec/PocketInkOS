/*
 * InkEpub - small header-only EPUB text & image for memory-constrained readers.
 * Parses ZIP central directory, OCF/OPF metadata, and streams XHTML text & images.
 * (c) Remixer Dec 2026 | Licensed under CC BY-NC-SA 3.0
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */

#ifndef INKEPUB_H
#define INKEPUB_H

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "fs/providers/inkreadable/inktext_normalize.h"

#ifndef INKEPUB_MALLOC
#include <stdlib.h>
#define INKEPUB_MALLOC malloc
#define INKEPUB_FREE free
#endif

#ifndef INKEPUB_ZIP_BUFFER_SIZE
#define INKEPUB_ZIP_BUFFER_SIZE 8192U
#endif

#ifndef INKEPUB_MAX_XML_BYTES
#define INKEPUB_MAX_XML_BYTES (192U * 1024U)
#endif

#ifndef INKEPUB_MAX_PAGES
#define INKEPUB_MAX_PAGES 65535U
#endif

#ifndef INKEPUB_MAX_IMAGES
#define INKEPUB_MAX_IMAGES 65535U
#endif

enum class InkEpubResult : int8_t {
  Ok = 0,
  Done = 1,
  Io = -1,
  Format = -2,
  Unsupported = -3,
  Memory = -4,
};

enum class InkEpubPageKind : uint8_t {
  Text = 0,
  Image = 1,
};

enum class InkEpubRasterKind : uint8_t {
  Unknown = 0,
  Png = 1,
  Jpeg = 2,
  Webp = 3,
  Wbmp = 4,
};

struct InkEpubHooks {
  File (*openFile)(const char *providerId, const char *path) = nullptr;
  void *(*alloc)(size_t bytes) = nullptr;
  void (*free)(void *ptr) = nullptr;
  uint32_t (*micros)() = nullptr;
};

struct InkEpubBookInfo {
  uint16_t spineItems = 0;
};

struct InkEpubTextLine {
  const char *text = nullptr;
  uint8_t length = 0;
};

struct InkEpubScreenInfo {
  uint32_t screen = 0;
  uint16_t lineCount = 0;
  bool endReached = false;
};

struct InkEpubPageInfo {
  InkEpubPageKind kind = InkEpubPageKind::Text;
  InkEpubRasterKind rasterKind = InkEpubRasterKind::Unknown;
  uint32_t imageBytes = 0;
};

typedef void (*InkEpubLineHandler)(const InkEpubTextLine &line, void *context);

void inkEpubSetHooks(const InkEpubHooks &hooks);
InkEpubResult inkEpubOpen(const char *providerId, const char *path,
                          InkEpubBookInfo *info);
InkEpubResult inkEpubExtractScreenText(const char *providerId,
                                       const char *path, uint32_t screen,
                                       uint8_t columns, uint8_t rows,
                                       InkEpubLineHandler handler,
                                       void *context,
                                       InkEpubScreenInfo *info);
bool inkEpubLoading(const char *providerId, const char *path);
InkEpubResult inkEpubContinueIndex(const char *providerId, const char *path,
                                   uint8_t columns, uint8_t rows,
                                   uint32_t budgetUs);
uint8_t inkEpubProgress(const char *providerId, const char *path);
uint16_t inkEpubPageCount(const char *providerId, const char *path,
                          uint8_t columns, uint8_t rows);
InkEpubResult inkEpubPageInfo(const char *providerId, const char *path,
                              uint32_t page, InkEpubPageInfo *info);
InkEpubResult inkEpubLoadImagePage(const char *providerId, const char *path,
                                   uint32_t page, uint8_t **outData,
                                   size_t *outSize,
                                   InkEpubRasterKind *outKind);
void inkEpubFreeBuffer(void *ptr);

#ifdef INKEPUB_IMPLEMENTATION

#include <algorithm>
#include <string.h>

namespace inkepub_detail {

static const uint32_t kZipEocdSig = 0x06054b50UL;
static const uint32_t kZipCentralSig = 0x02014b50UL;
static const uint16_t kZipMethodStore = 0;
static const uint16_t kZipMethodDeflate = 8;
static const uint32_t kFnvOffset = 2166136261UL;
static const uint32_t kFnvPrime = 16777619UL;

struct HooksStore {
  InkEpubHooks hooks;
};

static HooksStore &hooksStore() {
  static HooksStore store;
  return store;
}

static void *allocMemory(size_t bytes) {
  InkEpubHooks &hooks = hooksStore().hooks;
  return hooks.alloc != nullptr ? hooks.alloc(bytes) : INKEPUB_MALLOC(bytes);
}

static void freeMemory(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  InkEpubHooks &hooks = hooksStore().hooks;
  if (hooks.free != nullptr) {
    hooks.free(ptr);
  } else {
    INKEPUB_FREE(ptr);
  }
}

static File openBookFile(const char *providerId, const char *path) {
  InkEpubHooks &hooks = hooksStore().hooks;
  return hooks.openFile != nullptr ? hooks.openFile(providerId, path) : File();
}

static uint32_t currentMicros() {
  InkEpubHooks &hooks = hooksStore().hooks;
  return hooks.micros != nullptr ? hooks.micros() : 0;
}

static uint8_t *scratchBuffer() {
  static uint8_t buffer[INKEPUB_ZIP_BUFFER_SIZE];
  return buffer;
}

static uint16_t readLe16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readLe32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

static uint32_t hashStep(uint32_t hash, char c) {
  return (hash ^ static_cast<uint8_t>(c == '\\' ? '/' : c)) * kFnvPrime;
}

static uint32_t hashPath(std::string_view path) {
  uint32_t hash = kFnvOffset;
  for (char c : path) {
    hash = hashStep(hash, c);
  }
  return hash;
}

static bool skipBytes(File &file, uint32_t bytes) {
  uint32_t chunk = bytes;
  uint8_t *buffer = scratchBuffer();
  while (chunk > 0) {
    const size_t want =
        chunk < INKEPUB_ZIP_BUFFER_SIZE ? chunk : INKEPUB_ZIP_BUFFER_SIZE;
    const size_t got = file.read(buffer, want);
    if (got != want) {
      return false;
    }
    chunk -= static_cast<uint32_t>(got);
  }
  return true;
}

struct ZipEntry {
  uint32_t hash = 0;
  uint32_t nameLen = 0;
  uint32_t dataOffset = 0;
  uint32_t compressedSize = 0;
  uint32_t uncompressedSize = 0;
  uint16_t method = 0;
  bool used = false;
};

struct ZipIndex {
  std::vector<ZipEntry> table;
  uint32_t mask = 0;

  bool init(uint16_t count) {
    uint32_t capacity = 1;
    const uint32_t wanted = static_cast<uint32_t>(count) * 2U + 1U;
    while (capacity < wanted && capacity < 32768U) {
      capacity <<= 1U;
    }
    if (capacity < wanted) {
      return false;
    }
    table.clear();
    table.resize(capacity);
    mask = capacity - 1U;
    return true;
  }

  bool insert(const ZipEntry &entry) {
    if (table.empty()) {
      return false;
    }
    uint32_t slot = entry.hash & mask;
    for (uint32_t i = 0; i <= mask; i++) {
      ZipEntry &target = table[slot];
      if (!target.used) {
        target = entry;
        target.used = true;
        return true;
      }
      slot = (slot + 1U) & mask;
    }
    return false;
  }

  const ZipEntry *find(std::string_view path) const {
    if (table.empty()) {
      return nullptr;
    }
    const uint32_t hash = hashPath(path);
    const uint32_t len = static_cast<uint32_t>(path.size());
    uint32_t slot = hash & mask;
    for (uint32_t i = 0; i <= mask; i++) {
      const ZipEntry &entry = table[slot];
      if (!entry.used) {
        return nullptr;
      }
      if (entry.hash == hash && entry.nameLen == len) {
        return &entry;
      }
      slot = (slot + 1U) & mask;
    }
    return nullptr;
  }
};

static bool readExactAt(File &file, uint32_t offset, uint8_t *out,
                        size_t size) {
  return file.seek(offset) && file.read(out, size) == size;
}

static bool findEocd(File &file, uint32_t fileSize, uint32_t &outOffset) {
  if (fileSize < 22U) {
    return false;
  }
  const uint32_t scanStart =
      fileSize > 65557U ? fileSize - 65557U : 0U;
  if (!file.seek(scanStart)) {
    return false;
  }

  uint32_t rolling = 0;
  uint32_t offset = scanStart;
  uint8_t *buffer = scratchBuffer();
  bool found = false;
  while (offset < fileSize) {
    const uint32_t remaining = fileSize - offset;
    const size_t want = remaining < INKEPUB_ZIP_BUFFER_SIZE
                            ? remaining
                            : INKEPUB_ZIP_BUFFER_SIZE;
    const size_t got = file.read(buffer, want);
    if (got == 0) {
      break;
    }
    for (size_t i = 0; i < got; i++) {
      rolling = (rolling >> 8) | (static_cast<uint32_t>(buffer[i]) << 24);
      if (rolling == kZipEocdSig && offset + i >= 3U) {
        outOffset = offset + static_cast<uint32_t>(i) - 3U;
        found = true;
      }
    }
    offset += static_cast<uint32_t>(got);
  }
  return found;
}

static InkEpubResult buildZipIndex(File &file, ZipIndex &index) {
  const size_t rawFileSize = file.size();
  if (rawFileSize > 0xffffffffULL) {
    return InkEpubResult::Unsupported;
  }
  const uint32_t fileSize = static_cast<uint32_t>(rawFileSize);
  uint32_t eocdOffset = 0;
  uint8_t fixed[46];
  if (!findEocd(file, fileSize, eocdOffset) ||
      !readExactAt(file, eocdOffset, fixed, 22)) {
    return InkEpubResult::Format;
  }
  if (readLe32(fixed) != kZipEocdSig || readLe16(fixed + 4) != 0 ||
      readLe16(fixed + 6) != 0 ||
      readLe16(fixed + 8) != readLe16(fixed + 10)) {
    return InkEpubResult::Unsupported;
  }

  const uint16_t entryCount = readLe16(fixed + 10);
  const uint32_t cdSize = readLe32(fixed + 12);
  const uint32_t cdOffset = readLe32(fixed + 16);
  if (cdOffset > fileSize || cdSize > fileSize - cdOffset ||
      !index.init(entryCount) || !file.seek(cdOffset)) {
    return InkEpubResult::Format;
  }

  uint32_t cursor = cdOffset;
  uint8_t *buffer = scratchBuffer();
  for (uint16_t i = 0; i < entryCount; i++) {
    if (file.read(fixed, sizeof(fixed)) != sizeof(fixed) ||
        readLe32(fixed) != kZipCentralSig) {
      return InkEpubResult::Format;
    }
    const uint16_t flags = readLe16(fixed + 8);
    const uint16_t method = readLe16(fixed + 10);
    const uint32_t compressedSize = readLe32(fixed + 20);
    const uint32_t uncompressedSize = readLe32(fixed + 24);
    const uint16_t nameLen = readLe16(fixed + 28);
    const uint16_t extraLen = readLe16(fixed + 30);
    const uint16_t commentLen = readLe16(fixed + 32);
    const uint32_t localOffset = readLe32(fixed + 42);
    if ((flags & 1U) != 0 || localOffset > fileSize ||
        compressedSize > fileSize) {
      return InkEpubResult::Unsupported;
    }

    uint32_t hash = kFnvOffset;
    uint16_t remainingName = nameLen;
    bool directory = false;
    while (remainingName > 0) {
      const size_t want = remainingName < INKEPUB_ZIP_BUFFER_SIZE
                              ? remainingName
                              : INKEPUB_ZIP_BUFFER_SIZE;
      const size_t got = file.read(buffer, want);
      if (got != want) {
        return InkEpubResult::Format;
      }
      for (size_t j = 0; j < got; j++) {
        hash = hashStep(hash, static_cast<char>(buffer[j]));
        directory = (j + 1U == got && remainingName == got && buffer[j] == '/');
      }
      remainingName = static_cast<uint16_t>(remainingName - got);
    }

    uint8_t local[30];
    if (!readExactAt(file, localOffset, local, sizeof(local)) ||
        readLe32(local) != 0x04034b50UL) {
      return InkEpubResult::Format;
    }
    const uint16_t localNameLen = readLe16(local + 26);
    const uint16_t localExtraLen = readLe16(local + 28);
    const uint64_t dataOffset64 =
        static_cast<uint64_t>(localOffset) + 30ULL + localNameLen +
        localExtraLen;
    if (dataOffset64 > 0xffffffffULL) {
      return InkEpubResult::Format;
    }
    const uint32_t dataOffset = static_cast<uint32_t>(dataOffset64);
    if (!directory && dataOffset <= fileSize &&
        compressedSize <= fileSize - dataOffset &&
        (method == kZipMethodStore || method == kZipMethodDeflate)) {
      ZipEntry entry;
      entry.hash = hash;
      entry.nameLen = nameLen;
      entry.dataOffset = dataOffset;
      entry.compressedSize = compressedSize;
      entry.uncompressedSize = uncompressedSize;
      entry.method = method;
      if (!index.insert(entry)) {
        return InkEpubResult::Memory;
      }
    }
    const uint32_t nextCentral =
        cursor + 46U + static_cast<uint32_t>(nameLen) + extraLen + commentLen;
    if (!file.seek(nextCentral)) {
      return InkEpubResult::Format;
    }
    cursor = nextCentral;
    if (cursor > cdOffset + cdSize) {
      return InkEpubResult::Format;
    }
  }
  return InkEpubResult::Ok;
}

typedef bool (*ByteOutput)(uint8_t byte, void *user);

struct ZipDataReader {
  File *file = nullptr;
  uint32_t remaining = 0;
  uint8_t *buffer = nullptr;
  size_t pos = 0;
  size_t have = 0;
};

static int zipDataByte(ZipDataReader *reader) {
  if (reader == nullptr || reader->file == nullptr || reader->buffer == nullptr) {
    return -1;
  }
  if (reader->pos >= reader->have) {
    if (reader->remaining == 0) {
      return -1;
    }
    const size_t want = reader->remaining < INKEPUB_ZIP_BUFFER_SIZE
                            ? reader->remaining
                            : INKEPUB_ZIP_BUFFER_SIZE;
    reader->have = reader->file->read(reader->buffer, want);
    reader->pos = 0;
    if (reader->have == 0) {
      return -1;
    }
    reader->remaining -= static_cast<uint32_t>(reader->have);
  }
  return reader->buffer[reader->pos++];
}

struct HuffTree {
  int16_t left[576];
  int16_t right[576];
  int16_t sym[576];
  uint16_t nodes = 0;
};

struct InflateState {
  ZipDataReader *reader = nullptr;
  ByteOutput output = nullptr;
  void *outputUser = nullptr;
  uint32_t bits = 0;
  uint8_t bitCount = 0;
  uint8_t *window = nullptr;
  uint16_t winPos = 0;
  uint32_t outCount = 0;
};

static void huffReset(HuffTree *tree) {
  tree->nodes = 1;
  tree->left[0] = tree->right[0] = -1;
  tree->sym[0] = -1;
}

static bool huffInsert(HuffTree *tree, uint16_t code, uint8_t len,
                       uint16_t sym) {
  uint16_t node = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (tree->sym[node] >= 0) {
      return false;
    }
    int16_t *child =
        ((code >> static_cast<uint8_t>(len - 1U - i)) & 1U)
            ? &tree->right[node]
            : &tree->left[node];
    if (*child < 0) {
      if (tree->nodes >= 576) {
        return false;
      }
      *child = static_cast<int16_t>(tree->nodes);
      tree->left[tree->nodes] = tree->right[tree->nodes] = -1;
      tree->sym[tree->nodes] = -1;
      tree->nodes++;
    }
    node = static_cast<uint16_t>(*child);
  }
  if (tree->sym[node] >= 0 || tree->left[node] >= 0 ||
      tree->right[node] >= 0) {
    return false;
  }
  tree->sym[node] = static_cast<int16_t>(sym);
  return true;
}

static bool huffBuild(HuffTree *tree, const uint8_t *lengths, uint16_t count) {
  uint16_t blCount[16] = {0};
  uint16_t nextCode[16] = {0};
  uint16_t code = 0;
  int32_t left = 1;
  huffReset(tree);
  for (uint16_t i = 0; i < count; i++) {
    if (lengths[i] > 15) {
      return false;
    }
    blCount[lengths[i]]++;
  }
  blCount[0] = 0;
  for (uint8_t bits = 1; bits <= 15; bits++) {
    left = (left << 1) - blCount[bits];
    if (left < 0) {
      return false;
    }
    code = static_cast<uint16_t>((code + blCount[bits - 1U]) << 1);
    nextCode[bits] = code;
  }
  for (uint16_t n = 0; n < count; n++) {
    const uint8_t len = lengths[n];
    if (len != 0 && !huffInsert(tree, nextCode[len]++, len, n)) {
      return false;
    }
  }
  return true;
}

static int inflateBits(InflateState *state, uint8_t count) {
  while (state->bitCount < count) {
    const int c = zipDataByte(state->reader);
    if (c < 0) {
      return -1;
    }
    state->bits |= static_cast<uint32_t>(static_cast<uint8_t>(c))
                   << state->bitCount;
    state->bitCount = static_cast<uint8_t>(state->bitCount + 8U);
  }
  const int out = static_cast<int>(state->bits & ((1UL << count) - 1UL));
  state->bits >>= count;
  state->bitCount = static_cast<uint8_t>(state->bitCount - count);
  return out;
}

static int huffDecode(InflateState *state, const HuffTree *tree) {
  int16_t node = 0;
  while (node >= 0 && tree->sym[node] < 0) {
    const int bit = inflateBits(state, 1);
    if (bit < 0) {
      return -1;
    }
    node = bit ? tree->right[node] : tree->left[node];
  }
  return node >= 0 ? tree->sym[node] : -1;
}

static InkEpubResult inflateEmit(InflateState *state, uint8_t byte) {
  state->window[state->winPos] = byte;
  state->winPos = static_cast<uint16_t>((state->winPos + 1U) & 32767U);
  if (state->outCount < 32768U) {
    state->outCount++;
  }
  return state->output == nullptr || state->output(byte, state->outputUser)
             ? InkEpubResult::Ok
             : InkEpubResult::Done;
}

static InkEpubResult inflateCodes(InflateState *state, const HuffTree *lit,
                                  const HuffTree *dist) {
  static const uint16_t lenBase[29] = {
      3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
      15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
      67,  83,  99,  115, 131, 163, 195, 227, 258};
  static const uint8_t lenExtra[29] = {
      0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t distBase[30] = {
      1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t distExtra[30] = {
      0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4,  4,  5,  5,  6,
      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  for (;;) {
    const int sym = huffDecode(state, lit);
    if (sym < 0) {
      return InkEpubResult::Format;
    }
    if (sym < 256) {
      const InkEpubResult r = inflateEmit(state, static_cast<uint8_t>(sym));
      if (r != InkEpubResult::Ok) {
        return r;
      }
      continue;
    }
    if (sym == 256) {
      return InkEpubResult::Ok;
    }
    if (sym > 285) {
      return InkEpubResult::Format;
    }

    const uint8_t li = static_cast<uint8_t>(sym - 257);
    int length = lenBase[li];
    if (lenExtra[li] != 0) {
      const int extra = inflateBits(state, lenExtra[li]);
      if (extra < 0) {
        return InkEpubResult::Format;
      }
      length += extra;
    }

    const int dsym = huffDecode(state, dist);
    if (dsym < 0 || dsym >= 30) {
      return InkEpubResult::Format;
    }
    int distance = distBase[dsym];
    if (distExtra[dsym] != 0) {
      const int extra = inflateBits(state, distExtra[dsym]);
      if (extra < 0) {
        return InkEpubResult::Format;
      }
      distance += extra;
    }
    if (distance <= 0 || static_cast<uint32_t>(distance) > state->outCount ||
        distance > 32768) {
      return InkEpubResult::Format;
    }

    while (length-- > 0) {
      const uint8_t b =
          state->window[static_cast<uint16_t>(state->winPos - distance) &
                        32767U];
      const InkEpubResult r = inflateEmit(state, b);
      if (r != InkEpubResult::Ok) {
        return r;
      }
    }
  }
}

static bool fixedTrees(HuffTree *lit, HuffTree *dist) {
  uint8_t ll[288];
  uint8_t dd[32];
  for (uint16_t i = 0; i < 288; i++) {
    ll[i] = i <= 143 ? 8 : (i <= 255 ? 9 : (i <= 279 ? 7 : 8));
  }
  for (uint8_t i = 0; i < 32; i++) {
    dd[i] = 5;
  }
  return huffBuild(lit, ll, 288) && huffBuild(dist, dd, 32);
}

static InkEpubResult dynamicTrees(InflateState *state, HuffTree *lit,
                                  HuffTree *dist) {
  static const uint8_t order[19] = {16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
  uint8_t clen[19] = {0};
  uint8_t lengths[320];
  HuffTree *codeTree = static_cast<HuffTree *>(allocMemory(sizeof(HuffTree)));
  if (codeTree == nullptr) {
    return InkEpubResult::Memory;
  }
#define INKEPUB_DYNAMIC_RETURN(value)                                          \
  do {                                                                         \
    freeMemory(codeTree);                                                      \
    return (value);                                                            \
  } while (0)
  const int hlit = inflateBits(state, 5);
  const int hdist = inflateBits(state, 5);
  const int hclen = inflateBits(state, 4);
  if (hlit < 0 || hdist < 0 || hclen < 0) {
    INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
  }
  const uint16_t litCount = static_cast<uint16_t>(hlit + 257);
  const uint16_t distCount = static_cast<uint16_t>(hdist + 1);
  const uint16_t total = static_cast<uint16_t>(litCount + distCount);
  for (uint8_t i = 0; i < static_cast<uint8_t>(hclen + 4); i++) {
    const int v = inflateBits(state, 3);
    if (v < 0) {
      INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
    }
    clen[order[i]] = static_cast<uint8_t>(v);
  }
  if (!huffBuild(codeTree, clen, 19)) {
    INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
  }
  for (uint16_t i = 0; i < total;) {
    const int sym = huffDecode(state, codeTree);
    uint8_t value = 0;
    uint16_t repeat = 0;
    if (sym < 0) {
      INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
    }
    if (sym <= 15) {
      lengths[i++] = static_cast<uint8_t>(sym);
      continue;
    }
    if (sym == 16) {
      const int extra = inflateBits(state, 2);
      if (i == 0 || extra < 0) {
        INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
      }
      value = lengths[i - 1U];
      repeat = static_cast<uint16_t>(extra + 3);
    } else if (sym == 17) {
      const int extra = inflateBits(state, 3);
      if (extra < 0) {
        INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
      }
      repeat = static_cast<uint16_t>(extra + 3);
    } else {
      const int extra = inflateBits(state, 7);
      if (extra < 0) {
        INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
      }
      repeat = static_cast<uint16_t>(extra + 11);
    }
    if (repeat > total - i) {
      INKEPUB_DYNAMIC_RETURN(InkEpubResult::Format);
    }
    while (repeat-- > 0 && i < total) {
      lengths[i++] = value;
    }
  }
  INKEPUB_DYNAMIC_RETURN(huffBuild(lit, lengths, litCount) &&
                                 huffBuild(dist, lengths + litCount, distCount)
                             ? InkEpubResult::Ok
                             : InkEpubResult::Format);
#undef INKEPUB_DYNAMIC_RETURN
}

static InkEpubResult inflateStored(InflateState *state) {
  state->bits = 0;
  state->bitCount = 0;
  const int l0 = zipDataByte(state->reader);
  const int l1 = zipDataByte(state->reader);
  const int n0 = zipDataByte(state->reader);
  const int n1 = zipDataByte(state->reader);
  if (l0 < 0 || l1 < 0 || n0 < 0 || n1 < 0) {
    return InkEpubResult::Format;
  }
  const uint16_t len = static_cast<uint16_t>(l0 | (l1 << 8));
  const uint16_t nlen = static_cast<uint16_t>(n0 | (n1 << 8));
  if (static_cast<uint16_t>(~len) != nlen) {
    return InkEpubResult::Format;
  }
  for (uint16_t i = 0; i < len; i++) {
    const int b = zipDataByte(state->reader);
    if (b < 0) {
      return InkEpubResult::Format;
    }
    const InkEpubResult r = inflateEmit(state, static_cast<uint8_t>(b));
    if (r != InkEpubResult::Ok) {
      return r;
    }
  }
  return InkEpubResult::Ok;
}

static InkEpubResult inflateRaw(ZipDataReader *reader, ByteOutput output,
                                void *outputUser) {
  InflateState state;
  state.reader = reader;
  state.output = output;
  state.outputUser = outputUser;
  state.window = static_cast<uint8_t *>(allocMemory(32768));
  HuffTree *lit = static_cast<HuffTree *>(allocMemory(sizeof(HuffTree)));
  HuffTree *dist = static_cast<HuffTree *>(allocMemory(sizeof(HuffTree)));
  if (state.window == nullptr || lit == nullptr || dist == nullptr) {
    freeMemory(state.window);
    freeMemory(lit);
    freeMemory(dist);
    return InkEpubResult::Memory;
  }
#define INKEPUB_INFLATE_RETURN(value)                                          \
  do {                                                                         \
    freeMemory(state.window);                                                  \
    freeMemory(lit);                                                           \
    freeMemory(dist);                                                          \
    return (value);                                                            \
  } while (0)
  for (;;) {
    const int final = inflateBits(&state, 1);
    const int type = inflateBits(&state, 2);
    InkEpubResult result = InkEpubResult::Ok;
    if (final < 0 || type < 0 || type == 3) {
      INKEPUB_INFLATE_RETURN(InkEpubResult::Format);
    }
    if (type == 0) {
      result = inflateStored(&state);
    } else {
      if (type == 1) {
        if (!fixedTrees(lit, dist)) {
          INKEPUB_INFLATE_RETURN(InkEpubResult::Format);
        }
      } else {
        result = dynamicTrees(&state, lit, dist);
      }
      if (result == InkEpubResult::Ok) {
        result = inflateCodes(&state, lit, dist);
      }
    }
    if (result != InkEpubResult::Ok) {
      INKEPUB_INFLATE_RETURN(result);
    }
    if (final) {
      INKEPUB_INFLATE_RETURN(InkEpubResult::Ok);
    }
  }
#undef INKEPUB_INFLATE_RETURN
}

static InkEpubResult streamZipEntry(File &file, const ZipEntry &entry,
                                    ByteOutput output, void *outputUser) {
  if (!file.seek(entry.dataOffset)) {
    return InkEpubResult::Io;
  }

  ZipDataReader reader;
  reader.file = &file;
  reader.remaining = entry.compressedSize;
  reader.buffer = scratchBuffer();

  if (entry.method == kZipMethodStore) {
    while (reader.remaining > 0) {
      const int b = zipDataByte(&reader);
      if (b < 0) {
        return InkEpubResult::Format;
      }
      if (output != nullptr && !output(static_cast<uint8_t>(b), outputUser)) {
        return InkEpubResult::Done;
      }
    }
    return InkEpubResult::Ok;
  }
  if (entry.method == kZipMethodDeflate) {
    return inflateRaw(&reader, output, outputUser);
  }
  return InkEpubResult::Unsupported;
}

struct StringLoad {
  std::string *text = nullptr;
  uint32_t maxBytes = 0;
  InkEpubResult result = InkEpubResult::Ok;
};

static bool appendToString(uint8_t byte, void *user) {
  StringLoad *load = static_cast<StringLoad *>(user);
  if (load == nullptr || load->text == nullptr) {
    return false;
  }
  if (load->text->size() >= load->maxBytes) {
    load->result = InkEpubResult::Memory;
    return false;
  }
  load->text->push_back(static_cast<char>(byte));
  return true;
}

static InkEpubResult loadZipText(File &file, const ZipIndex &index,
                                 std::string_view zipPath, uint32_t maxBytes,
                                 std::string &out) {
  const ZipEntry *entry = index.find(zipPath);
  if (entry == nullptr) {
    return InkEpubResult::Format;
  }
  out.clear();
  if (entry->uncompressedSize > maxBytes) {
    return InkEpubResult::Memory;
  }
  StringLoad load;
  load.text = &out;
  load.maxBytes = maxBytes;
  const InkEpubResult result = streamZipEntry(file, *entry, appendToString, &load);
  if (result == InkEpubResult::Done) {
    return load.result;
  }
  return result == InkEpubResult::Ok ? load.result : result;
}

static bool startsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

static std::string_view findAttribute(std::string_view tag,
                                      std::string_view name) {
  size_t pos = 0;
  while (pos < tag.size()) {
    pos = tag.find(name, pos);
    if (pos == std::string_view::npos) {
      return {};
    }
    const bool boundary =
        pos == 0 || tag[pos - 1] == ' ' || tag[pos - 1] == '\t' ||
        tag[pos - 1] == '\n' || tag[pos - 1] == '\r';
    const size_t valuePos = pos + name.size();
    if (boundary && valuePos + 1 < tag.size() && tag[valuePos] == '=' &&
        (tag[valuePos + 1] == '"' || tag[valuePos + 1] == '\'')) {
      const char quote = tag[valuePos + 1];
      const size_t start = valuePos + 2;
      const size_t end = tag.find(quote, start);
      return end == std::string_view::npos ? std::string_view{}
                                           : tag.substr(start, end - start);
    }
    pos += name.size();
  }
  return {};
}

static void appendXmlAttribute(std::string &out, std::string_view value) {
  out.clear();
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] == '&') {
      if (startsWith(value.substr(i), "&amp;")) {
        out.push_back('&');
        i += 4;
      } else if (startsWith(value.substr(i), "&lt;")) {
        out.push_back('<');
        i += 3;
      } else if (startsWith(value.substr(i), "&gt;")) {
        out.push_back('>');
        i += 3;
      } else if (startsWith(value.substr(i), "&nbsp;")) {
        out.push_back(' ');
        i += 5;
      } else {
        out.push_back('&');
      }
    } else {
      out.push_back(value[i] == '\\' ? '/' : value[i]);
    }
  }
}

static bool extractContainerRootfile(std::string_view xml, std::string &out) {
  size_t pos = 0;
  while ((pos = xml.find("<rootfile", pos)) != std::string_view::npos) {
    const size_t end = xml.find('>', pos);
    if (end == std::string_view::npos) {
      return false;
    }
    const std::string_view value =
        findAttribute(xml.substr(pos, end - pos), "full-path");
    if (!value.empty()) {
      appendXmlAttribute(out, value);
      return !out.empty();
    }
    pos = end + 1;
  }
  return false;
}

static std::string baseDirectory(std::string_view path) {
  const size_t slash = path.rfind('/');
  return slash == std::string_view::npos ? std::string()
                                         : std::string(path.substr(0, slash + 1));
}

static std::string normalizeZipPath(std::string_view base,
                                    std::string_view href) {
  const size_t fragment = href.find('#');
  href = href.substr(0, fragment);
  std::string combined;
  if (!href.empty() && href[0] == '/') {
    combined.assign(href.substr(1));
  } else {
    combined.reserve(base.size() + href.size());
    combined.append(base);
    combined.append(href);
  }

  std::string out;
  out.reserve(combined.size());
  size_t pos = 0;
  while (pos <= combined.size()) {
    const size_t next = combined.find('/', pos);
    const size_t end = next == std::string::npos ? combined.size() : next;
    const std::string_view part(combined.data() + pos, end - pos);
    if (part.empty() || part == ".") {
    } else if (part == "..") {
      if (!out.empty()) {
        const size_t slash = out.rfind('/');
        out.erase(slash == std::string::npos ? 0 : slash);
      }
    } else {
      if (!out.empty()) {
        out.push_back('/');
      }
      for (char c : part) {
        out.push_back(c == '\\' ? '/' : c);
      }
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }
  return out;
}

struct ManifestItem {
  uint32_t idHash = 0;
  uint32_t idLen = 0;
  std::string href;
  InkEpubRasterKind rasterKind = InkEpubRasterKind::Unknown;
};

struct PageRef {
  InkEpubPageKind kind = InkEpubPageKind::Text;
  uint32_t spineIndex = 0;
  uint32_t lineStart = 0;
  uint16_t imageIndex = 0;
};

struct ImageRef {
  std::string path;
  InkEpubRasterKind rasterKind = InkEpubRasterKind::Unknown;
};

static bool iequalsAscii(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); i++) {
    const char a = left[i] >= 'A' && left[i] <= 'Z'
                       ? static_cast<char>(left[i] - 'A' + 'a')
                       : left[i];
    const char b = right[i] >= 'A' && right[i] <= 'Z'
                       ? static_cast<char>(right[i] - 'A' + 'a')
                       : right[i];
    if (a != b) {
      return false;
    }
  }
  return true;
}

static bool endsWithAscii(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         iequalsAscii(text.substr(text.size() - suffix.size()), suffix);
}

static InkEpubRasterKind rasterKindForMedia(std::string_view mediaType,
                                            std::string_view path) {
  if (iequalsAscii(mediaType, "image/jpeg") ||
      iequalsAscii(mediaType, "image/jpg")) {
    return InkEpubRasterKind::Jpeg;
  }
  if (iequalsAscii(mediaType, "image/png")) {
    return InkEpubRasterKind::Png;
  }
  if (iequalsAscii(mediaType, "image/webp")) {
    return InkEpubRasterKind::Webp;
  }
  if (iequalsAscii(mediaType, "image/vnd.wap.wbmp") ||
      iequalsAscii(mediaType, "image/wbmp")) {
    return InkEpubRasterKind::Wbmp;
  }
  if (endsWithAscii(path, ".jpg") || endsWithAscii(path, ".jpeg") ||
      endsWithAscii(path, ".jfif")) {
    return InkEpubRasterKind::Jpeg;
  }
  if (endsWithAscii(path, ".png")) {
    return InkEpubRasterKind::Png;
  }
  if (endsWithAscii(path, ".webp")) {
    return InkEpubRasterKind::Webp;
  }
  if (endsWithAscii(path, ".wbmp")) {
    return InkEpubRasterKind::Wbmp;
  }
  return InkEpubRasterKind::Unknown;
}

static bool extensionLooksReadable(std::string_view path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string_view::npos) {
    return true;
  }
  const std::string_view ext = path.substr(dot + 1);
  return ext == "xhtml" || ext == "html" || ext == "htm" || ext == "xml";
}

static bool parseOpfSpine(std::string_view xml, std::string_view opfPath,
                          std::vector<std::string> &spine,
                          std::vector<ManifestItem> &manifest) {
  const std::string base = baseDirectory(opfPath);
  std::string id;
  std::string href;
  std::string mediaType;
  size_t pos = 0;
  manifest.clear();

  while ((pos = xml.find("<item ", pos)) != std::string_view::npos) {
    const size_t end = xml.find('>', pos);
    if (end == std::string_view::npos) {
      return false;
    }
    const std::string_view tag = xml.substr(pos, end - pos);
    const std::string_view idAttr = findAttribute(tag, "id");
    const std::string_view hrefAttr = findAttribute(tag, "href");
    const std::string_view mediaAttr = findAttribute(tag, "media-type");
    if (!idAttr.empty() && !hrefAttr.empty()) {
      appendXmlAttribute(id, idAttr);
      appendXmlAttribute(href, hrefAttr);
      appendXmlAttribute(mediaType, mediaAttr);
      if (!id.empty() && !href.empty()) {
        ManifestItem item;
        item.idHash = hashPath(id);
        item.idLen = static_cast<uint32_t>(id.size());
        item.href = normalizeZipPath(base, href);
        item.rasterKind = rasterKindForMedia(mediaType, item.href);
        manifest.push_back(item);
      }
    }
    pos = end + 1;
  }

  pos = 0;
  while ((pos = xml.find("<itemref ", pos)) != std::string_view::npos) {
    const size_t end = xml.find('>', pos);
    if (end == std::string_view::npos) {
      return false;
    }
    const std::string_view idrefAttr =
        findAttribute(xml.substr(pos, end - pos), "idref");
    if (!idrefAttr.empty()) {
      appendXmlAttribute(id, idrefAttr);
      const uint32_t idHash = hashPath(id);
      const uint32_t idLen = static_cast<uint32_t>(id.size());
      for (const ManifestItem &item : manifest) {
        if (item.idHash == idHash && item.idLen == idLen &&
            extensionLooksReadable(item.href)) {
          spine.push_back(item.href);
          break;
        }
      }
    }
    pos = end + 1;
  }
  return !spine.empty();
}

static InkEpubResult loadSpine(File &file, const ZipIndex &index,
                               std::vector<std::string> &spine,
                               std::vector<ManifestItem> &manifest) {
  std::string xml;
  std::string opfPath;
  InkEpubResult result =
      loadZipText(file, index, "META-INF/container.xml", 8192U, xml);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (!extractContainerRootfile(xml, opfPath)) {
    return InkEpubResult::Format;
  }
  result = loadZipText(file, index, opfPath, INKEPUB_MAX_XML_BYTES, xml);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  spine.clear();
  return parseOpfSpine(xml, opfPath, spine, manifest) ? InkEpubResult::Ok
                                                      : InkEpubResult::Format;
}

struct BookCache {
  std::string providerId;
  std::string path;
  ZipIndex index;
  std::vector<std::string> spine;
  std::vector<ManifestItem> manifest;
  std::vector<PageRef> pages;
  std::vector<ImageRef> images;
  uint32_t nextSpineIndex = 0;
  uint8_t pageColumns = 0;
  uint8_t pageRows = 0;
  InkEpubResult pageError = InkEpubResult::Ok;
  uint8_t transientIoFailures = 0;
  bool pagesComplete = false;
  bool valid = false;
};

static BookCache &bookCache() {
  static BookCache cache;
  return cache;
}

static bool sameBook(const BookCache &cache, const char *providerId,
                     const char *path) {
  return cache.valid && providerId != nullptr && path != nullptr &&
         cache.providerId == providerId && cache.path == path;
}

static bool sameBookPath(const BookCache &cache, const char *providerId,
                         const char *path) {
  return providerId != nullptr && path != nullptr &&
         cache.providerId == providerId && cache.path == path;
}

static void resetBookCache(BookCache &cache, const char *providerId,
                           const char *path) {
  cache.valid = false;
  cache.providerId = providerId != nullptr ? providerId : "";
  cache.path = path != nullptr ? path : "";
  cache.index.table.clear();
  cache.index.mask = 0;
  cache.spine.clear();
  cache.manifest.clear();
  cache.pages.clear();
  cache.images.clear();
  cache.nextSpineIndex = 0;
  cache.pageColumns = 0;
  cache.pageRows = 0;
  cache.pageError = InkEpubResult::Ok;
  cache.transientIoFailures = 0;
  cache.pagesComplete = false;
}

static InkEpubResult noteIoFailure(BookCache &cache) {
  if (cache.transientIoFailures < 255) {
    cache.transientIoFailures++;
  }
  if (cache.transientIoFailures >= 3) {
    cache.pageError = InkEpubResult::Io;
  }
  return InkEpubResult::Io;
}

static void clearIoFailures(BookCache &cache) { cache.transientIoFailures = 0; }

static InkEpubResult prepareBook(const char *providerId, const char *path,
                                 BookCache **out) {
  if (providerId == nullptr || path == nullptr) {
    return InkEpubResult::Io;
  }

  BookCache &cache = bookCache();
  if (!sameBookPath(cache, providerId, path)) {
    resetBookCache(cache, providerId, path);
  }
  if (out != nullptr) {
    *out = &cache;
  }
  return InkEpubResult::Ok;
}

static InkEpubResult ensureBook(const char *providerId, const char *path,
                                BookCache **out) {
  if (providerId == nullptr || path == nullptr) {
    return InkEpubResult::Io;
  }

  BookCache &cache = bookCache();
  if (sameBook(cache, providerId, path)) {
    if (out != nullptr) {
      *out = &cache;
    }
    return InkEpubResult::Ok;
  }

  if (!sameBookPath(cache, providerId, path)) {
    resetBookCache(cache, providerId, path);
  }

  File file = openBookFile(providerId, path);
  if (!file || file.isDirectory()) {
    return noteIoFailure(cache);
  }
  clearIoFailures(cache);
  InkEpubResult result = buildZipIndex(file, cache.index);
  if (result != InkEpubResult::Ok) {
    if (result == InkEpubResult::Io) {
      return noteIoFailure(cache);
    }
    cache.pageError = result;
    return result;
  }
  result = loadSpine(file, cache.index, cache.spine, cache.manifest);
  if (result != InkEpubResult::Ok) {
    if (result == InkEpubResult::Io) {
      return noteIoFailure(cache);
    }
    cache.pageError = result;
    return result;
  }
  cache.valid = true;
  if (out != nullptr) {
    *out = &cache;
  }
  return InkEpubResult::Ok;
}

struct TextPager {
  uint32_t targetStart = 0;
  uint8_t columns = 0;
  uint8_t rows = 0;
  InkEpubLineHandler handler = nullptr;
  void *handlerUser = nullptr;
  InkEpubScreenInfo *info = nullptr;
  char line[40] = {};
  uint8_t lineLen = 0;
  uint32_t globalLine = 0;
  uint16_t drawn = 0;
  bool lastBlank = true;
  bool pendingSpace = false;
  uint8_t utf8Remaining = 0;
  uint32_t utf8Codepoint = 0;

  bool finishLine(bool allowBlank) {
    const bool blank = lineLen == 0;
    if (blank && (!allowBlank || lastBlank)) {
      return true;
    }
    if (globalLine >= targetStart && drawn < rows) {
      line[lineLen] = '\0';
      InkEpubTextLine out;
      out.text = line;
      out.length = lineLen;
      if (handler != nullptr) {
        handler(out, handlerUser);
      }
      drawn++;
      if (info != nullptr) {
        info->lineCount = drawn;
      }
      if (drawn >= rows) {
        lineLen = 0;
        return false;
      }
    }
    globalLine++;
    lastBlank = blank;
    lineLen = 0;
    pendingSpace = false;
    return true;
  }

  bool putAscii(char c) {
    if (c == '\r') {
      return true;
    }
    if (c == '\n') {
      return finishLine(true);
    }
    if (c == '\t' || c == ' ') {
      pendingSpace = true;
      return true;
    }
    if (c < 0x20 || c > 0x7e) {
      c = '?';
    }
    if (pendingSpace && lineLen > 0) {
      if (lineLen >= columns && !finishLine(false)) {
        return false;
      }
      line[lineLen++] = ' ';
    }
    pendingSpace = false;
    if (lineLen >= columns && !finishLine(false)) {
      return false;
    }
    line[lineLen++] = c;
    lastBlank = false;
    return true;
  }

  bool putByte(uint8_t byte) {
    if (byte < 0x80) {
      utf8Remaining = 0;
      return putAscii(static_cast<char>(byte));
    }
    if ((byte & 0xc0U) == 0x80U) {
      if (utf8Remaining == 0) {
        return putAscii('?');
      }
      utf8Codepoint = (utf8Codepoint << 6U) | (byte & 0x3fU);
      utf8Remaining--;
      if (utf8Remaining > 0) {
        return true;
      }
      const char *replacement = inkTextAsciiReplacement(utf8Codepoint);
      if (replacement == nullptr) {
        return putAscii('?');
      }
      while (*replacement != '\0') {
        if (!putAscii(*replacement++)) {
          return false;
        }
      }
      return true;
    }
    if ((byte & 0xe0U) == 0xc0U) {
      utf8Codepoint = byte & 0x1fU;
      utf8Remaining = 1;
      return true;
    }
    if ((byte & 0xf0U) == 0xe0U) {
      utf8Codepoint = byte & 0x0fU;
      utf8Remaining = 2;
      return true;
    }
    if ((byte & 0xf8U) == 0xf0U) {
      utf8Codepoint = byte & 0x07U;
      utf8Remaining = 3;
      return true;
    }
    utf8Remaining = 0;
    return putAscii('?');
  }
};

typedef void (*ImageTagHandler)(std::string_view tag, void *user);

struct TagStripper {
  TextPager *pager = nullptr;
  ImageTagHandler imageHandler = nullptr;
  void *imageUser = nullptr;
  bool inTag = false;
  bool entity = false;
  char entityText[8] = {};
  uint8_t entityLen = 0;
  char tag[8] = {};
  char rawTag[192] = {};
  uint8_t tagLen = 0;
  uint8_t rawTagLen = 0;
  bool tagCapture = false;
  bool closing = false;
  char skipTag[8] = {};
  uint8_t skipTagLen = 0;
  bool skipContent = false;

  bool putPager(uint8_t byte) {
    return pager == nullptr || pager->putByte(byte);
  }

  bool flushEntityLiteral() {
    if (!putPager('&')) {
      return false;
    }
    for (uint8_t i = 0; i < entityLen; i++) {
      if (!putPager(static_cast<uint8_t>(entityText[i]))) {
        return false;
      }
    }
    entity = false;
    entityLen = 0;
    return true;
  }

  bool putReplacement(const char *replacement) {
    while (replacement != nullptr && *replacement != '\0') {
      if (!putPager(static_cast<uint8_t>(*replacement++))) {
        return false;
      }
    }
    return true;
  }

  static int entityHexValue(char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  }

  bool numericEntity(uint32_t *out) const {
    if (out == nullptr || entityLen < 2 || entityText[0] != '#') {
      return false;
    }
    uint8_t pos = 1;
    uint8_t base = 10;
    if (pos < entityLen && (entityText[pos] == 'x' || entityText[pos] == 'X')) {
      base = 16;
      pos++;
    }
    if (pos >= entityLen) {
      return false;
    }
    uint32_t value = 0;
    for (; pos < entityLen; pos++) {
      const int digit =
          base == 16 ? entityHexValue(entityText[pos])
                     : (entityText[pos] >= '0' && entityText[pos] <= '9'
                            ? entityText[pos] - '0'
                            : -1);
      if (digit < 0 || digit >= base || value > 0x10ffffUL / base) {
        return false;
      }
      value = value * base + static_cast<uint32_t>(digit);
    }
    *out = value;
    return true;
  }

  bool finishEntity() {
    char out = 0;
    if (entityLen == 3 && memcmp(entityText, "amp", 3) == 0) {
      out = '&';
    } else if (entityLen == 2 && memcmp(entityText, "lt", 2) == 0) {
      out = '<';
    } else if (entityLen == 2 && memcmp(entityText, "gt", 2) == 0) {
      out = '>';
    } else if (entityLen == 4 && memcmp(entityText, "nbsp", 4) == 0) {
      out = ' ';
    } else if (entityLen == 3 && memcmp(entityText, "shy", 3) == 0) {
      entity = false;
      entityLen = 0;
      return true;
    } else if ((entityLen == 4 && memcmp(entityText, "zwnj", 4) == 0) ||
               (entityLen == 3 && memcmp(entityText, "zwj", 3) == 0) ||
               (entityLen == 3 && memcmp(entityText, "lrm", 3) == 0) ||
               (entityLen == 3 && memcmp(entityText, "rlm", 3) == 0)) {
      entity = false;
      entityLen = 0;
      return true;
    }
    uint32_t codepoint = 0;
    if (out == 0 && numericEntity(&codepoint)) {
      entity = false;
      entityLen = 0;
      if (codepoint < 0x80U) {
        return codepoint >= 0x20U ? putPager(static_cast<uint8_t>(codepoint))
                                  : true;
      }
      const char *replacement = inkTextAsciiReplacement(codepoint);
      return replacement != nullptr ? putReplacement(replacement) : putPager('?');
    }
    if (out == 0) {
      return flushEntityLiteral();
    }
    entity = false;
    entityLen = 0;
    return putPager(static_cast<uint8_t>(out));
  }

  static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  bool newlineTag() const {
    if (tagLen == 2 && tag[0] == 'b' && tag[1] == 'r') {
      return true;
    }
    if (!closing) {
      return false;
    }
    if (tagLen == 1 && (tag[0] == 'p')) {
      return true;
    }
    if (tagLen == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
      return true;
    }
    return (tagLen == 2 && tag[0] == 'l' && tag[1] == 'i') ||
           (tagLen == 3 && memcmp(tag, "div", 3) == 0);
  }

  bool tagEquals(const char *name, uint8_t len) const {
    return tagLen == len && memcmp(tag, name, len) == 0;
  }

  bool imageTag() const { return !closing && tagEquals("img", 3); }

  bool skipStartTag() const {
    return tagEquals("head", 4) || tagEquals("style", 5) ||
           tagEquals("script", 6);
  }

  bool skipEndTag() const {
    return skipContent && closing && tagLen == skipTagLen &&
           memcmp(tag, skipTag, tagLen) == 0;
  }

  void updateSkipContent() {
    if (skipEndTag()) {
      skipContent = false;
      skipTagLen = 0;
      return;
    }
    if (!closing && skipStartTag()) {
      skipContent = true;
      skipTagLen = tagLen;
      memcpy(skipTag, tag, tagLen);
    }
  }

  void handleImageTag() {
    if (imageHandler != nullptr && imageTag()) {
      imageHandler(std::string_view(rawTag, rawTagLen), imageUser);
    }
  }

  bool putByte(uint8_t byte) {
    if (!inTag && skipContent) {
      if (byte == '<') {
        inTag = true;
        tagLen = 0;
        rawTagLen = 0;
        closing = false;
        tagCapture = true;
      }
      return true;
    }

    if (!inTag && entity) {
      if (byte == ';') {
        return finishEntity();
      }
      if (entityLen < sizeof(entityText) && byte >= 0x20 && byte <= 0x7e &&
          byte != '<' && byte != '&') {
        entityText[entityLen++] = static_cast<char>(byte);
        return true;
      }
      if (!flushEntityLiteral()) {
        return false;
      }
    }

    if (!inTag && byte == '&') {
      entity = true;
      entityLen = 0;
      return true;
    }
    if (!inTag && byte == '<') {
      inTag = true;
      tagLen = 0;
      rawTagLen = 0;
      closing = false;
      tagCapture = true;
      return true;
    }
    if (!inTag) {
      return putPager(byte);
    }

    if (byte == '>') {
      inTag = false;
      const bool addNewline = !skipContent && newlineTag();
      if (!skipContent) {
        handleImageTag();
      }
      updateSkipContent();
      return addNewline ? putPager('\n') : true;
    }
    if (rawTagLen < sizeof(rawTag)) {
      rawTag[rawTagLen++] = static_cast<char>(byte);
    }
    if (tagCapture) {
      if (byte == '/') {
        closing = tagLen == 0;
        return true;
      }
      const bool nameChar =
          (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9');
      if (nameChar && tagLen < sizeof(tag)) {
        tag[tagLen++] = lower(static_cast<char>(byte));
      } else {
        tagCapture = false;
      }
    }
    return true;
  }
};

static bool stripXhtmlByte(uint8_t byte, void *user) {
  TagStripper *stripper = static_cast<TagStripper *>(user);
  return stripper != nullptr && stripper->putByte(byte);
}

static InkEpubRasterKind manifestRasterKind(const BookCache &cache,
                                            std::string_view path) {
  for (const ManifestItem &item : cache.manifest) {
    if (item.href.size() == path.size() && item.href == path) {
      return item.rasterKind != InkEpubRasterKind::Unknown
                 ? item.rasterKind
                 : rasterKindForMedia({}, path);
    }
  }
  return rasterKindForMedia({}, path);
}

struct PageBuildContext {
  BookCache *cache = nullptr;
  std::string base;
  InkEpubResult result = InkEpubResult::Ok;
};

static void collectImagePage(std::string_view tag, void *user) {
  PageBuildContext *context = static_cast<PageBuildContext *>(user);
  if (context == nullptr || context->cache == nullptr ||
      context->result != InkEpubResult::Ok) {
    return;
  }
  const std::string_view src = findAttribute(tag, "src");
  if (src.empty()) {
    return;
  }
  std::string href;
  appendXmlAttribute(href, src);
  if (href.empty()) {
    return;
  }
  ImageRef image;
  image.path = normalizeZipPath(context->base, href);
  image.rasterKind = manifestRasterKind(*context->cache, image.path);
  if (image.rasterKind == InkEpubRasterKind::Unknown) {
    return;
  }
  if (context->cache->images.size() >= INKEPUB_MAX_IMAGES ||
      context->cache->pages.size() >= INKEPUB_MAX_PAGES) {
    context->result = InkEpubResult::Memory;
    return;
  }
  const uint16_t imageIndex =
      static_cast<uint16_t>(context->cache->images.size());
  context->cache->images.push_back(image);
  PageRef page;
  page.kind = InkEpubPageKind::Image;
  page.imageIndex = imageIndex;
  context->cache->pages.push_back(page);
}

static InkEpubResult countSpineTextAndImages(File &file, const ZipIndex &index,
                                             BookCache &cache,
                                             uint32_t spineIndex,
                                             uint8_t columns, uint8_t rows) {
  if (spineIndex >= cache.spine.size()) {
    return InkEpubResult::Format;
  }
  const std::string &path = cache.spine[spineIndex];
  const ZipEntry *entry = index.find(path);
  if (entry == nullptr) {
    return InkEpubResult::Ok;
  }

  const uint32_t firstLine = 0;
  TextPager pager;
  pager.targetStart = 0xffffffffUL;
  pager.columns = columns;
  pager.rows = 255;
  PageBuildContext build;
  build.cache = &cache;
  build.base = baseDirectory(path);
  TagStripper stripper;
  stripper.pager = &pager;
  stripper.imageHandler = collectImagePage;
  stripper.imageUser = &build;

  const InkEpubResult result =
      streamZipEntry(file, *entry, stripXhtmlByte, &stripper);
  if (result != InkEpubResult::Ok && result != InkEpubResult::Done) {
    return result;
  }
  if (build.result != InkEpubResult::Ok) {
    return build.result;
  }
  if (stripper.entity && !stripper.flushEntityLiteral()) {
    return InkEpubResult::Ok;
  }
  pager.finishLine(false);

  const uint32_t lineCount = pager.globalLine;
  for (uint32_t line = firstLine; line < lineCount; line += rows) {
    if (cache.pages.size() >= INKEPUB_MAX_PAGES) {
      return InkEpubResult::Memory;
    }
    PageRef page;
    page.kind = InkEpubPageKind::Text;
    page.spineIndex = spineIndex;
    page.lineStart = line;
    cache.pages.push_back(page);
  }
  return InkEpubResult::Ok;
}

static void resetPages(BookCache &cache, uint8_t columns, uint8_t rows) {
  cache.pages.clear();
  cache.images.clear();
  cache.nextSpineIndex = 0;
  cache.pageColumns = columns;
  cache.pageRows = rows;
  cache.pageError = InkEpubResult::Ok;
  cache.pagesComplete = false;
}

static InkEpubResult ensurePageLayout(const char *providerId, const char *path,
                                      uint8_t columns, uint8_t rows,
                                      BookCache **out) {
  if (columns == 0 || rows == 0 || columns >= 40) {
    return InkEpubResult::Format;
  }
  BookCache *cache = nullptr;
  InkEpubResult result = ensureBook(providerId, path, &cache);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (cache->pageColumns != columns || cache->pageRows != rows) {
    resetPages(*cache, columns, rows);
  }
  if (out != nullptr) {
    *out = cache;
  }
  return InkEpubResult::Ok;
}

static InkEpubResult continuePages(const char *providerId, const char *path,
                                   uint8_t columns, uint8_t rows,
                                   uint32_t budgetUs, BookCache **out) {
  BookCache *cache = nullptr;
  InkEpubResult result =
      ensurePageLayout(providerId, path, columns, rows, &cache);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (cache->pageError != InkEpubResult::Ok) {
    return cache->pageError;
  }
  if (cache->pagesComplete) {
    if (out != nullptr) {
      *out = cache;
    }
    return InkEpubResult::Done;
  }

  File file = openBookFile(providerId, path);
  if (!file || file.isDirectory()) {
    return noteIoFailure(*cache);
  }
  clearIoFailures(*cache);
  const uint32_t startedAt = currentMicros();
  while (cache->nextSpineIndex < cache->spine.size()) {
    result = countSpineTextAndImages(file, cache->index, *cache,
                                     cache->nextSpineIndex, columns, rows);
    if (result != InkEpubResult::Ok) {
      if (result == InkEpubResult::Io) {
        return noteIoFailure(*cache);
      }
      cache->pageError = result;
      return result;
    }
    clearIoFailures(*cache);
    cache->nextSpineIndex++;
    if (budgetUs > 0 && currentMicros() - startedAt >= budgetUs) {
      if (out != nullptr) {
        *out = cache;
      }
      return InkEpubResult::Ok;
    }
  }
  cache->pagesComplete = true;
  if (out != nullptr) {
    *out = cache;
  }
  return InkEpubResult::Done;
}

static InkEpubResult extractTextPage(File &file, const ZipIndex &index,
                                     const std::vector<std::string> &spine,
                                     const PageRef &page, uint8_t columns,
                                     uint8_t rows,
                                     InkEpubLineHandler handler,
                                     void *context,
                                     InkEpubScreenInfo *info) {
  if (page.spineIndex >= spine.size()) {
    return InkEpubResult::Format;
  }
  const ZipEntry *entry = index.find(spine[page.spineIndex]);
  if (entry == nullptr) {
    return InkEpubResult::Format;
  }
  TextPager pager;
  pager.targetStart = page.lineStart;
  pager.columns = columns;
  pager.rows = rows;
  pager.handler = handler;
  pager.handlerUser = context;
  pager.info = info;
  TagStripper stripper;
  stripper.pager = &pager;
  if (info != nullptr) {
    info->lineCount = 0;
    info->endReached = false;
  }
  const InkEpubResult result =
      streamZipEntry(file, *entry, stripXhtmlByte, &stripper);
  if (result == InkEpubResult::Done) {
    return InkEpubResult::Ok;
  }
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (stripper.entity && !stripper.flushEntityLiteral()) {
    return InkEpubResult::Ok;
  }
  pager.finishLine(false);
  if (info != nullptr) {
    info->endReached = pager.drawn < rows;
  }
  return InkEpubResult::Ok;
}

struct BufferLoad {
  uint8_t *data = nullptr;
  size_t capacity = 0;
  size_t size = 0;
};

static bool appendToBuffer(uint8_t byte, void *user) {
  BufferLoad *load = static_cast<BufferLoad *>(user);
  if (load == nullptr || load->data == nullptr || load->size >= load->capacity) {
    return false;
  }
  load->data[load->size++] = byte;
  return true;
}

static InkEpubResult loadEntryBuffer(File &file, const ZipEntry &entry,
                                     uint8_t **outData, size_t *outSize) {
  if (outData == nullptr || outSize == nullptr || entry.uncompressedSize == 0) {
    return InkEpubResult::Format;
  }
  *outData = nullptr;
  *outSize = 0;
  uint8_t *data = static_cast<uint8_t *>(allocMemory(entry.uncompressedSize));
  if (data == nullptr) {
    return InkEpubResult::Memory;
  }
  BufferLoad load;
  load.data = data;
  load.capacity = entry.uncompressedSize;
  const InkEpubResult result = streamZipEntry(file, entry, appendToBuffer, &load);
  if ((result != InkEpubResult::Ok && result != InkEpubResult::Done) ||
      load.size != entry.uncompressedSize) {
    freeMemory(data);
    return result == InkEpubResult::Ok ? InkEpubResult::Format : result;
  }
  *outData = data;
  *outSize = load.size;
  return InkEpubResult::Ok;
}

} // namespace inkepub_detail

void inkEpubSetHooks(const InkEpubHooks &hooks) {
  inkepub_detail::hooksStore().hooks = hooks;
}

InkEpubResult inkEpubOpen(const char *providerId, const char *path,
                          InkEpubBookInfo *info) {
  inkepub_detail::BookCache *cache = nullptr;
  const InkEpubResult result =
      inkepub_detail::prepareBook(providerId, path, &cache);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (info != nullptr) {
    const size_t count = cache != nullptr && cache->valid ? cache->spine.size()
                                                          : 1U;
    info->spineItems =
        count > 65535U ? 65535U : static_cast<uint16_t>(count);
  }
  return InkEpubResult::Ok;
}

InkEpubResult inkEpubExtractScreenText(const char *providerId,
                                       const char *path, uint32_t screen,
                                       uint8_t columns, uint8_t rows,
                                       InkEpubLineHandler handler,
                                       void *context,
                                       InkEpubScreenInfo *info) {
  inkepub_detail::BookCache *cache = nullptr;
  InkEpubResult result =
      inkepub_detail::ensurePageLayout(providerId, path, columns, rows, &cache);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (cache->pageError != InkEpubResult::Ok) {
    return cache->pageError;
  }
  if (!cache->pagesComplete) {
    return InkEpubResult::Done;
  }
  if (screen >= cache->pages.size()) {
    return InkEpubResult::Done;
  }
  const inkepub_detail::PageRef &pageRef = cache->pages[screen];
  if (pageRef.kind != InkEpubPageKind::Text) {
    if (info != nullptr) {
      info->screen = screen;
      info->lineCount = 0;
      info->endReached = false;
    }
    return InkEpubResult::Ok;
  }
  File file = inkepub_detail::openBookFile(providerId, path);
  if (!file || file.isDirectory()) {
    return InkEpubResult::Io;
  }
  if (info != nullptr) {
    info->screen = screen;
  }
  return inkepub_detail::extractTextPage(file, cache->index, cache->spine,
                                         pageRef, columns, rows, handler,
                                         context, info);
}

bool inkEpubLoading(const char *providerId, const char *path) {
  inkepub_detail::BookCache &cache = inkepub_detail::bookCache();
  if (!inkepub_detail::sameBookPath(cache, providerId, path)) {
    return false;
  }
  return cache.pageError == InkEpubResult::Ok &&
         (!cache.valid || !cache.pagesComplete);
}

InkEpubResult inkEpubContinueIndex(const char *providerId, const char *path,
                                   uint8_t columns, uint8_t rows,
                                   uint32_t budgetUs) {
  return inkepub_detail::continuePages(providerId, path, columns, rows, budgetUs,
                                       nullptr);
}

uint8_t inkEpubProgress(const char *providerId, const char *path) {
  inkepub_detail::BookCache &cache = inkepub_detail::bookCache();
  if (!inkepub_detail::sameBookPath(cache, providerId, path) || !cache.valid ||
      cache.spine.empty()) {
    return 0;
  }
  if (cache.pagesComplete) {
    return 100;
  }
  const uint32_t percent =
      (static_cast<uint32_t>(cache.nextSpineIndex) * 100U) /
      cache.spine.size();
  return percent > 99U ? 99U : static_cast<uint8_t>(percent);
}

uint16_t inkEpubPageCount(const char *providerId, const char *path,
                          uint8_t columns, uint8_t rows) {
  inkepub_detail::BookCache &cache = inkepub_detail::bookCache();
  if (!inkepub_detail::sameBookPath(cache, providerId, path) ||
      cache.pageColumns != columns || cache.pageRows != rows ||
      cache.pageError != InkEpubResult::Ok || !cache.pagesComplete) {
    return 0;
  }
  return cache.pages.size() > 65535U ? 65535U
                                     : static_cast<uint16_t>(cache.pages.size());
}

InkEpubResult inkEpubPageInfo(const char *providerId, const char *path,
                              uint32_t page, InkEpubPageInfo *info) {
  inkepub_detail::BookCache *cache = nullptr;
  const InkEpubResult result =
      inkepub_detail::ensurePageLayout(providerId, path, 31, 16, &cache);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (cache->pageError != InkEpubResult::Ok) {
    return cache->pageError;
  }
  if (!cache->pagesComplete) {
    return InkEpubResult::Done;
  }
  if (info == nullptr || page >= cache->pages.size()) {
    return InkEpubResult::Format;
  }
  const inkepub_detail::PageRef &pageRef = cache->pages[page];
  info->kind = pageRef.kind;
  info->rasterKind = InkEpubRasterKind::Unknown;
  info->imageBytes = 0;
  if (pageRef.kind == InkEpubPageKind::Image &&
      pageRef.imageIndex < cache->images.size()) {
    const inkepub_detail::ImageRef &image = cache->images[pageRef.imageIndex];
    info->rasterKind = image.rasterKind;
    const inkepub_detail::ZipEntry *entry = cache->index.find(image.path);
    info->imageBytes = entry != nullptr ? entry->uncompressedSize : 0;
  }
  return InkEpubResult::Ok;
}

InkEpubResult inkEpubLoadImagePage(const char *providerId, const char *path,
                                   uint32_t page, uint8_t **outData,
                                   size_t *outSize,
                                   InkEpubRasterKind *outKind) {
  inkepub_detail::BookCache *cache = nullptr;
  InkEpubResult result =
      inkepub_detail::ensurePageLayout(providerId, path, 31, 16, &cache);
  if (result != InkEpubResult::Ok) {
    return result;
  }
  if (cache->pageError != InkEpubResult::Ok) {
    return cache->pageError;
  }
  if (!cache->pagesComplete) {
    return InkEpubResult::Done;
  }
  if (page >= cache->pages.size()) {
    return InkEpubResult::Format;
  }
  const inkepub_detail::PageRef &pageRef = cache->pages[page];
  if (pageRef.kind != InkEpubPageKind::Image ||
      pageRef.imageIndex >= cache->images.size()) {
    return InkEpubResult::Format;
  }
  const inkepub_detail::ImageRef &image = cache->images[pageRef.imageIndex];
  const inkepub_detail::ZipEntry *entry = cache->index.find(image.path);
  if (entry == nullptr) {
    return InkEpubResult::Format;
  }
  File file = inkepub_detail::openBookFile(providerId, path);
  if (!file || file.isDirectory()) {
    return InkEpubResult::Io;
  }
  result = inkepub_detail::loadEntryBuffer(file, *entry, outData, outSize);
  if (result == InkEpubResult::Ok && outKind != nullptr) {
    *outKind = image.rasterKind;
  }
  return result;
}

void inkEpubFreeBuffer(void *ptr) { inkepub_detail::freeMemory(ptr); }

#endif // INKEPUB_IMPLEMENTATION

#endif // INKEPUB_H
