#include "ui/inkfont/inkfont.h"

#include "sys/psram_allocator.h"

#include <string.h>

namespace {

constexpr size_t kHeaderBytes = 24;
constexpr size_t kRangeBytes = 12;
constexpr size_t kGlyphBytes = 8;
// Cache slot: key (codepoint + 1, high bit = known missing), glyph record,
// then the bitmap. Zero keys mark empty slots.
constexpr size_t kSlotHeader = 4 + kGlyphBytes;
constexpr uint32_t kMissing = 0x80000000UL;
constexpr size_t kCacheBudget = 12 * 1024;

uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

uint32_t rd32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void wr32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

size_t bitmapBytes(const InkGlyph &g, uint8_t bpp) {
  return (static_cast<size_t>(g.width) * g.height * bpp + 7) / 8;
}

// Decodes a glyph record; returns the bitmap offset within the bitmap area.
uint32_t decodeRecord(const uint8_t *rec, InkGlyph &out) {
  const uint32_t word = rd32(rec);
  out.advance = static_cast<uint8_t>(word >> 24);
  out.width = rec[4];
  out.height = rec[5];
  out.xOffset = static_cast<int8_t>(rec[6]);
  out.yOffset = static_cast<int8_t>(rec[7]);
  return word & 0xFFFFFFUL;
}

} // namespace

bool InkFont::parseHeader(const uint8_t *h) {
  if (memcmp(h, "IFNT", 4) != 0 || h[4] != 1 ||
      (h[5] != 1 && h[5] != 2 && h[5] != 4)) {
    return false;
  }
  bpp_ = h[5];
  size_ = h[6] ? h[6] : 1;
  lineHeight_ = h[7];
  ascent_ = static_cast<int8_t>(h[8]);
  descent_ = h[9];
  rangeCount_ = rd16(h + 10);
  glyphCount_ = rd32(h + 12);
  maxGlyphBytes_ = rd16(h + 16);
  flags_ = rd16(h + 18);
  bitmapOffset_ = rd32(h + 20);
  const uint32_t tables =
      kHeaderBytes + rangeCount_ * kRangeBytes + glyphCount_ * kGlyphBytes;
  if (bitmapOffset_ < tables || bitmapOffset_ > totalSize_) {
    bpp_ = 0;
    return false;
  }
  return true;
}

bool InkFont::openMemory(const uint8_t *data, size_t size) {
  close();
  if (data == nullptr || size < kHeaderBytes) {
    return false;
  }
  totalSize_ = static_cast<uint32_t>(size);
  if (!parseHeader(data)) {
    return false;
  }
  data_ = data;
  ranges_ = data + kHeaderBytes;
  return true;
}

bool InkFont::openFile(File file, uint16_t cacheSlots) {
  close();
  uint8_t header[kHeaderBytes];
  if (!file || file.isDirectory() ||
      file.read(header, sizeof(header)) != sizeof(header)) {
    return false;
  }
  totalSize_ = static_cast<uint32_t>(file.size());
  if (!parseHeader(header)) {
    return false;
  }
  // Ranges stay resident: binary search over SD would cost a seek per probe.
  slotStride_ = static_cast<uint16_t>((kSlotHeader + maxGlyphBytes_ + 3) & ~3U);
  const size_t budgetSlots = kCacheBudget / slotStride_;
  cacheSlots_ = static_cast<uint16_t>(
      cacheSlots < budgetSlots ? cacheSlots : budgetSlots);
  if (cacheSlots_ < 8) {
    cacheSlots_ = 8;
  }
  const size_t rangeBytes = rangeCount_ * kRangeBytes;
  const size_t cacheBytes = static_cast<size_t>(cacheSlots_) * slotStride_;
  owned_ = static_cast<uint8_t *>(psram::allocateOrDefault(rangeBytes + cacheBytes));
  if (owned_ == nullptr || file.read(owned_, rangeBytes) != rangeBytes) {
    close();
    return false;
  }
  ranges_ = owned_;
  cache_ = owned_ + rangeBytes;
  memset(cache_, 0, cacheBytes);
  file_ = file;
  return true;
}

void InkFont::close() {
  if (file_) {
    file_.close();
  }
  file_ = File();
  psram::release(owned_);
  owned_ = nullptr;
  cache_ = nullptr;
  data_ = nullptr;
  ranges_ = nullptr;
  bpp_ = 0;
}

int32_t InkFont::glyphIndex(uint32_t cp) const {
  uint16_t lo = 0;
  uint16_t hi = rangeCount_;
  while (lo < hi) {
    const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    const uint8_t *range = ranges_ + mid * kRangeBytes;
    const uint32_t first = rd32(range);
    if (cp < first) {
      hi = mid;
    } else if (cp - first >= rd32(range + 4)) {
      lo = static_cast<uint16_t>(mid + 1);
    } else {
      const uint32_t index = rd32(range + 8) + (cp - first);
      return index < glyphCount_ ? static_cast<int32_t>(index) : -1;
    }
  }
  return -1;
}

bool InkFont::readFile(uint32_t offset, void *out, size_t size) const {
  return offset <= totalSize_ && size <= totalSize_ - offset &&
         file_.seek(offset) &&
         file_.read(static_cast<uint8_t *>(out), size) == size;
}

bool InkFont::glyph(uint32_t cp, InkGlyph &out) const {
  if (!valid()) {
    return false;
  }
  if (data_ == nullptr) {
    return fileGlyph(cp, out);
  }
  const int32_t index = glyphIndex(cp);
  if (index < 0) {
    return false;
  }
  const uint32_t tableOffset = kHeaderBytes + rangeCount_ * kRangeBytes;
  const uint32_t offset =
      decodeRecord(data_ + tableOffset + index * kGlyphBytes, out);
  const uint32_t area = totalSize_ - bitmapOffset_;
  if (offset > area || bitmapBytes(out, bpp_) > area - offset) {
    return false;
  }
  out.bits = data_ + bitmapOffset_ + offset;
  return true;
}

bool InkFont::fileGlyph(uint32_t cp, InkGlyph &out) const {
  const uint32_t key = cp + 1;
  uint8_t *slot = cache_ + ((key * 2654435761UL) >> 8) % cacheSlots_ * slotStride_;
  const uint32_t cached = rd32(slot);
  if ((cached & ~kMissing) == key) {
    if (cached & kMissing) {
      return false;
    }
    decodeRecord(slot + 4, out);
    out.bits = slot + kSlotHeader;
    return true;
  }

  uint32_t stored = key | kMissing;
  const int32_t index = glyphIndex(cp);
  uint8_t *rec = slot + 4;
  const uint32_t tableOffset = kHeaderBytes + rangeCount_ * kRangeBytes;
  if (index >= 0 && readFile(tableOffset + index * kGlyphBytes, rec, kGlyphBytes)) {
    const uint32_t offset = decodeRecord(rec, out);
    const size_t bytes = bitmapBytes(out, bpp_);
    if (bytes <= maxGlyphBytes_ &&
        readFile(bitmapOffset_ + offset, slot + kSlotHeader, bytes)) {
      stored = key;
    }
  }
  wr32(slot, stored);
  if (stored & kMissing) {
    return false;
  }
  out.bits = slot + kSlotHeader;
  return true;
}
