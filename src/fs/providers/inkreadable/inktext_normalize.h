#ifndef INKTEXT_NORMALIZE_H
#define INKTEXT_NORMALIZE_H

#include <stddef.h>
#include <stdint.h>

struct InkTextAsciiReplacement {
  uint16_t codepoint;
  const char *ascii;
};

static const InkTextAsciiReplacement INK_TEXT_ASCII_REPLACEMENTS[] = {
    {0x00a0, " "},   {0x00a2, "cent"}, {0x00a3, "GBP"},
    {0x00a5, "JPY"}, {0x00a7, "Sec."}, {0x00a9, "(c)"},
    {0x00ab, "\""},  {0x00ad, ""},     {0x00ae, "(R)"},
    {0x00b0, "deg"},
    {0x00b6, "P"},   {0x00bb, "\""},
    {0x00c0, "A"},   {0x00c1, "A"},    {0x00c2, "A"},
    {0x00c3, "A"},   {0x00c4, "A"},    {0x00c5, "A"},
    {0x00c6, "AE"},  {0x00c7, "C"},    {0x00c8, "E"},
    {0x00c9, "E"},   {0x00ca, "E"},    {0x00cb, "E"},
    {0x00cc, "I"},   {0x00cd, "I"},    {0x00ce, "I"},
    {0x00cf, "I"},   {0x00d0, "D"},    {0x00d1, "N"},
    {0x00d2, "O"},   {0x00d3, "O"},    {0x00d4, "O"},
    {0x00d5, "O"},   {0x00d6, "O"},    {0x00d7, "x"},
    {0x00d8, "O"},   {0x00d9, "U"},    {0x00da, "U"},
    {0x00db, "U"},   {0x00dc, "U"},    {0x00dd, "Y"},
    {0x00de, "TH"},  {0x00df, "ss"},   {0x00e0, "a"},
    {0x00e1, "a"},   {0x00e2, "a"},    {0x00e3, "a"},
    {0x00e4, "a"},   {0x00e5, "a"},    {0x00e6, "ae"},
    {0x00e7, "c"},   {0x00e8, "e"},    {0x00e9, "e"},
    {0x00ea, "e"},   {0x00eb, "e"},    {0x00ec, "i"},
    {0x00ed, "i"},   {0x00ee, "i"},    {0x00ef, "i"},
    {0x00f0, "d"},   {0x00f1, "n"},    {0x00f2, "o"},
    {0x00f3, "o"},   {0x00f4, "o"},    {0x00f5, "o"},
    {0x00f6, "o"},   {0x00f7, "/"},    {0x00f8, "o"},
    {0x00f9, "u"},   {0x00fa, "u"},    {0x00fb, "u"},
    {0x00fc, "u"},   {0x00fd, "y"},    {0x00fe, "th"},
    {0x00ff, "y"},   {0x0110, "D"},    {0x0111, "d"},
    {0x0131, "i"},   {0x0132, "IJ"},   {0x0133, "ij"},
    {0x0141, "L"},   {0x0142, "l"},    {0x0149, "'n"},
    {0x0152, "OE"},  {0x0153, "oe"},   {0x0160, "S"},
    {0x0161, "s"},   {0x0178, "Y"},    {0x017d, "Z"},
    {0x017e, "z"},   {0x017f, "s"},    {0x0189, "D"},
    {0x02bb, "'"},   {0x02bc, "'"},    {0x061c, ""},
    {0x180e, ""},    {0x2009, " "},    {0x200b, ""},
    {0x200c, ""},    {0x200d, ""},     {0x200e, ""},
    {0x200f, ""},    {0x2013, "-"},    {0x2014, "-"},
    {0x2018, "'"},   {0x2019, "'"},    {0x201b, "'"},
    {0x201c, "\""},  {0x201d, "\""},   {0x201e, "\""},
    {0x201f, "\""},  {0x2020, "+"},    {0x2021, "++"},
    {0x2022, "*"},   {0x2026, "..."},  {0x202a, ""},
    {0x202b, ""},    {0x202c, ""},     {0x202d, ""},
    {0x202e, ""},    {0x202f, " "},    {0x2032, "'"},
    {0x2060, ""},    {0x20ac, "EUR"},  {0x2122, "(TM)"},
    {0x2212, "-"},   {0xfb00, "ff"},   {0xfb01, "fi"},
    {0xfb02, "fl"},  {0xfb03, "ffi"},  {0xfb04, "ffl"},
    {0xfb05, "st"},  {0xfb06, "st"},   {0xfeff, ""},
    {0xff07, "'"},
};

static const char *inkTextAsciiReplacement(uint32_t codepoint) {
  if (codepoint > 0xffffU) {
    return nullptr;
  }
  uint16_t lo = 0;
  uint16_t hi = static_cast<uint16_t>(
      sizeof(INK_TEXT_ASCII_REPLACEMENTS) /
      sizeof(INK_TEXT_ASCII_REPLACEMENTS[0]));
  while (lo < hi) {
    const uint16_t mid = static_cast<uint16_t>(lo + ((hi - lo) >> 1));
    const uint16_t candidate = INK_TEXT_ASCII_REPLACEMENTS[mid].codepoint;
    if (candidate == codepoint) {
      return INK_TEXT_ASCII_REPLACEMENTS[mid].ascii;
    }
    if (candidate < codepoint) {
      lo = static_cast<uint16_t>(mid + 1U);
    } else {
      hi = mid;
    }
  }
  return nullptr;
}

#endif
