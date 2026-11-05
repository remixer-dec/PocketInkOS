#include "ui/inkfont/inktext.h"

#include "ui/inkfont/inkshape.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int32_t kUnbounded = INT32_MAX / 2;
constexpr uint8_t kMaxFallbackDepth = 6;
// Lines with right-to-left text are reordered in a fixed buffer; longer lines
// (far wider than the panel) are drawn in logical order.
constexpr uint16_t kMaxBidiGlyphs = 160;
constexpr uint32_t kEllipsis = 0x2026;
constexpr uint32_t kLam = 0x0644;

// Ordered-dither thresholds for antialiased glyph edges on a 1-bit surface.
constexpr uint8_t kBayer[4][4] = {
    {8, 136, 40, 168}, {200, 72, 232, 104}, {56, 184, 24, 152}, {248, 120, 216, 88}};

// Primary font of a style plus its derived metrics. Sub-pixel positions are
// kept in 1/256 px so fractional advances never accumulate rounding drift.
struct Face {
  const InkFont *font;
  const InkTextStyle *style;
  uint16_t target;
  int32_t scale;
  int16_t ascent;
  int16_t lineHeight;
};

int32_t fontScale(const InkFont &font, uint16_t target) {
  if (font.pixelFont()) {
    const int32_t whole = (target * 2 + font.size()) / (2 * font.size());
    return (whole < 1 ? 1 : whole) * 256;
  }
  return static_cast<int32_t>(target) * 256 / font.size();
}

Face makeFace(const InkTextStyle &style) {
  Face face;
  face.font = style.font != nullptr && style.font->valid() ? style.font
                                                          : &inkFontDefault();
  face.style = &style;
  face.target = style.size ? style.size : face.font->size();
  face.scale = fontScale(*face.font, face.target);
  face.ascent = static_cast<int16_t>((face.font->ascent() * face.scale + 128) >> 8);
  const int16_t natural =
      static_cast<int16_t>((face.font->lineHeight() * face.scale + 128) >> 8);
  face.lineHeight = static_cast<int16_t>(natural + style.lineGap);
  if (face.lineHeight < 1) {
    face.lineHeight = 1;
  }
  return face;
}

// A glyph resolved through the fallback chain, ready to measure or draw.
struct Shaped {
  InkGlyph glyph;
  const InkFont *font = nullptr;
  int32_t scale = 256;
  int32_t advance = 0;
  bool draw = false;
  bool tofu = false;
};

bool lookup(const Face &face, uint32_t cp, Shaped &out) {
  const InkFont *font = face.font;
  for (uint8_t depth = 0; font != nullptr && depth < kMaxFallbackDepth;
       depth++, font = font->fallback) {
    if (font->glyph(cp, out.glyph)) {
      out.font = font;
      out.scale = font == face.font ? face.scale : fontScale(*font, face.target);
      out.advance = out.glyph.advance * out.scale;
      out.draw = out.glyph.width != 0 && out.glyph.height != 0;
      return true;
    }
  }
  return false;
}

// One shaped character with the source bytes it came from.
struct Item {
  uint32_t cp;
  uint32_t base;
  const char *start;
  const char *end;
};

// Decodes UTF-8 and applies Arabic contextual forms on the fly.
class Items {
public:
  Items(const char *p, const char *end) : p_(p), end_(end) {}

  bool next(Item &item) {
    if (p_ >= end_) {
      return false;
    }
    item.start = p_;
    item.base = inkUtf8Next(p_, end_);
    item.cp = item.base;
    const InkJoining joining = inkJoining(item.base);
    if (joining == InkJoining::Dual || joining == InkJoining::Right) {
      shape(item, joining);
    } else if (joining != InkJoining::Transparent) {
      prevJoins_ = joining == InkJoining::Causing;
    }
    item.end = p_;
    return true;
  }

private:
  void shape(Item &item, InkJoining joining) {
    const bool joinsPrev = prevJoins_;
    uint32_t next = 0;
    const char *q = p_;
    bool adjacent = true;
    while (q < end_) {
      next = inkUtf8Next(q, end_);
      if (inkJoining(next) != InkJoining::Transparent) {
        break;
      }
      adjacent = false;
      next = 0;
    }
    if (item.base == kLam && adjacent) {
      const uint32_t ligature = inkLamAlef(next, joinsPrev);
      if (ligature != 0) {
        item.cp = ligature;
        p_ = q;
        prevJoins_ = false;
        return;
      }
    }
    const InkJoining after = inkJoining(next);
    const bool joinsNext =
        joining == InkJoining::Dual &&
        (after == InkJoining::Dual || after == InkJoining::Right ||
         after == InkJoining::Causing);
    item.cp = inkArabicForm(item.base, joinsPrev, joinsNext);
    prevJoins_ = joining == InkJoining::Dual;
  }

  const char *p_;
  const char *end_;
  bool prevJoins_ = false;
};

bool isSpace(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000; }

bool isDash(uint32_t cp) {
  return cp == '-' || cp == 0x2010 || cp == 0x2013 || cp == 0x2014;
}

Shaped shape(const Face &face, uint32_t cp, uint32_t base) {
  Shaped out;
  if (inkInvisible(cp) || cp == '\n') {
    return out;
  }
  if (cp == '\t') {
    lookup(face, ' ', out);
    out.advance *= 4;
    out.draw = false;
    return out;
  }
  if (lookup(face, cp, out) || (base != cp && lookup(face, base, out))) {
    return out;
  }
  // Missing everywhere: a hollow box keeps the gap visible and measurable.
  out.tofu = true;
  out.draw = true;
  out.advance = face.target * 160;
  return out;
}

struct Line {
  const char *start;
  const char *end;  // content end, trailing spaces trimmed
  const char *next; // where the following line starts
  int32_t width;
  int32_t fullWidth; // including trailing spaces
  bool newline;      // ended at '\n'
  bool hard;         // split inside a word for lack of a break opportunity
};

Line breakLine(const Face &face, const char *p, const char *end, int32_t maxWidth) {
  Line line{p, p, end, 0, 0, false, false};
  Items items(p, end);
  Item item;
  int32_t width = 0;
  const char *breakEnd = nullptr;
  const char *breakNext = nullptr;
  int32_t breakWidth = 0;
  bool prevWide = false;
  while (items.next(item)) {
    if (item.cp == '\n') {
      line.next = item.end;
      line.newline = true;
      line.fullWidth = width;
      return line;
    }
    const int32_t advance = shape(face, item.cp, item.base).advance;
    if (isSpace(item.cp)) {
      width += advance;
      if (line.end > p) {
        breakEnd = line.end;
        breakWidth = line.width;
        breakNext = item.end;
      }
      prevWide = false;
      continue;
    }
    const bool wide = inkBreaksAnywhere(item.base);
    if ((wide || prevWide) && line.end > p) {
      breakEnd = line.end;
      breakWidth = line.width;
      breakNext = item.start;
    }
    if (width + advance > maxWidth && line.end > p) {
      if (breakEnd != nullptr) {
        line.end = breakEnd;
        line.width = breakWidth;
        line.next = breakNext;
      } else {
        line.next = item.start;
        line.hard = true;
      }
      line.fullWidth = line.width;
      return line;
    }
    width += advance;
    line.end = item.end;
    line.width = width;
    if (isDash(item.cp) || item.cp == 0x200B) {
      breakEnd = line.end;
      breakWidth = width;
      breakNext = item.end;
    }
    prevWide = wide;
  }
  line.fullWidth = width;
  return line;
}

bool paragraphRtl(const char *p, const char *end) {
  while (p < end) {
    const uint32_t cp = inkUtf8Next(p, end);
    if (cp == '\n') {
      break;
    }
    const InkBidiClass cls = inkBidiClass(cp);
    if (cls == InkBidiClass::Left || cls == InkBidiClass::Right) {
      return cls == InkBidiClass::Right;
    }
  }
  return false;
}

int16_t floorPx(int32_t v) { return static_cast<int16_t>(v >> 8); }
int16_t ceilPx(int32_t v) { return static_cast<int16_t>((v + 255) >> 8); }

uint8_t coverage(const InkGlyph &g, uint8_t bpp, int32_t x, int32_t y) {
  if (x < 0 || y < 0 || x >= g.width || y >= g.height) {
    return 0;
  }
  const uint32_t bit = (static_cast<uint32_t>(y) * g.width + x) * bpp;
  const uint8_t max = static_cast<uint8_t>((1U << bpp) - 1U);
  const uint8_t value = (g.bits[bit >> 3] >> (8 - bpp - (bit & 7))) & max;
  return static_cast<uint8_t>(value * 255 / max);
}

// Every glyph pixel reaching the surface goes through here as a horizontal
// run, which keeps drawing fast on canvases with native span fills. A future
// grayscale panel can map antialiased coverage here instead of thresholding.
struct RunWriter {
  Adafruit_GFX &gfx;
  uint16_t color;
  int16_t y;
  int16_t start = -1;

  void set(int16_t x, bool on) {
    if (on && start < 0) {
      start = x;
    } else if (!on && start >= 0) {
      gfx.drawFastHLine(start, y, x - start, color);
      start = -1;
    }
  }
};

void drawTofu(Adafruit_GFX &gfx, const Face &face, int32_t pen, int16_t baseline) {
  const int16_t w = static_cast<int16_t>(face.target * 160 / 256);
  const int16_t h = static_cast<int16_t>(face.ascent * 3 / 4);
  gfx.drawRect(ceilPx(pen) + 1, baseline - h, w - 2 > 1 ? w - 2 : 1, h,
               face.style->color);
}

void drawGlyph(Adafruit_GFX &gfx, const Face &face, const Shaped &s, int32_t pen,
               int16_t baseline) {
  if (s.tofu) {
    drawTofu(gfx, face, pen, baseline);
    return;
  }
  const InkGlyph &g = s.glyph;
  const uint8_t bpp = s.font->bpp();
  const uint16_t color = face.style->color;

  if (bpp == 1 && (s.scale & 255) == 0) {
    // Crisp path: whole-pixel scaling of 1-bit glyphs, one span per run.
    const int16_t k = static_cast<int16_t>(s.scale >> 8);
    const int16_t x0 = static_cast<int16_t>(((pen + 128) >> 8) + g.xOffset * k);
    const int16_t y0 = static_cast<int16_t>(baseline + g.yOffset * k);
    for (int16_t row = 0; row < g.height; row++) {
      const uint32_t rowBit = static_cast<uint32_t>(row) * g.width;
      int16_t run = -1;
      for (int16_t col = 0; col <= g.width; col++) {
        const uint32_t bit = rowBit + col;
        const bool on = col < g.width && (g.bits[bit >> 3] & (0x80 >> (bit & 7)));
        if (on && run < 0) {
          run = col;
        } else if (!on && run >= 0) {
          if (k == 1) {
            gfx.drawFastHLine(x0 + run, y0 + row, col - run, color);
          } else {
            gfx.fillRect(x0 + run * k, y0 + row * k, (col - run) * k, k, color);
          }
          run = -1;
        }
      }
    }
    return;
  }

  // General path: bilinear coverage sampling for antialiased fonts and
  // fractional scales, resolved to ink by threshold or ordered dither.
  const int32_t scale = s.scale;
  const int32_t ox = pen + g.xOffset * scale;
  const int32_t oy = baseline * 256 + g.yOffset * scale;
  const int16_t x0 = floorPx(ox);
  const int16_t x1 = ceilPx(ox + g.width * scale);
  const int16_t y1 = ceilPx(oy + g.height * scale);
  const bool dither = face.style->antialias == InkAntialias::Dither;
  for (int16_t dy = floorPx(oy); dy < y1; dy++) {
    const int32_t sy = ((dy * 256 + 128 - oy) * 256) / scale - 128;
    const int32_t iy = sy >> 8;
    const int32_t fy = sy & 255;
    RunWriter writer{gfx, color, dy};
    for (int16_t dx = x0; dx < x1; dx++) {
      const int32_t sx = ((dx * 256 + 128 - ox) * 256) / scale - 128;
      const int32_t ix = sx >> 8;
      const int32_t fx = sx & 255;
      const int32_t top = coverage(g, bpp, ix, iy) * (256 - fx) +
                          coverage(g, bpp, ix + 1, iy) * fx;
      const int32_t bottom = coverage(g, bpp, ix, iy + 1) * (256 - fx) +
                             coverage(g, bpp, ix + 1, iy + 1) * fx;
      const int32_t c = (top * (256 - fy) + bottom * fy) >> 16;
      writer.set(dx, dither ? c > kBayer[dy & 3][dx & 3] : c >= 128);
    }
    writer.set(x1, false);
  }
}

struct BidiGlyph {
  uint32_t cp;
  uint32_t base;
  InkBidiClass cls;
  uint8_t level;
};

void reverse(BidiGlyph *a, uint16_t from, uint16_t to) {
  while (from + 1 < to) {
    const BidiGlyph t = a[from];
    a[from++] = a[--to];
    a[to] = t;
  }
}

bool actsRight(const BidiGlyph &g) {
  return (g.level & 1) || (g.cls == InkBidiClass::Number && g.level == 2);
}

// Resolves embedding levels (simplified UAX #9: W/N rules for one paragraph
// level, no explicit embeddings) and reorders glyphs into visual order.
void reorder(BidiGlyph *g, uint16_t n, bool rtl) {
  const uint8_t base = rtl ? 1 : 0;
  bool lastRight = rtl;
  for (uint16_t i = 0; i < n; i++) {
    if (g[i].cls == InkBidiClass::Mark) {
      g[i].cls = i > 0 ? g[i - 1].cls : InkBidiClass::Neutral;
    }
    if (g[i].cls == InkBidiClass::Left) {
      lastRight = false;
    } else if (g[i].cls == InkBidiClass::Right) {
      lastRight = true;
    } else if (g[i].cls == InkBidiClass::Number) {
      // Numbers keep LTR digit order; after RTL text they join the RTL run.
      g[i].level = lastRight || rtl ? 2 : 0;
      continue;
    }
    if (g[i].cls != InkBidiClass::Neutral) {
      g[i].level = g[i].cls == InkBidiClass::Right ? 1 : (rtl ? 2 : 0);
    }
  }
  // Neutrals take the direction of surrounding strong text when both sides
  // agree (numbers count as RTL there), otherwise the paragraph level.
  for (uint16_t i = 0; i < n; i++) {
    if (g[i].cls != InkBidiClass::Neutral) {
      continue;
    }
    uint16_t j = i;
    while (j < n && g[j].cls == InkBidiClass::Neutral) {
      j++;
    }
    const bool beforeR = i > 0 ? actsRight(g[i - 1]) : rtl;
    const bool afterR = j < n ? actsRight(g[j]) : rtl;
    const uint8_t level = beforeR == afterR ? (beforeR ? 1 : (rtl ? 2 : 0)) : base;
    while (i < j) {
      g[i++].level = level;
    }
    i--;
  }
  for (uint8_t level = 2; level >= 1; level--) {
    for (uint16_t i = 0; i < n;) {
      if (g[i].level < level) {
        i++;
        continue;
      }
      uint16_t j = i;
      while (j < n && g[j].level >= level) {
        j++;
      }
      reverse(g, i, j);
      i = j;
    }
  }
  // Reversal put combining marks before their base; restore base-then-mark.
  for (uint16_t i = 0; i < n; i++) {
    if ((g[i].level & 1) && inkBidiClass(g[i].base) == InkBidiClass::Mark) {
      uint16_t j = i;
      while (j + 1 < n && inkBidiClass(g[j].base) == InkBidiClass::Mark) {
        j++;
      }
      reverse(g, i, j + 1);
      i = j;
    }
  }
}

int32_t ellipsisWidth(const Face &face) {
  const Shaped s = shape(face, kEllipsis, kEllipsis);
  return s.tofu ? shape(face, '.', '.').advance * 3 : s.advance;
}

// Draws the ellipsis glyph (or three dots when the font chain lacks it).
int32_t drawEllipsis(Adafruit_GFX &gfx, const Face &face, int32_t pen,
                     int16_t baseline) {
  Shaped s = shape(face, kEllipsis, kEllipsis);
  const uint8_t count = s.tofu ? 3 : 1;
  if (s.tofu) {
    s = shape(face, '.', '.');
  }
  for (uint8_t i = 0; i < count; i++) {
    if (s.draw) {
      drawGlyph(gfx, face, s, pen, baseline);
    }
    pen += s.advance;
  }
  return pen;
}

void drawLine(Adafruit_GFX &gfx, const Face &face, const char *start,
              const char *end, int32_t pen, int16_t top, bool rtl,
              bool ellipsis) {
  const int16_t baseline = top + face.ascent;
  bool mixed = rtl;
  uint16_t count = 0;
  for (const char *p = start; p < end; count++) {
    mixed = inkBidiClass(inkUtf8Next(p, end)) == InkBidiClass::Right || mixed;
  }
  mixed = mixed && count <= kMaxBidiGlyphs;

  BidiGlyph glyphs[kMaxBidiGlyphs];
  uint16_t n = 0;
  Items items(start, end);
  Item item;
  while (items.next(item)) {
    if (mixed) {
      glyphs[n++] = {item.cp, item.base, inkBidiClass(item.base), 0};
      continue;
    }
    const Shaped s = shape(face, item.cp, item.base);
    if (s.draw) {
      drawGlyph(gfx, face, s, pen, baseline);
    }
    pen += s.advance;
  }
  if (!mixed) {
    if (ellipsis) {
      drawEllipsis(gfx, face, pen, baseline);
    }
    return;
  }
  if (ellipsis && rtl) {
    pen = drawEllipsis(gfx, face, pen, baseline);
  }
  reorder(glyphs, n, rtl);
  for (uint16_t i = 0; i < n; i++) {
    const uint32_t cp = glyphs[i].level & 1 ? inkBidiMirror(glyphs[i].cp) : glyphs[i].cp;
    const Shaped s = shape(face, cp, glyphs[i].base);
    if (s.draw) {
      drawGlyph(gfx, face, s, pen, baseline);
    }
    pen += s.advance;
  }
  if (ellipsis && !rtl) {
    drawEllipsis(gfx, face, pen, baseline);
  }
}

// Cuts a line so its content plus an ellipsis fits maxWidth.
Line truncate(const Face &face, const Line &line, int32_t maxWidth) {
  Line out = line;
  const int32_t room = maxWidth - ellipsisWidth(face);
  Items items(line.start, line.end);
  Item item;
  int32_t width = 0;
  out.end = line.start;
  out.width = 0;
  while (items.next(item)) {
    width += shape(face, item.cp, item.base).advance;
    if (width > room) {
      break;
    }
    if (!isSpace(item.cp)) {
      out.end = item.end;
      out.width = width;
    }
  }
  return out;
}

const char *textEnd(const char *text, size_t length) {
  if (text == nullptr) {
    return nullptr;
  }
  return text + strnlen(text, length);
}

InkTextResult layout(Adafruit_GFX *gfx, int16_t x, int16_t y, int16_t w,
                     int16_t h, const char *text, size_t length,
                     const InkTextStyle &style, uint16_t maxLines) {
  InkTextResult result;
  const char *end = textEnd(text, length);
  if (end == nullptr || end == text) {
    return result;
  }
  const Face face = makeFace(style);
  if (h > 0) {
    const int16_t fit = static_cast<int16_t>(
        (h + (style.lineGap > 0 ? style.lineGap : 0)) / face.lineHeight);
    maxLines = static_cast<uint16_t>(fit < maxLines ? fit : maxLines);
  }
  const int32_t boxWidth = w > 0 ? w * 256 : kUnbounded;
  const int32_t wrapWidth = style.wrap ? boxWidth : kUnbounded;

  // Vertical alignment needs the line count first, so measure, then draw.
  int16_t top = y;
  if (gfx != nullptr && style.valign != InkVAlign::Top && h > 0) {
    const InkTextResult measured =
        layout(nullptr, x, y, w, h, text, length, style, maxLines);
    const int16_t spare = static_cast<int16_t>(h - measured.height);
    top = static_cast<int16_t>(
        y + (style.valign == InkVAlign::Middle ? spare / 2 : spare));
  }

  const char *p = text;
  bool rtl = paragraphRtl(p, end);
  while (p < end && result.lines < maxLines) {
    Line line = breakLine(face, p, end, wrapWidth);
    const bool last = result.lines + 1 == maxLines;
    const bool cut = style.ellipsis &&
                     ((last && line.next < end) || line.width > boxWidth);
    if (cut) {
      line = truncate(face, line, boxWidth);
    }
    const int16_t lineWidth = ceilPx(line.width + (cut ? ellipsisWidth(face) : 0));
    if (gfx != nullptr) {
      InkAlign align = style.align;
      if (align == InkAlign::Auto) {
        align = rtl ? InkAlign::Right : InkAlign::Left;
      }
      int16_t lineX = x;
      const int16_t span = w > 0 ? w : 0;
      if (align == InkAlign::Center) {
        lineX = static_cast<int16_t>(x + (span - lineWidth) / 2);
      } else if (align == InkAlign::Right) {
        lineX = static_cast<int16_t>(x + span - lineWidth);
      }
      drawLine(*gfx, face, line.start, line.end, lineX * 256,
               static_cast<int16_t>(top + result.lines * face.lineHeight), rtl,
               cut);
    }
    if (lineWidth > result.width) {
      result.width = lineWidth;
    }
    result.lines++;
    p = line.next;
    if (line.newline) {
      rtl = paragraphRtl(p, end);
    }
  }
  result.consumed = static_cast<size_t>(p - text);
  result.height = static_cast<int16_t>(
      result.lines ? result.lines * face.lineHeight - style.lineGap : 0);
  return result;
}

} // namespace

int16_t inkTextLineHeight(const InkTextStyle &style) {
  return makeFace(style).lineHeight;
}

int16_t inkTextAscent(const InkTextStyle &style) { return makeFace(style).ascent; }

int16_t inkTextWidth(const char *text, const InkTextStyle &style, size_t length) {
  InkTextStyle single = style;
  single.wrap = false;
  return layout(nullptr, 0, 0, 0, 0, text, length, single, 1).width;
}

bool inkTextHasGlyph(uint32_t cp, const InkTextStyle &style) {
  Shaped s;
  return lookup(makeFace(style), cp, s);
}

int16_t inkTextDraw(Adafruit_GFX &gfx, int16_t x, int16_t y, const char *text,
                    const InkTextStyle &style, size_t length) {
  InkTextStyle single = style;
  single.wrap = false;
  single.valign = InkVAlign::Top;
  if (single.align == InkAlign::Auto) {
    single.align = InkAlign::Left;
  }
  return layout(&gfx, x, y, 0, 0, text, length, single, 1).width;
}

InkTextResult inkTextBox(Adafruit_GFX *gfx, int16_t x, int16_t y, int16_t w,
                         int16_t h, const char *text, const InkTextStyle &style,
                         size_t length) {
  return layout(gfx, x, y, w, h, text, length, style, UINT16_MAX);
}

InkTextCursor::InkTextCursor(Adafruit_GFX &gfx, const InkTextStyle &initial)
    : style(initial), gfx_(gfx), right_(gfx.width()) {}

void InkTextCursor::setCursor(int16_t x, int16_t y) {
  x_ = x;
  y_ = y;
}

void InkTextCursor::setMargins(int16_t left, int16_t right) {
  left_ = left;
  right_ = right;
}

void InkTextCursor::print(const char *text, size_t length) {
  const char *end = textEnd(text, length);
  if (end == nullptr) {
    return;
  }
  const Face face = makeFace(style);
  const char *p = text;
  while (p < end) {
    const int32_t room = style.wrap ? (right_ - x_) * 256 : kUnbounded;
    const Line line = breakLine(face, p, end, room);
    if (line.hard && x_ > left_) {
      // The word would fit better on a fresh line than split here.
      x_ = left_;
      y_ = static_cast<int16_t>(y_ + face.lineHeight);
      continue;
    }
    drawLine(gfx_, face, line.start, line.end, x_ * 256, y_,
             paragraphRtl(line.start, line.end), false);
    p = line.next;
    if (line.newline || p < end) {
      x_ = left_;
      y_ = static_cast<int16_t>(y_ + face.lineHeight);
    } else {
      x_ = static_cast<int16_t>(x_ + ceilPx(line.fullWidth));
    }
  }
}

void InkTextCursor::printf(const char *format, ...) {
  char buffer[160];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  print(buffer);
}
