#include "ui/inkfont/inkshape.h"

namespace {

constexpr uint32_t kReplacement = 0xFFFD;

// Contextual form counts for U+0621..U+064A in Presentation Forms-B order
// (isolated, final, initial, medial). Isolated forms are consecutive from
// U+FE80, so the counts alone locate every form. 0 = no forms.
constexpr uint8_t kArabicForms[] = {
    1, 2, 2, 2, 2, 4, 2, 4, 2, 4, 4, 4, 4, 4, 2, 2, 2, 2, 4, 4, 4,
    4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 2, 2, 4,
};

struct ExtraForm {
  uint16_t cp;
  uint16_t isolated;
  uint8_t forms;
};

// Persian and Urdu letters from Presentation Forms-A.
constexpr ExtraForm kExtraForms[] = {
    {0x0671, 0xFB50, 2}, {0x067E, 0xFB56, 4}, {0x0686, 0xFB7A, 4},
    {0x0698, 0xFB8A, 2}, {0x06A9, 0xFB8E, 4}, {0x06AF, 0xFB92, 4},
    {0x06BA, 0xFB9E, 2}, {0x06BE, 0xFBAA, 4}, {0x06C1, 0xFBA6, 4},
    {0x06CC, 0xFBFC, 4}, {0x06D2, 0xFBAE, 2},
};

bool between(uint32_t cp, uint32_t lo, uint32_t hi) {
  return cp >= lo && cp <= hi;
}

bool arabicMark(uint32_t cp) {
  return between(cp, 0x0610, 0x061A) || between(cp, 0x064B, 0x065F) ||
         cp == 0x0670 || between(cp, 0x06D6, 0x06DC) ||
         between(cp, 0x06DF, 0x06E4) || cp == 0x06E7 || cp == 0x06E8 ||
         between(cp, 0x06EA, 0x06ED);
}

bool combiningMark(uint32_t cp) {
  return between(cp, 0x0300, 0x036F) || between(cp, 0x0591, 0x05BD) ||
         cp == 0x05BF || cp == 0x05C1 || cp == 0x05C2 || cp == 0x05C4 ||
         cp == 0x05C5 || cp == 0x05C7 || arabicMark(cp) ||
         between(cp, 0x1AB0, 0x1AFF) || between(cp, 0x1DC0, 0x1DFF) ||
         between(cp, 0x20D0, 0x20FF) || between(cp, 0xFE20, 0xFE2F);
}

// Returns the isolated form and form count for a shapeable letter.
uint8_t formsOf(uint32_t cp, uint32_t &isolated) {
  if (between(cp, 0x0621, 0x064A)) {
    isolated = 0xFE80;
    for (uint32_t c = 0x0621; c < cp; c++) {
      isolated += kArabicForms[c - 0x0621];
    }
    return kArabicForms[cp - 0x0621];
  }
  for (const ExtraForm &extra : kExtraForms) {
    if (extra.cp == cp) {
      isolated = extra.isolated;
      return extra.forms;
    }
  }
  return 0;
}

} // namespace

uint32_t inkUtf8Next(const char *&p, const char *end) {
  const uint8_t lead = static_cast<uint8_t>(*p++);
  if (lead < 0x80) {
    return lead;
  }
  uint8_t extra;
  uint32_t cp;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    cp = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    cp = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    cp = lead & 0x07;
  } else {
    return kReplacement;
  }
  for (uint8_t i = 0; i < extra; i++) {
    if (p >= end || (static_cast<uint8_t>(*p) & 0xC0) != 0x80) {
      return kReplacement;
    }
    cp = (cp << 6) | (static_cast<uint8_t>(*p++) & 0x3F);
  }
  // Reject overlong forms, surrogates and out-of-range values.
  static const uint32_t kMin[] = {0, 0x80, 0x800, 0x10000};
  if (cp < kMin[extra] || cp > 0x10FFFF || between(cp, 0xD800, 0xDFFF)) {
    return kReplacement;
  }
  return cp;
}

InkBidiClass inkBidiClass(uint32_t cp) {
  if (cp < 0x80) {
    if (between(cp, '0', '9')) {
      return InkBidiClass::Number;
    }
    return between(cp, 'A', 'Z') || between(cp, 'a', 'z') ? InkBidiClass::Left
                                                         : InkBidiClass::Neutral;
  }
  if (combiningMark(cp)) {
    return InkBidiClass::Mark;
  }
  if (between(cp, 0x0660, 0x0669) || between(cp, 0x06F0, 0x06F9)) {
    return InkBidiClass::Number;
  }
  if (cp == 0x200F) {
    return InkBidiClass::Right;
  }
  if (cp == 0x200E) {
    return InkBidiClass::Left;
  }
  if (between(cp, 0x0590, 0x08FF) || between(cp, 0xFB1D, 0xFDFF) ||
      between(cp, 0xFE70, 0xFEFF) || between(cp, 0x10800, 0x10FFF) ||
      between(cp, 0x1E800, 0x1EFFF)) {
    return InkBidiClass::Right;
  }
  if (between(cp, 0x80, 0xBF) || cp == 0xD7 || cp == 0xF7 ||
      between(cp, 0x2000, 0x2BFF) || between(cp, 0x3000, 0x3003) ||
      between(cp, 0xFE50, 0xFE6F) || between(cp, 0xFF01, 0xFF0F)) {
    return InkBidiClass::Neutral;
  }
  return InkBidiClass::Left;
}

uint32_t inkBidiMirror(uint32_t cp) {
  static const uint16_t kPairs[][2] = {
      {'(', ')'}, {'<', '>'}, {'[', ']'}, {'{', '}'},
      {0xAB, 0xBB}, {0x2039, 0x203A},
  };
  for (const auto &pair : kPairs) {
    if (cp == pair[0]) {
      return pair[1];
    }
    if (cp == pair[1]) {
      return pair[0];
    }
  }
  return cp;
}

InkJoining inkJoining(uint32_t cp) {
  if (cp == 0x0640 || cp == 0x200D) {
    return InkJoining::Causing;
  }
  if (arabicMark(cp)) {
    return InkJoining::Transparent;
  }
  uint32_t isolated;
  switch (formsOf(cp, isolated)) {
  case 4:
    return InkJoining::Dual;
  case 2:
    return InkJoining::Right;
  default:
    return InkJoining::None;
  }
}

uint32_t inkArabicForm(uint32_t cp, bool joinsPrev, bool joinsNext) {
  uint32_t isolated;
  const uint8_t forms = formsOf(cp, isolated);
  if (forms < 2) {
    return forms == 1 ? isolated : cp;
  }
  if (forms == 2) {
    joinsNext = false;
  }
  if (joinsPrev && joinsNext) {
    return isolated + 3;
  }
  if (joinsNext) {
    return isolated + 2;
  }
  return joinsPrev ? isolated + 1 : isolated;
}

uint32_t inkLamAlef(uint32_t alef, bool joinsPrev) {
  uint32_t base;
  switch (alef) {
  case 0x0622:
    base = 0xFEF5;
    break;
  case 0x0623:
    base = 0xFEF7;
    break;
  case 0x0625:
    base = 0xFEF9;
    break;
  case 0x0627:
    base = 0xFEFB;
    break;
  default:
    return 0;
  }
  return joinsPrev ? base + 1 : base;
}

bool inkInvisible(uint32_t cp) {
  return cp == '\r' || cp == 0xAD || between(cp, 0x200B, 0x200F) ||
         between(cp, 0x202A, 0x202E) || between(cp, 0x2060, 0x2064) ||
         cp == 0xFEFF || between(cp, 0xFE00, 0xFE0F);
}

bool inkBreaksAnywhere(uint32_t cp) {
  return between(cp, 0x2E80, 0x9FFF) || between(cp, 0xAC00, 0xD7A3) ||
         between(cp, 0xF900, 0xFAFF) || between(cp, 0xFF00, 0xFF60) ||
         between(cp, 0x20000, 0x3FFFF);
}
