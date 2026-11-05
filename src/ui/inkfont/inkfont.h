/// InkFont: compact pre-rasterized Unicode bitmap fonts (.ifnt).
///
/// One format serves both flash and SD: a font is either a memory blob (ROM
/// array or PSRAM copy, zero-copy lookups) or an open file, in which case only
/// the header, the range table and a small direct-mapped glyph cache live in
/// RAM. Fonts chain through `fallback`, so a small built-in font can defer to a
/// full-Unicode SD font for scripts it lacks. See tools/inkfont-convert.py.
///
/// File-backed fonts mutate their cache during lookups; draw with one font
/// from one task at a time (the shell renders from a single task).
#ifndef INKFONT_H
#define INKFONT_H

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

/// One glyph as stored. `bits` stays valid until the next lookup on the font
/// that produced it.
struct InkGlyph {
  const uint8_t *bits = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t advance = 0;
  /// Bitmap top-left relative to the pen on the baseline (yOffset < 0 is up).
  int8_t xOffset = 0;
  int8_t yOffset = 0;
};

class InkFont {
public:
  InkFont() = default;
  /// Wraps a blob that outlives the font, e.g. a generated ROM array.
  InkFont(const uint8_t *data, size_t size) { openMemory(data, size); }
  ~InkFont() { close(); }
  InkFont(const InkFont &) = delete;
  InkFont &operator=(const InkFont &) = delete;

  bool openMemory(const uint8_t *data, size_t size);
  /// Takes ownership of an open file; cacheSlots bounds glyph-cache RAM.
  bool openFile(File file, uint16_t cacheSlots = 96);
  void close();

  bool valid() const { return bpp_ != 0; }
  /// Bits per pixel of stored coverage: 1 (crisp) or 2/4 (antialiased).
  uint8_t bpp() const { return bpp_; }
  /// Nominal pixel size the glyphs were rasterized at.
  uint8_t size() const { return size_; }
  uint8_t lineHeight() const { return lineHeight_; }
  int8_t ascent() const { return ascent_; }
  uint8_t descent() const { return descent_; }
  /// Pixel-designed fonts only scale by whole factors to stay crisp.
  bool pixelFont() const { return (flags_ & 1U) != 0; }

  /// Looks a codepoint up in this font only (fallbacks are not consulted).
  bool glyph(uint32_t codepoint, InkGlyph &out) const;

  /// Consulted, in order, when this font lacks a codepoint.
  const InkFont *fallback = nullptr;

private:
  int32_t glyphIndex(uint32_t codepoint) const;
  bool parseHeader(const uint8_t *header);
  bool readFile(uint32_t offset, void *out, size_t size) const;
  bool fileGlyph(uint32_t codepoint, InkGlyph &out) const;

  const uint8_t *data_ = nullptr;
  const uint8_t *ranges_ = nullptr;
  mutable File file_;
  uint8_t *owned_ = nullptr;
  uint8_t *cache_ = nullptr;
  uint32_t totalSize_ = 0;
  uint32_t glyphCount_ = 0;
  uint32_t bitmapOffset_ = 0;
  uint16_t rangeCount_ = 0;
  uint16_t maxGlyphBytes_ = 0;
  uint16_t cacheSlots_ = 0;
  uint16_t slotStride_ = 0;
  uint16_t flags_ = 0;
  uint8_t bpp_ = 0;
  uint8_t size_ = 0;
  uint8_t lineHeight_ = 0;
  int8_t ascent_ = 0;
  uint8_t descent_ = 0;
};

/// Built-in fonts compiled into flash. Names are stable for apps and runtimes.
size_t inkFontBuiltinCount();
const char *inkFontBuiltinName(size_t index);
const InkFont *inkFontBuiltin(size_t index);
/// Looks up a built-in font by name, returning nullptr when unknown.
const InkFont *inkFontNamed(const char *name);
/// Default UI text font ("sans-12").
const InkFont &inkFontDefault();

/// SD path of the full-Unicode fallback chained behind every built-in font.
extern const char *const INK_FONT_UNICODE_PATH;
/// Opens or closes the SD fallback; called when SD mount state changes.
void inkFontStorageChanged(bool mounted);

#endif
