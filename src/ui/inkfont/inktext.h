/// InkText: UTF-8 text layout and rendering on any Adafruit_GFX surface.
///
/// Positions are the top-left of the line box, so a line of style `s` drawn at
/// y occupies [y, y + inkTextLineHeight(s)). Wrapping breaks at spaces, after
/// dashes, at '\n' and between CJK characters; overlong words are split.
/// Arabic is shaped and right-to-left runs are reordered per line.
#ifndef INKTEXT_H
#define INKTEXT_H

#include "ui/inkfont/inkfont.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Horizontal placement. In boxes Auto follows the paragraph direction (left
/// for LTR, right for RTL). For single-line draws x is the left edge (Auto,
/// Left), the center or the right edge.
enum class InkAlign : uint8_t { Auto, Left, Center, Right };
enum class InkVAlign : uint8_t { Top, Middle, Bottom };
/// How partial coverage of antialiased (2/4 bpp) fonts reaches a 1-bit panel.
enum class InkAntialias : uint8_t { Threshold, Dither };

struct InkTextStyle {
  /// nullptr selects inkFontDefault().
  const InkFont *font = nullptr;
  /// Target pixel size; 0 keeps the font's own. Pixel fonts round to whole
  /// multiples, antialiased fonts scale freely.
  uint8_t size = 0;
  uint16_t color = 1;
  InkAlign align = InkAlign::Auto;
  InkVAlign valign = InkVAlign::Top;
  /// Extra pixels between lines (may be negative).
  int8_t lineGap = 0;
  bool wrap = true;
  /// Ends the last visible line with an ellipsis when text is cut off.
  bool ellipsis = false;
  InkAntialias antialias = InkAntialias::Threshold;
};

struct InkTextResult {
  /// Widest laid-out line.
  int16_t width = 0;
  int16_t height = 0;
  uint16_t lines = 0;
  /// Bytes laid out; equals the text length when everything fit. Pass the
  /// remainder to the next call to paginate.
  size_t consumed = 0;
};

/// Pass as `length` to read up to the terminating NUL.
constexpr size_t INK_TEXT_NUL = static_cast<size_t>(-1);

int16_t inkTextLineHeight(const InkTextStyle &style = {});
/// Distance from the line top to the baseline.
int16_t inkTextAscent(const InkTextStyle &style = {});
/// Width of the first line of text (up to '\n').
int16_t inkTextWidth(const char *text, const InkTextStyle &style = {},
                     size_t length = INK_TEXT_NUL);
/// True when the font chain has a glyph for cp.
bool inkTextHasGlyph(uint32_t cp, const InkTextStyle &style = {});

/// Draws one line (no wrapping) anchored at x per style.align; returns its
/// width.
int16_t inkTextDraw(Adafruit_GFX &gfx, int16_t x, int16_t y, const char *text,
                    const InkTextStyle &style = {},
                    size_t length = INK_TEXT_NUL);

/// Wraps, aligns and clips text to a box; only whole lines are drawn.
/// gfx == nullptr measures without drawing; h <= 0 means unbounded height.
InkTextResult inkTextBox(Adafruit_GFX *gfx, int16_t x, int16_t y, int16_t w,
                         int16_t h, const char *text,
                         const InkTextStyle &style = {},
                         size_t length = INK_TEXT_NUL);

/// Print-style cursor for incremental output: continues where the previous
/// print stopped, honours '\n' and wraps at the right margin back to `left`.
class InkTextCursor {
public:
  InkTextCursor(Adafruit_GFX &gfx, const InkTextStyle &style = {});
  void setCursor(int16_t x, int16_t y);
  /// Wrapped lines restart at `left` and end at `right` (exclusive).
  void setMargins(int16_t left, int16_t right);
  void print(const char *text, size_t length = INK_TEXT_NUL);
  void printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }

  InkTextStyle style;

private:
  Adafruit_GFX &gfx_;
  int16_t x_ = 0;
  int16_t y_ = 0;
  int16_t left_ = 0;
  int16_t right_;
};

#endif
