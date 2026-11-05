/// Minimal script support for InkText: UTF-8 decoding, Arabic contextual
/// shaping through Unicode presentation forms, and the bidi classes needed to
/// reorder mixed right-to-left lines. This is a deliberately small subset of
/// UAX #9 (no explicit embeddings) that covers ordinary Arabic, Persian, Urdu
/// and Hebrew text mixed with Latin words and numbers.
#ifndef INKSHAPE_H
#define INKSHAPE_H

#include <stdint.h>

/// Decodes one codepoint and advances `p`; malformed input yields U+FFFD.
uint32_t inkUtf8Next(const char *&p, const char *end);

enum class InkBidiClass : uint8_t { Left, Right, Number, Neutral, Mark };

InkBidiClass inkBidiClass(uint32_t cp);
/// Mirrored bracket for right-to-left runs, or cp itself.
uint32_t inkBidiMirror(uint32_t cp);

enum class InkJoining : uint8_t { None, Right, Dual, Causing, Transparent };

InkJoining inkJoining(uint32_t cp);
/// Presentation form of an Arabic letter given its neighbors' connections, or
/// cp when the letter has no contextual forms.
uint32_t inkArabicForm(uint32_t cp, bool joinsPrev, bool joinsNext);
/// Lam-alef ligature for an alef following lam, or 0 when `alef` is not one.
uint32_t inkLamAlef(uint32_t alef, bool joinsPrev);

/// Zero-width formatting characters that are never drawn.
bool inkInvisible(uint32_t cp);
/// Ideographs, kana and Hangul allow line breaks between any two characters.
bool inkBreaksAnywhere(uint32_t cp);

#endif
