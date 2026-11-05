#!/usr/bin/env python3
"""Converts TTF/OTF/TTC or BDF fonts into the InkFont (.ifnt) bitmap format.

Glyphs are pre-rasterized once on the host, so the firmware only blits bitmaps.
Outline fonts need Pillow (FreeType); BDF fonts are parsed without dependencies.

  tools/inkfont-convert.py DejaVuSans.ttf --size 12 --ranges rom -o sans12.ifnt
  tools/inkfont-convert.py unifont.otf unifont_upper.otf --size 16 --ranges all \
      -o unifont-16.ifnt
  tools/inkfont-convert.py font.bdf --ranges all --header kInkFontMy -o my.h

Format (little endian), mirrored by src/ui/inkfont/inkfont.cpp:
  header  24 B  "IFNT" ver bpp size lineHeight ascent descent rangeCount
                glyphCount maxGlyphBytes flags bitmapOffset
  ranges  12 B  first codepoint, count, first glyph index
  glyphs   8 B  bitmap offset:24 | advance:8, width, height, xOffset, yOffset
  bitmaps       cropped rows packed MSB-first as one continuous bit stream
"""

import argparse
import struct
import sys
import unicodedata

# Named range sets keep build commands short. "rom" is everything that fits
# the built-in flash budget: Latin incl. Vietnamese, Greek, Cyrillic, Hebrew,
# Arabic (with presentation forms used by the shaper) and common symbols.
RANGE_SETS = {
    "ascii": [(0x20, 0x7E)],
    "latin": [(0x20, 0x7E), (0xA0, 0x24F), (0x1E00, 0x1EFF)],
    "combining": [(0x300, 0x36F)],
    "greek": [(0x370, 0x3FF)],
    "cyrillic": [(0x400, 0x52F)],
    "hebrew": [(0x590, 0x5FF)],
    "arabic": [(0x600, 0x6FF), (0xFB50, 0xFDFF), (0xFE70, 0xFEFF)],
    "symbols": [(0x2000, 0x206F), (0x20A0, 0x20CF), (0x2100, 0x214F),
                (0x2150, 0x218F), (0x2190, 0x21FF), (0x2200, 0x22FF),
                (0x2300, 0x23FF), (0x2460, 0x24FF), (0x2500, 0x259F),
                (0x25A0, 0x25FF), (0x2600, 0x26FF), (0x2700, 0x27BF),
                (0xFFFD, 0xFFFD)],
    "all": [(0x20, 0x10FFFF)],
}
RANGE_SETS["rom"] = (RANGE_SETS["latin"] + RANGE_SETS["combining"] +
                     RANGE_SETS["greek"] + RANGE_SETS["cyrillic"] +
                     RANGE_SETS["hebrew"] + RANGE_SETS["arabic"] +
                     RANGE_SETS["symbols"])

FLAG_PIXEL = 1  # designed for 1:1 pixels; firmware scales by integers only


def parse_ranges(spec):
    out = []
    for part in spec.split(","):
        part = part.strip().lower()
        if part in RANGE_SETS:
            out += RANGE_SETS[part]
        elif "-" in part:
            a, b = part.split("-", 1)
            out.append((int(a, 16), int(b, 16)))
        elif part:
            out.append((int(part, 16), int(part, 16)))
    return out


def in_ranges(cp, ranges):
    # Unassigned, private-use and surrogate codepoints are never emitted:
    # placeholder glyphs for them would only waste space.
    if unicodedata.category(chr(cp)) in ("Cn", "Co", "Cs", "Cc"):
        return False
    return any(a <= cp <= b for a, b in ranges)


def sfnt_cmap(path, index=0):
    """Returns the set of codepoints mapped by an sfnt cmap (formats 4, 12)."""
    data = open(path, "rb").read()
    base = 0
    if data[:4] == b"ttcf":
        base = struct.unpack_from(">I", data, 12 + 4 * index)[0]
    count = struct.unpack_from(">H", data, base + 4)[0]
    cmap = None
    for i in range(count):
        tag, _, off, _ = struct.unpack_from(">4sIII", data, base + 12 + 16 * i)
        if tag == b"cmap":
            cmap = off
    if cmap is None:
        return set()
    best = None
    for i in range(struct.unpack_from(">H", data, cmap + 2)[0]):
        pid, eid, off = struct.unpack_from(">HHI", data, cmap + 4 + 8 * i)
        fmt = struct.unpack_from(">H", data, cmap + off)[0]
        if pid in (0, 3) and fmt in (4, 12) and (best is None or fmt == 12):
            best = cmap + off
    if best is None:
        return set()
    cps = set()
    if struct.unpack_from(">H", data, best)[0] == 12:
        groups = struct.unpack_from(">I", data, best + 12)[0]
        for g in range(groups):
            a, b, gid = struct.unpack_from(">III", data, best + 16 + 12 * g)
            cps.update(range(a, b + 1))
        return cps
    segs = struct.unpack_from(">H", data, best + 6)[0] // 2
    ends = best + 14
    starts = ends + 2 * segs + 2
    deltas = starts + 2 * segs
    roffs = deltas + 2 * segs
    for s in range(segs):
        end = struct.unpack_from(">H", data, ends + 2 * s)[0]
        start = struct.unpack_from(">H", data, starts + 2 * s)[0]
        delta = struct.unpack_from(">h", data, deltas + 2 * s)[0]
        roff = struct.unpack_from(">H", data, roffs + 2 * s)[0]
        for cp in range(start, min(end, 0xFFFE) + 1):
            if roff == 0:
                gid = (cp + delta) & 0xFFFF
            else:
                at = roffs + 2 * s + roff + 2 * (cp - start)
                gid = struct.unpack_from(">H", data, at)[0]
                gid = (gid + delta) & 0xFFFF if gid else 0
            if gid:
                cps.add(cp)
    return cps


class Glyph:
    __slots__ = ("adv", "w", "h", "xo", "yo", "px")

    def __init__(self, adv, w, h, xo, yo, px):
        # px: row-major coverage values 0..255, len w*h
        self.adv, self.w, self.h, self.xo, self.yo, self.px = adv, w, h, xo, yo, px


def crop(adv, w, h, xo, yo, px):
    rows = [r for r in range(h) if any(px[r * w:(r + 1) * w])]
    cols = [c for c in range(w) if any(px[r * w + c] for r in range(h))]
    if not rows:
        return Glyph(adv, 0, 0, 0, 0, [])
    r0, r1, c0, c1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
    out = [px[r * w + c] for r in range(r0, r1) for c in range(c0, c1)]
    return Glyph(adv, c1 - c0, r1 - r0, xo + c0, yo + r0, out)


def load_outline(paths, size, bpp, ranges):
    try:
        from PIL import ImageFont
    except ImportError:
        sys.exit("Pillow is required for TTF/OTF input: pip install pillow")
    glyphs = {}
    metrics = None
    for path in paths:
        font = ImageFont.truetype(path, size)
        if metrics is None:
            metrics = font.getmetrics()
        mode = "1" if bpp == 1 else "L"
        for cp in sorted(sfnt_cmap(path)):
            if cp in glyphs or cp < 0x20 or not in_ranges(cp, ranges):
                continue
            ch = chr(cp)
            adv = int(round(font.getlength(ch, mode=mode)))
            try:
                mask, (ox, oy) = font.getmask2(ch, mode=mode, anchor="ls")
            except (OSError, ValueError):
                continue
            w, h = mask.size
            px = list(mask) if w and h else []
            if bpp == 1:
                px = [255 if v else 0 for v in px]
            glyphs[cp] = crop(min(adv, 255), w, h, ox, oy, px)
    return glyphs, metrics


def load_bdf(path, ranges):
    glyphs, ascent, descent = {}, 0, 0
    cp = adv = None
    lines = iter(open(path, encoding="latin-1").read().splitlines())
    for line in lines:
        key, _, rest = line.partition(" ")
        if key == "FONT_ASCENT":
            ascent = int(rest)
        elif key == "FONT_DESCENT":
            descent = int(rest)
        elif key == "ENCODING":
            cp = int(rest.split()[0])
        elif key == "DWIDTH":
            adv = int(rest.split()[0])
        elif key == "BBX":
            w, h, bx, by = map(int, rest.split())
        elif key == "BITMAP":
            rows = [next(lines) for _ in range(h)]
            if cp is None or cp < 0x20 or not in_ranges(cp, ranges):
                continue
            px = []
            for row in rows:
                bits = int(row, 16) if row else 0
                total = len(row) * 4
                px += [255 if bits >> (total - 1 - x) & 1 else 0
                       for x in range(w)]
            glyphs[cp] = crop(adv or w, w, h, bx, -(by + h), px)
    return glyphs, (ascent, descent)


def pack(glyphs, size, bpp, ascent, descent, line_height, flags):
    cps = sorted(glyphs)
    ranges = []
    for i, cp in enumerate(cps):
        if ranges and ranges[-1][0] + ranges[-1][1] == cp:
            ranges[-1][1] += 1
        else:
            ranges.append([cp, 1, i])
    levels = (1 << bpp) - 1
    table, bitmaps, max_bytes = bytearray(), bytearray(), 0
    for cp in cps:
        g = glyphs[cp]
        acc = nbits = 0
        data = bytearray()
        for v in g.px:
            acc = (acc << bpp) | ((v * levels + 127) // 255)
            nbits += bpp
            if nbits >= 8:
                nbits -= 8
                data.append((acc >> nbits) & 0xFF)
        if nbits:
            data.append((acc << (8 - nbits)) & 0xFF)
        off = len(bitmaps)
        if off >= 1 << 24:
            sys.exit("bitmap area exceeds 16 MiB")
        table += struct.pack("<IBBbb", off | (g.adv << 24), g.w, g.h,
                             max(-128, min(127, g.xo)),
                             max(-128, min(127, g.yo)))
        bitmaps += data
        max_bytes = max(max_bytes, len(data))
    bitmap_offset = 24 + 12 * len(ranges) + 8 * len(cps)
    out = struct.pack("<4sBBBBbBHIHHI", b"IFNT", 1, bpp, size, line_height,
                      ascent, descent, len(ranges), len(cps), max_bytes,
                      flags, bitmap_offset)
    for first, count, base in ranges:
        out += struct.pack("<III", first, count, base)
    return out + bytes(table) + bytes(bitmaps), len(ranges), len(cps)


def write_header(path, symbol, blob, source):
    with open(path, "w") as f:
        f.write(f"// Generated by tools/inkfont-convert.py from {source}.\n")
        f.write("// Do not edit; regenerate instead.\n#pragma once\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"alignas(4) static const uint8_t {symbol}[{len(blob)}] = {{\n")
        for i in range(0, len(blob), 20):
            f.write("    " + ",".join(str(b) for b in blob[i:i + 20]) + ",\n")
        f.write("};\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("fonts", nargs="+",
                    help="source fonts; later files only fill missing glyphs")
    ap.add_argument("--size", type=int, default=0,
                    help="pixel size (outline fonts; BDF uses its own)")
    ap.add_argument("--bpp", type=int, choices=(1, 2, 4), default=1)
    ap.add_argument("--ranges", default="rom",
                    help="named sets (%s) or hex ranges like 20-7e,400-4ff"
                    % ",".join(RANGE_SETS))
    ap.add_argument("--line-height", type=int, default=0)
    ap.add_argument("--header", metavar="SYMBOL",
                    help="emit a C header with this array name")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    ranges = parse_ranges(args.ranges)
    if args.fonts[0].lower().endswith(".bdf"):
        glyphs, (ascent, descent) = load_bdf(args.fonts[0], ranges)
        size = args.size or ascent + descent
        flags, bpp = FLAG_PIXEL, 1
    else:
        if not args.size:
            sys.exit("--size is required for outline fonts")
        glyphs, (ascent, descent) = load_outline(args.fonts, args.size,
                                                 args.bpp, ranges)
        size, bpp = args.size, args.bpp
        flags = FLAG_PIXEL if bpp == 1 else 0
    line_height = args.line_height or ascent + descent
    blob, nranges, nglyphs = pack(glyphs, size, bpp, ascent, descent,
                                  line_height, flags)
    if args.header:
        write_header(args.output, args.header, blob,
                     " + ".join(p.rsplit("/", 1)[-1] for p in args.fonts))
    else:
        open(args.output, "wb").write(blob)
    print(f"{args.output}: {nglyphs} glyphs, {nranges} ranges, "
          f"{len(blob)} bytes")


if __name__ == "__main__":
    main()
