# InkFont / InkText (experimental)

UTF-8 text rendering with no third-party code: pre-rasterized bitmap fonts
(`.ifnt`), a compact layout engine and a small amount of script support.

## Coverage

- **Built-in (flash, ~270 KB):** `sans-12`, `sans-bold-12`, `sans-16`,
  `sans-bold-16` (DejaVu Sans, 1 bpp): Latin with Vietnamese, Greek,
  Cyrillic, Hebrew, Arabic, punctuation, arrows, math, box drawing and
  symbols. `display-32` (2 bpp, antialiased) carries Latin-1 for clocks and
  headlines and falls back to `sans-bold-16` at 2x for other scripts.
- **SD card:** copy `fonts/sdcard/fonts/unicode.ifnt` (GNU Unifont 15.1,
  16 px, 2 MB, all assigned BMP + plane 1 characters) to `/fonts/unicode.ifnt`.
  The firmware attaches it automatically as the fallback for every built-in
  font: CJK, Hangul, Thai, Devanagari, emoji (monochrome) and so on. Only
  ~12 KB of RAM is used for its range table and glyph cache.

Unifont is dual licensed GPL-2.0+ (with font embedding exception) / SIL OFL 1.1;
DejaVu fonts use the Bitstream Vera license.

## API

```cpp
#include "ui/inkfont/inktext.h"

InkTextStyle style;                      // sans-12, black, wrap, Auto align
inkTextDraw(gfx, 4, 10, "Привет, 世界");   // one line, y = line top

style.font = inkFontNamed("sans-16");
style.align = InkAlign::Center;
style.valign = InkVAlign::Middle;
style.ellipsis = true;
InkTextResult r = inkTextBox(&gfx, 0, 20, 200, 120, text, style);
// r.consumed: bytes that fit, for pagination; pass nullptr gfx to measure.

InkTextCursor out(gfx);                  // print()-style incremental output
out.printf("%d°C ", 21);
```

`size` scales any font: 1 bpp fonts by whole multiples (crisp), antialiased
fonts freely (bilinear). Antialiased edges are thresholded or ordered-dithered
(`InkAntialias::Dither`); `RunWriter` in `inktext.cpp` is the single place a
future grayscale driver would hook into.

JS and `.pink` apps reach the same fonts through the existing font registry:
`ui.setFont("sans-12")` then `ui.print("…")`; the cursor y is the line top.

## Script support

Line breaking at spaces, dashes, `\n`, ZWSP and between CJK characters;
Thai, Lao, Khmer and Myanmar break between character clusters (true word
breaks would need a dictionary). Overlong words are split, never before a
combining mark. Combining marks are zero-advance overlays (the converter
strips Unifont's dotted-circle placeholders). Arabic/Persian/Urdu contextual shaping and lam-alef
ligatures via presentation forms. Simplified bidi reordering per line
(paragraph direction from the first strong character, numbers stay LTR,
brackets mirrored). Not supported: explicit bidi embeddings, Indic shaping
(Devanagari etc. render letters but without reordering or conjuncts), precise
stacking of several marks on one letter, kerning.

## Fonts

`tools/inkfont-convert.py` builds `.ifnt` files or C headers from TTF/OTF
(needs Pillow) or BDF fonts. The format is documented at the top of the
script.
