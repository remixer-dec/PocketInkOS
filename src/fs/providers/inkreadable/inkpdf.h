/*
 * InkPDF - header-only PDF parser and text extractor for low-resource environments.
 * Only limited subset of PDF features is present to achieve pdf to text conversion.
 * (c) Remixer Dec 2026 | Licensed under CC BY-NC-SA 3.0
 * Distributed as a part of PocketInkOS https://github.com/remixer-dec/PocketInkOS
 */


#ifndef INKPDF_H
#define INKPDF_H

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

static const uint8_t kInkPdfTextColumns = 31;
static const uint8_t kInkPdfTextRows = 16;

enum class InkPdfResult : int8_t {
  Ok = 0,
  Done = 1,
  Io = -1,
  Format = -2,
  Memory = -3,
};

struct InkPdfTextLine {
  const char *text = nullptr;
  bool firstInStream = false;
  bool lastInStream = false;
  bool suppress = false;
};

struct InkPdfScreenInfo {
  uint16_t streamIndex = 0;
  uint16_t skipLines = 0;
  uint16_t lineCount = 0;
  bool firstScreenInStream = false;
  bool lastScreenInStream = false;
  bool hasNextStream = false;
  uint32_t decodeUs = 0;
};

typedef void (*InkPdfTextLineHandler)(const InkPdfTextLine &line,
                                      void *context);

struct InkPdfHooks {
  File (*openFile)(const char *providerId, const char *path) = nullptr;
  void *(*alloc)(size_t bytes) = nullptr;
  void (*free)(void *ptr) = nullptr;
  uint32_t (*micros)() = nullptr;
  bool normalizeText = false;
};

void inkPdfSetHooks(const InkPdfHooks &hooks);
InkPdfResult inkPdfOpen(const char *providerId, const char *path,
                        uint32_t *outSize);
InkPdfResult inkPdfContinueIndex(const char *providerId, const char *path,
                                 uint32_t budgetUs);
bool inkPdfReady(const char *providerId, const char *path);
bool inkPdfLoading(const char *providerId, const char *path);
uint8_t inkPdfProgress(const char *providerId, const char *path);
uint32_t inkPdfScreenCount(const char *providerId, const char *path);
InkPdfResult inkPdfExtractScreenText(const char *providerId, const char *path,
                                     uint32_t screen,
                                     InkPdfTextLineHandler handler,
                                     void *context,
                                     InkPdfScreenInfo *outInfo);

#endif

#ifdef INKPDF_IMPLEMENTATION
#ifndef INKPDF_IMPLEMENTATION_INCLUDED
#define INKPDF_IMPLEMENTATION_INCLUDED

#include <ctype.h>
#include <new>
#include <stdio.h>
#include <string.h>

InkPdfHooks inkPdfHooks;

void inkPdfSetHooks(const InkPdfHooks &hooks) {
  inkPdfHooks = hooks;
}

namespace {

static const uint8_t kTextColumns = kInkPdfTextColumns;
static const uint8_t kTextRows = kInkPdfTextRows;
static const uint16_t kStoredTextLines = 24;
static const float kDefaultPageHeight = 792.0f;
static const float kHeaderFooterBand = 0.20f;
static const float kCoordEpsilon = 1.0f;
static const uint16_t kDictLookback = 2048;
static const uint16_t kMaxPdfStreams = 256;
static const uint8_t kMaxPdfFontMaps = 24;
static const uint16_t kMaxPdfCMapEntries = 768;
static const size_t kMaxPdfCMapBytes = 12288;

using PdfResult = InkPdfResult;

struct PdfStreamInfo {
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t screenStart = 0;
  uint32_t firstHash = 0;
  uint32_t lastHash = 0;
  float firstX = 0.0f;
  float firstY = 0.0f;
  float lastX = 0.0f;
  float lastY = 0.0f;
  uint16_t lineCount = 0;
  uint8_t firstLen = 0;
  uint8_t lastLen = 0;
  bool flate = false;
  bool firstHasPosition = false;
  bool lastHasPosition = false;
  bool suppressFirst = false;
  bool suppressLast = false;
};

struct PdfCMapEntry {
  uint16_t code = 0;
  char text = '\0';
};

struct PdfFontMap {
  char resource[12] = {};
  uint32_t fontObject = 0;
  uint16_t fontGeneration = 0;
  uint8_t codeBytes = 1;
  uint16_t entryStart = 0;
  uint16_t entryCount = 0;
};

struct PdfPageCache {
  bool valid = false;
  bool indexing = false;
  char providerId[12] = {};
  char path[288] = {};
  uint32_t size = 0;
  uint32_t searchFrom = 0;
  uint32_t startedAt = 0;
  uint32_t totalDecodeUs = 0;
  uint16_t streamCount = 0;
  uint16_t candidates = 0;
  uint16_t decoded = 0;
  uint16_t decodeFailed = 0;
  uint16_t noText = 0;
  uint32_t screenCount = 0;
  uint8_t fontMapCount = 0;
  uint16_t fontEntryCount = 0;
  bool truncated = false;
  PdfCMapEntry fontEntries[kMaxPdfCMapEntries];
  PdfFontMap fontMaps[kMaxPdfFontMaps];
  PdfStreamInfo streams[kMaxPdfStreams];
};

PdfPageCache pdfCache;

void resetPdfCache() {
  pdfCache.~PdfPageCache();
  new (&pdfCache) PdfPageCache();
}

File inkPdfOpenFile(const char *providerId, const char *path) {
  if (inkPdfHooks.openFile == nullptr) {
    return File();
  }
  return inkPdfHooks.openFile(providerId, path);
}

uint32_t inkPdfMicros() {
  return inkPdfHooks.micros != nullptr ? inkPdfHooks.micros() : 0;
}

void *inkPdfMalloc(size_t bytes) {
  return inkPdfHooks.alloc != nullptr ? inkPdfHooks.alloc(bytes) : nullptr;
}

void inkPdfFree(void *ptr) {
  if (ptr != nullptr && inkPdfHooks.free != nullptr) {
    inkPdfHooks.free(ptr);
  }
}

bool copyText(char *dest, size_t destSize, const char *source) {
  const char *text = source != nullptr ? source : "";
  if (destSize == 0) {
    return text[0] == '\0';
  }
  const size_t length = strlen(text);
  const size_t copyLength = length < destSize ? length : destSize - 1;
  if (copyLength > 0) {
    memcpy(dest, text, copyLength);
  }
  dest[copyLength] = '\0';
  return text[copyLength] == '\0';
}

char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

bool equalsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    if (asciiLower(*left++) != asciiLower(*right++)) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

const PdfFontMap *fontMapForResource(const char *resource) {
  if (resource == nullptr || resource[0] == '\0') {
    return nullptr;
  }
  for (uint8_t i = 0; i < pdfCache.fontMapCount; i++) {
    const PdfFontMap &map = pdfCache.fontMaps[i];
    if (map.entryCount > 0 && strcmp(map.resource, resource) == 0) {
      return &map;
    }
  }
  return nullptr;
}

bool lookupFontChar(const PdfFontMap *map, uint16_t code, char *out) {
  if (map == nullptr || out == nullptr) {
    return false;
  }
  if (map->entryStart > pdfCache.fontEntryCount ||
      map->entryCount > pdfCache.fontEntryCount - map->entryStart) {
    return false;
  }
  for (uint16_t i = 0; i < map->entryCount; i++) {
    const PdfCMapEntry &entry = pdfCache.fontEntries[map->entryStart + i];
    if (entry.code == code) {
      *out = entry.text;
      return true;
    }
  }
  return false;
}

class PdfByteOutput {
public:
  virtual ~PdfByteOutput() = default;
  virtual void feedByte(uint8_t byte) = 0;
  virtual bool completeBytes() const { return false; }
};

class PdfTextOutput {
public:
  virtual ~PdfTextOutput() = default;
  virtual void setTextPosition(float, float) {}
  virtual void emit(char c) = 0;
  virtual void space() = 0;
  virtual void newline() = 0;
  virtual bool complete() const { return false; }
};

class PdfTextNormalizer : public PdfTextOutput {
public:
  explicit PdfTextNormalizer(PdfTextOutput &out) : out(out) {}

  void setTextPosition(float x, float y) override {
    flushLiteralDotAfterNewline();
    flushPendingNewline();
    flushPendingChar();
    out.setTextPosition(x, y);
  }

  void emit(char c) override {
    if (c == '\r') {
      return;
    }
    if (c == '\n') {
      newline();
      return;
    }

    if (pendingDotAfterNewline) {
      flushLiteralDotAfterNewline();
    }
    if (pendingNewline) {
      if (c == '.') {
        pendingNewline = false;
        pendingDotAfterNewline = true;
        return;
      }
      flushPendingNewline();
    }
    flushPendingChar();
    pendingChar = c;
    hasPendingChar = true;
  }

  void space() override {
    flushLiteralDotAfterNewline();
    flushPendingNewline();
    flushPendingChar();
    out.space();
  }

  void newline() override {
    if (pendingDotAfterNewline) {
      out.emit('.');
      pendingDotAfterNewline = false;
      pendingNewline = true;
      return;
    }
    if (hasPendingChar && pendingChar == '-') {
      hasPendingChar = false;
      pendingNewline = false;
      return;
    }
    flushPendingChar();
    if (pendingNewline) {
      out.newline();
    }
    pendingNewline = true;
  }

  bool complete() const override { return out.complete(); }

  void finish() {
    flushLiteralDotAfterNewline();
    flushPendingNewline();
    flushPendingChar();
  }

private:
  void flushPendingChar() {
    if (!hasPendingChar) {
      return;
    }
    out.emit(pendingChar);
    hasPendingChar = false;
  }

  void flushPendingNewline() {
    if (!pendingNewline) {
      return;
    }
    out.newline();
    pendingNewline = false;
  }

  void flushLiteralDotAfterNewline() {
    if (!pendingDotAfterNewline) {
      return;
    }
    out.newline();
    out.emit('.');
    pendingDotAfterNewline = false;
  }

  PdfTextOutput &out;
  char pendingChar = '\0';
  bool hasPendingChar = false;
  bool pendingNewline = false;
  bool pendingDotAfterNewline = false;
};

class PdfPageText : public PdfTextOutput {
public:
  PdfPageText(uint16_t skipLines = 0, uint16_t maxLines = kTextRows)
      : skipLines(skipLines), maxLines(maxLines) {}

  void setTextPosition(float x, float y) override {
    pendingX = x;
    pendingY = y;
    pendingPosition = true;
  }

  void emit(char c) override {
    sawText = true;
    if (c == '\r') {
      return;
    }
    if (c == '\n') {
      newline();
      return;
    }
    if (c == '\t') {
      c = ' ';
    }
    if (c < 0x20 || c > 0x7e) {
      c = '?';
    }
    if (logicalLineLen >= kTextColumns) {
      newline();
    }
    if (collecting() && lineLen < kTextColumns) {
      if (lineLen == 0 && pendingPosition) {
        lineX[lineCount] = pendingX;
        lineY[lineCount] = pendingY;
        lineHasPosition[lineCount] = true;
      }
      lines[lineCount][lineLen++] = c;
      lines[lineCount][lineLen] = '\0';
    }
    logicalLineLen++;
  }

  void space() override {
    if (logicalLineLen == 0) {
      return;
    }
    if (!collecting() || lineLen == 0 || lines[lineCount][lineLen - 1] != ' ') {
      emit(' ');
    }
  }

  void newline() override {
    if (complete()) {
      return;
    }
    if (collecting()) {
      trimCurrent();
      lineCount++;
    }
    currentLine++;
    lineLen = 0;
    logicalLineLen = 0;
    if (collecting()) {
      lines[lineCount][0] = '\0';
    }
  }

  void finish() {
    if (logicalLineLen > 0 || (currentLine == 0 && sawText)) {
      newline();
    }
    while (lineCount > 0 && lines[lineCount - 1][0] == '\0') {
      lineCount--;
    }
  }

  bool complete() const override { return lineCount >= maxLines; }
  bool anyText() const { return sawText; }
  uint16_t count() const { return lineCount; }
  const char *line(uint16_t index) const {
    return index < lineCount ? lines[index] : "";
  }
  bool linePosition(uint16_t index, float *x, float *y) const {
    if (index >= lineCount || !lineHasPosition[index]) {
      return false;
    }
    if (x != nullptr) {
      *x = lineX[index];
    }
    if (y != nullptr) {
      *y = lineY[index];
    }
    return true;
  }

private:
  bool collecting() const {
    return currentLine >= skipLines && lineCount < maxLines &&
           lineCount < kStoredTextLines;
  }

  void trimCurrent() {
    if (!collecting()) {
      return;
    }
    while (lineLen > 0 && lines[lineCount][lineLen - 1] == ' ') {
      lineLen--;
    }
    lines[lineCount][lineLen] = '\0';
  }

  char lines[kStoredTextLines][kTextColumns + 1] = {};
  float lineX[kStoredTextLines] = {};
  float lineY[kStoredTextLines] = {};
  bool lineHasPosition[kStoredTextLines] = {};
  uint16_t skipLines = 0;
  uint16_t currentLine = 0;
  uint16_t maxLines = kTextRows;
  uint16_t lineCount = 0;
  uint8_t lineLen = 0;
  uint8_t logicalLineLen = 0;
  bool sawText = false;
  float pendingX = 0.0f;
  float pendingY = 0.0f;
  bool pendingPosition = false;
};

uint32_t hashLineByte(uint32_t hash, char c) {
  return (hash ^ static_cast<uint8_t>(c)) * 16777619UL;
}

bool sameCoord(float left, float right) {
  const float delta = left > right ? left - right : right - left;
  return delta <= kCoordEpsilon;
}

bool inHeaderFooterBand(float y) {
  return y <= kDefaultPageHeight * kHeaderFooterBand ||
         y >= kDefaultPageHeight * (1.0f - kHeaderFooterBand);
}

class PdfLineCountSink : public PdfTextOutput {
public:
  void setTextPosition(float x, float y) override {
    pendingX = x;
    pendingY = y;
    pendingPosition = true;
  }

  void emit(char c) override {
    sawText = true;
    if (c == '\r') {
      return;
    }
    if (c == '\n') {
      newline();
      return;
    }
    if (c == '\t') {
      c = ' ';
    }
    if (lineLen >= kTextColumns) {
      newline();
    }
    if (c >= 0x20) {
      if (lineLen == 0 && pendingPosition) {
        currentX = pendingX;
        currentY = pendingY;
        currentHasPosition = true;
      }
      currentHash = hashLineByte(currentHash, c);
      lineLen++;
    }
  }

  void space() override {
    if (lineLen > 0) {
      emit(' ');
    }
  }

  void newline() override {
    if (complete()) {
      return;
    }
    if (lineLen > 0 || lineCount == 0) {
      finishLineSignature();
      lineCount++;
    }
    lineLen = 0;
    currentHash = 2166136261UL;
    currentHasPosition = false;
  }

  void finish() {
    if (!complete() && (lineLen > 0 || (lineCount == 0 && sawText))) {
      lineCount++;
    }
  }

  bool complete() const override { return lineCount >= 65534U; }
  bool anyText() const { return sawText; }
  uint16_t count() const { return lineCount; }
  void applyTo(PdfStreamInfo &info) const {
    info.lineCount = lineCount;
    info.firstHash = firstHash;
    info.lastHash = lastHash;
    info.firstLen = firstLen;
    info.lastLen = lastLen;
    info.firstX = firstX;
    info.firstY = firstY;
    info.lastX = lastX;
    info.lastY = lastY;
    info.firstHasPosition = firstHasPosition;
    info.lastHasPosition = lastHasPosition;
  }

private:
  void finishLineSignature() {
    if (lineLen == 0) {
      return;
    }
    if (firstLen == 0) {
      firstHash = currentHash;
      firstLen = lineLen;
      firstX = currentX;
      firstY = currentY;
      firstHasPosition = currentHasPosition;
    }
    lastHash = currentHash;
    lastLen = lineLen;
    lastX = currentX;
    lastY = currentY;
    lastHasPosition = currentHasPosition;
  }

  uint16_t lineCount = 0;
  uint8_t lineLen = 0;
  bool sawText = false;
  uint32_t currentHash = 2166136261UL;
  uint32_t firstHash = 0;
  uint32_t lastHash = 0;
  float pendingX = 0.0f;
  float pendingY = 0.0f;
  float currentX = 0.0f;
  float currentY = 0.0f;
  float firstX = 0.0f;
  float firstY = 0.0f;
  float lastX = 0.0f;
  float lastY = 0.0f;
  uint8_t firstLen = 0;
  uint8_t lastLen = 0;
  bool pendingPosition = false;
  bool currentHasPosition = false;
  bool firstHasPosition = false;
  bool lastHasPosition = false;
};

char winAnsiByte(uint8_t byte) {
  if (byte >= 0x20 && byte <= 0x7e) {
    return static_cast<char>(byte);
  }
  switch (byte) {
  case 0x85:
    return '\n';
  case 0x91:
  case 0x92:
    return '\'';
  case 0x93:
  case 0x94:
    return '"';
  case 0x95:
    return '*';
  case 0x96:
  case 0x97:
    return '-';
  case 0xa0:
    return ' ';
  default:
    return byte < 0x20 ? '\0' : '?';
  }
}

char unicodeToAscii(uint16_t code) {
  if (code >= 0x20 && code <= 0x7e) {
    return static_cast<char>(code);
  }
  if (code == 0x0009) {
    return '\t';
  }
  if (code == 0x000a || code == 0x000d) {
    return '\n';
  }
  if (code == 0x00a0) {
    return ' ';
  }
  if (code == 0x2018 || code == 0x2019) {
    return '\'';
  }
  if (code == 0x201c || code == 0x201d) {
    return '"';
  }
  if (code == 0x2010 || code == 0x2011 || code == 0x2012 ||
      code == 0x2013 || code == 0x2014 || code == 0x2212) {
    return '-';
  }
  if (code == 0x2022) {
    return '*';
  }
  return code == 0 ? '\0' : '?';
}

class PdfContentParser {
public:
  explicit PdfContentParser(PdfTextOutput &out) : out(out) {}

  void feed(uint8_t byte) {
    if (out.complete()) {
      return;
    }
    const char c = static_cast<char>(byte);
    switch (state) {
    case State::Normal:
      feedNormal(c);
      break;
    case State::Comment:
      if (c == '\r' || c == '\n') {
        state = State::Normal;
      }
      break;
    case State::Literal:
      feedLiteral(byte);
      break;
    case State::LiteralEscape:
      feedLiteralEscape(byte);
      break;
    case State::LiteralOctal:
      feedLiteralOctal(byte);
      break;
    case State::HexString:
      feedHexString(byte);
      break;
    }
  }

  void finish() {
    finishToken();
    clearPending();
  }

  bool complete() const { return out.complete(); }

private:
  enum class State : uint8_t {
    Normal,
    Comment,
    Literal,
    LiteralEscape,
    LiteralOctal,
    HexString,
  };

  enum class StringEncoding : uint8_t {
    Unknown,
    Latin,
    Utf16BE,
    Utf16LE,
  };

  static bool whitespace(char c) {
    return c == '\0' || c == '\t' || c == '\n' || c == '\f' || c == '\r' ||
           c == ' ';
  }

  static bool delimiter(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
           c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
  }

  static bool octalDigit(uint8_t c) { return c >= '0' && c <= '7'; }

  static int hexDigit(uint8_t c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  }

  static bool isNumericToken(const char *text) {
    if (text == nullptr || text[0] == '\0') {
      return false;
    }
    if (text[0] == '+' || text[0] == '-' || text[0] == '.') {
      text++;
    }
    bool digit = false;
    while (*text != '\0') {
      if (*text >= '0' && *text <= '9') {
        digit = true;
      } else if (*text != '.') {
        return false;
      }
      text++;
    }
    return digit;
  }

  void feedNormal(char c) {
    if (whitespace(c)) {
      finishToken();
      return;
    }
    if (c == '%') {
      finishToken();
      state = State::Comment;
      return;
    }
    if (c == '(') {
      finishToken();
      beginString(false);
      literalDepth = 1;
      state = State::Literal;
      return;
    }
    if (c == '<') {
      finishToken();
      beginString(true);
      hexHaveNibble = false;
      state = State::HexString;
      return;
    }
    if (c == '[') {
      finishToken();
      if (arrayDepth < 8) {
        arrayDepth++;
      }
      return;
    }
    if (c == ']') {
      finishToken();
      if (arrayDepth > 0) {
        arrayDepth--;
      }
      return;
    }
    if (delimiter(c)) {
      finishToken();
      tokenAppend(c);
      finishToken();
      return;
    }
    tokenAppend(c);
  }

  void feedLiteral(uint8_t byte) {
    if (byte == '\\') {
      state = State::LiteralEscape;
      return;
    }
    if (byte == '(') {
      literalDepth++;
      stringByte(byte);
      return;
    }
    if (byte == ')') {
      if (literalDepth > 0) {
        literalDepth--;
      }
      if (literalDepth == 0) {
        endString();
        state = State::Normal;
        return;
      }
      stringByte(byte);
      return;
    }
    stringByte(byte);
  }

  void feedLiteralEscape(uint8_t byte) {
    if (octalDigit(byte)) {
      octalValue = byte - '0';
      octalDigits = 1;
      state = State::LiteralOctal;
      return;
    }
    switch (byte) {
    case 'n':
      stringByte('\n');
      break;
    case 'r':
      stringByte('\r');
      break;
    case 't':
      stringByte('\t');
      break;
    case 'b':
      stringByte('\b');
      break;
    case 'f':
      stringByte('\f');
      break;
    case '\r':
    case '\n':
      break;
    default:
      stringByte(byte);
      break;
    }
    state = State::Literal;
  }

  void feedLiteralOctal(uint8_t byte) {
    if (octalDigits < 3 && octalDigit(byte)) {
      octalValue = static_cast<uint8_t>((octalValue << 3) + (byte - '0'));
      octalDigits++;
      return;
    }
    stringByte(octalValue);
    state = State::Literal;
    feedLiteral(byte);
  }

  void feedHexString(uint8_t byte) {
    if (byte == '<' && prefixLen == 0 && !hexHaveNibble) {
      state = State::Normal;
      return;
    }
    if (byte == '>') {
      if (hexHaveNibble) {
        stringByte(static_cast<uint8_t>(hexNibble << 4));
      }
      endString();
      state = State::Normal;
      return;
    }
    if (whitespace(static_cast<char>(byte))) {
      return;
    }
    const int value = hexDigit(byte);
    if (value < 0) {
      endString();
      state = State::Normal;
      return;
    }
    if (!hexHaveNibble) {
      hexNibble = static_cast<uint8_t>(value);
      hexHaveNibble = true;
      return;
    }
    stringByte(static_cast<uint8_t>((hexNibble << 4) | value));
    hexHaveNibble = false;
  }

  void tokenAppend(char c) {
    if (static_cast<size_t>(tokenLen) + 1U >= sizeof(token)) {
      return;
    }
    token[tokenLen++] = c;
    token[tokenLen] = '\0';
  }

  void finishToken() {
    if (tokenLen == 0) {
      return;
    }

    bool nameToken = false;
    if (expectingName && strcmp(token, "/") != 0) {
      copyText(lastName, sizeof(lastName), token);
      expectingName = false;
      nameToken = true;
    }
    noteMarkedContentToken();

    if (strcmp(token, "/") == 0) {
      expectingName = true;
      clearToken();
      return;
    }

    if (inText && arrayDepth > 0 && isNumericToken(token)) {
      const float adjust = static_cast<float>(atof(token));
      if (adjust < -120.0f) {
        appendDecoded(' ');
      }
      clearToken();
      return;
    }
    if (isNumericToken(token)) {
      pushOperand(static_cast<float>(atof(token)));
      clearToken();
      return;
    }

    if (strcmp(token, "BDC") == 0 || strcmp(token, "BMC") == 0) {
      if (artifactCandidate && artifactPagination) {
        artifactDepth++;
      }
      artifactCandidate = false;
      artifactPagination = false;
    } else if (strcmp(token, "EMC") == 0) {
      if (artifactDepth > 0) {
        artifactDepth--;
      }
    } else if (strcmp(token, "BT") == 0) {
      inText = true;
      emittedInBlock = false;
      clearPending();
      clearOperands();
    } else if (strcmp(token, "ET") == 0) {
      clearPending();
      if (inText && emittedInBlock) {
        out.newline();
      }
      inText = false;
      clearOperands();
    } else if (inText && strcmp(token, "Tf") == 0) {
      activeFont = fontMapForResource(lastName);
    } else if (inText && (strcmp(token, "Tj") == 0 || strcmp(token, "TJ") == 0)) {
      flushPending();
    } else if (inText && (strcmp(token, "'") == 0 || strcmp(token, "\"") == 0)) {
      if (emittedInBlock) {
        out.newline();
      }
      flushPending();
    } else if (inText && (strcmp(token, "T*") == 0 || strcmp(token, "Td") == 0 ||
                          strcmp(token, "TD") == 0 || strcmp(token, "Tm") == 0)) {
      clearPending();
      updateTextPosition(token);
      if (emittedInBlock) {
        out.newline();
      }
    } else if (pendingLen > 0 && !isOperandToken(token) && !nameToken) {
      clearPending();
    }

    clearOperands();
    clearToken();
  }

  void clearToken() {
    tokenLen = 0;
    token[0] = '\0';
  }

  bool isOperandToken(const char *text) const {
    if (text == nullptr || text[0] == '\0') {
      return false;
    }
    if (text[0] == '/' || isNumericToken(text)) {
      return true;
    }
    return false;
  }

  void beginString(bool hex) {
    hexString = hex;
    stringEncoding = StringEncoding::Unknown;
    prefixLen = 0;
    utfHaveLead = false;
    mappedCode = 0;
    mappedBytes = 0;
  }

  void endString() {
    if (activeFont != nullptr) {
      mappedCode = 0;
      mappedBytes = 0;
    } else if (stringEncoding == StringEncoding::Unknown) {
      for (uint8_t i = 0; i < prefixLen; i++) {
        appendLatinByte(prefix[i]);
      }
    } else if (utfHaveLead) {
      appendDecoded('?');
    }
    prefixLen = 0;
    utfHaveLead = false;
  }

  void stringByte(uint8_t byte) {
    if (!inText || artifactDepth > 0) {
      return;
    }
    if (activeFont != nullptr) {
      appendMappedFontByte(byte);
      return;
    }
    if (stringEncoding == StringEncoding::Unknown) {
      prefix[prefixLen++] = byte;
      if (prefixLen < 2) {
        return;
      }
      if (prefix[0] == 0xfe && prefix[1] == 0xff) {
        stringEncoding = StringEncoding::Utf16BE;
      } else if (prefix[0] == 0xff && prefix[1] == 0xfe) {
        stringEncoding = StringEncoding::Utf16LE;
      } else {
        stringEncoding = StringEncoding::Latin;
        appendLatinByte(prefix[0]);
        appendLatinByte(prefix[1]);
      }
      prefixLen = 0;
      return;
    }
    if (stringEncoding == StringEncoding::Latin) {
      appendLatinByte(byte);
      return;
    }
    appendUtf16Byte(byte);
  }

  void appendLatinByte(uint8_t byte) {
    const char c = winAnsiByte(byte);
    if (c != '\0') {
      appendDecoded(c);
    }
  }

  void appendUtf16Byte(uint8_t byte) {
    if (!utfHaveLead) {
      utfLead = byte;
      utfHaveLead = true;
      return;
    }
    const uint16_t code =
        stringEncoding == StringEncoding::Utf16BE
            ? static_cast<uint16_t>((utfLead << 8) | byte)
            : static_cast<uint16_t>((byte << 8) | utfLead);
    utfHaveLead = false;
    const char c = unicodeToAscii(code);
    if (c != '\0') {
      appendDecoded(c);
    }
  }

  void appendMappedFontByte(uint8_t byte) {
    const uint8_t codeBytes = activeFont->codeBytes > 1 ? 2 : 1;
    mappedCode = static_cast<uint16_t>((mappedCode << 8) | byte);
    mappedBytes++;
    if (mappedBytes < codeBytes) {
      return;
    }

    char c = '\0';
    if (lookupFontChar(activeFont, mappedCode, &c)) {
      appendDecoded(c);
    } else if (!hexString && codeBytes == 1) {
      appendLatinByte(static_cast<uint8_t>(mappedCode));
    }
    mappedCode = 0;
    mappedBytes = 0;
  }

  void appendDecoded(char c) {
    if (static_cast<size_t>(pendingLen) + 1U >= sizeof(pending)) {
      flushPending();
    }
    pending[pendingLen++] = c;
    pending[pendingLen] = '\0';
  }

  void flushPending() {
    if (!inText || pendingLen == 0 || artifactDepth > 0) {
      clearPending();
      return;
    }
    for (uint16_t i = 0; i < pendingLen; i++) {
      if (i == 0) {
        out.setTextPosition(textX, textY);
      }
      out.emit(pending[i]);
    }
    pendingLen = 0;
    pending[0] = '\0';
    emittedInBlock = true;
  }

  void clearPending() {
    pendingLen = 0;
    pending[0] = '\0';
  }

  void pushOperand(float value) {
    if (operandCount < 8) {
      operands[operandCount++] = value;
      return;
    }
    for (uint8_t i = 1; i < 8; i++) {
      operands[i - 1] = operands[i];
    }
    operands[7] = value;
  }

  void clearOperands() { operandCount = 0; }

  void updateTextPosition(const char *op) {
    if (strcmp(op, "Tm") == 0 && operandCount >= 6) {
      textX = operands[operandCount - 2];
      textY = operands[operandCount - 1];
    } else if ((strcmp(op, "Td") == 0 || strcmp(op, "TD") == 0) &&
               operandCount >= 2) {
      textX += operands[operandCount - 2];
      textY += operands[operandCount - 1];
    }
  }

  void noteMarkedContentToken() {
    if (strcmp(token, "Artifact") == 0) {
      artifactCandidate = true;
    } else if (artifactCandidate &&
               (strcmp(token, "Pagination") == 0 ||
                strcmp(token, "Header") == 0 || strcmp(token, "Footer") == 0 ||
                strcmp(token, "Top") == 0 || strcmp(token, "Bottom") == 0)) {
      artifactPagination = true;
    } else if (strcmp(token, "BDC") != 0 && strcmp(token, "BMC") != 0 &&
               strcmp(token, "EMC") != 0 && strcmp(token, "/") != 0 &&
               strcmp(token, ">") != 0 && !isOperandToken(token)) {
      if (strcmp(token, "Type") != 0 && strcmp(token, "Subtype") != 0 &&
          strcmp(token, "Attached") != 0) {
        artifactCandidate = false;
        artifactPagination = false;
      }
    }
  }

  PdfTextOutput &out;
  State state = State::Normal;
  StringEncoding stringEncoding = StringEncoding::Unknown;
  bool inText = false;
  bool emittedInBlock = false;
  bool hexHaveNibble = false;
  bool hexString = false;
  bool utfHaveLead = false;
  bool expectingName = false;
  uint8_t arrayDepth = 0;
  uint8_t hexNibble = 0;
  uint8_t utfLead = 0;
  uint8_t prefix[2] = {};
  uint8_t prefixLen = 0;
  uint8_t literalDepth = 0;
  uint8_t octalValue = 0;
  uint8_t octalDigits = 0;
  char token[16] = {};
  uint8_t tokenLen = 0;
  char lastName[12] = {};
  char pending[256] = {};
  uint16_t pendingLen = 0;
  uint16_t mappedCode = 0;
  uint8_t mappedBytes = 0;
  float operands[8] = {};
  uint8_t operandCount = 0;
  float textX = 0.0f;
  float textY = 0.0f;
  uint8_t artifactDepth = 0;
  bool artifactCandidate = false;
  bool artifactPagination = false;
  const PdfFontMap *activeFont = nullptr;
};

class PdfContentByteOutput : public PdfByteOutput {
public:
  explicit PdfContentByteOutput(PdfContentParser &parser) : parser(parser) {}

  void feedByte(uint8_t byte) override { parser.feed(byte); }
  bool completeBytes() const override { return parser.complete(); }

private:
  PdfContentParser &parser;
};

class PdfBufferByteOutput : public PdfByteOutput {
public:
  PdfBufferByteOutput(char *buffer, size_t capacity)
      : buffer(buffer), capacity(capacity) {
    if (buffer != nullptr && capacity > 0) {
      buffer[0] = '\0';
    }
  }

  void feedByte(uint8_t byte) override {
    if (buffer == nullptr || length + 1 >= capacity) {
      truncated = true;
      return;
    }
    buffer[length++] = static_cast<char>(byte);
    buffer[length] = '\0';
  }

  bool completeBytes() const override { return truncated; }
  size_t size() const { return length; }

private:
  char *buffer = nullptr;
  size_t capacity = 0;
  size_t length = 0;
  bool truncated = false;
};

struct PdfStreamSource {
  File *file = nullptr;
  uint32_t remaining = 0;
};

int pdfStreamByte(PdfStreamSource *source) {
  if (source == nullptr || source->file == nullptr || source->remaining == 0) {
    return -1;
  }
  const int byte = source->file->read();
  if (byte < 0) {
    return -1;
  }
  source->remaining--;
  return byte;
}

struct PdfHuff {
  int16_t left[576];
  int16_t right[576];
  int16_t sym[576];
  uint16_t nodes;
};

struct PdfInflate {
  PdfStreamSource *source = nullptr;
  PdfByteOutput *out = nullptr;
  uint32_t bits = 0;
  uint8_t bitCount = 0;
  uint8_t *window = nullptr;
  uint16_t winPos = 0;
  uint32_t outCount = 0;
};

void huffReset(PdfHuff *huff) {
  huff->nodes = 1;
  huff->left[0] = huff->right[0] = -1;
  huff->sym[0] = -1;
}

bool huffInsert(PdfHuff *huff, uint16_t code, uint8_t len, uint16_t sym) {
  uint16_t node = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (huff->sym[node] >= 0) {
      return false;
    }
    int16_t *child =
        ((code >> static_cast<uint8_t>(len - 1U - i)) & 1U)
            ? &huff->right[node]
            : &huff->left[node];
    if (*child < 0) {
      if (huff->nodes >= 576) {
        return false;
      }
      *child = static_cast<int16_t>(huff->nodes);
      huff->left[huff->nodes] = huff->right[huff->nodes] = -1;
      huff->sym[huff->nodes] = -1;
      huff->nodes++;
    }
    node = static_cast<uint16_t>(*child);
  }
  if (huff->sym[node] >= 0 || huff->left[node] >= 0 ||
      huff->right[node] >= 0) {
    return false;
  }
  huff->sym[node] = static_cast<int16_t>(sym);
  return true;
}

bool huffBuild(PdfHuff *huff, const uint8_t *lengths, uint16_t count) {
  uint16_t blCount[16] = {0};
  uint16_t nextCode[16] = {0};
  uint16_t code = 0;
  int32_t left = 1;
  huffReset(huff);
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
    if (len != 0 && !huffInsert(huff, nextCode[len]++, len, n)) {
      return false;
    }
  }
  return true;
}

int inflateBits(PdfInflate *inflate, uint8_t count) {
  while (inflate->bitCount < count) {
    const int c = pdfStreamByte(inflate->source);
    if (c < 0) {
      return -1;
    }
    inflate->bits |= static_cast<uint32_t>(static_cast<uint8_t>(c))
                     << inflate->bitCount;
    inflate->bitCount = static_cast<uint8_t>(inflate->bitCount + 8U);
  }
  const int out = static_cast<int>(inflate->bits & ((1UL << count) - 1UL));
  inflate->bits >>= count;
  inflate->bitCount = static_cast<uint8_t>(inflate->bitCount - count);
  return out;
}

int huffDecode(PdfInflate *inflate, const PdfHuff *huff) {
  int16_t node = 0;
  while (node >= 0 && huff->sym[node] < 0) {
    const int bit = inflateBits(inflate, 1);
    if (bit < 0) {
      return -1;
    }
    node = bit ? huff->right[node] : huff->left[node];
  }
  return node >= 0 ? huff->sym[node] : -1;
}

PdfResult inflateEmit(PdfInflate *inflate, uint8_t byte) {
  if (inflate->out->completeBytes()) {
    return PdfResult::Done;
  }
  inflate->window[inflate->winPos] = byte;
  inflate->winPos = static_cast<uint16_t>((inflate->winPos + 1U) & 32767U);
  if (inflate->outCount < 32768U) {
    inflate->outCount++;
  }
  inflate->out->feedByte(byte);
  return inflate->out->completeBytes() ? PdfResult::Done : PdfResult::Ok;
}

PdfResult inflateCodes(PdfInflate *inflate, const PdfHuff *lit,
                       const PdfHuff *dist) {
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
    const int sym = huffDecode(inflate, lit);
    if (sym < 0) {
      return PdfResult::Format;
    }
    if (sym < 256) {
      const PdfResult result =
          inflateEmit(inflate, static_cast<uint8_t>(sym));
      if (result != PdfResult::Ok) {
        return result;
      }
    } else if (sym == 256) {
      return PdfResult::Ok;
    } else if (sym <= 285) {
      const uint8_t li = static_cast<uint8_t>(sym - 257);
      int length = lenBase[li];
      if (lenExtra[li] != 0) {
        const int extra = inflateBits(inflate, lenExtra[li]);
        if (extra < 0) {
          return PdfResult::Format;
        }
        length += extra;
      }
      const int dsym = huffDecode(inflate, dist);
      if (dsym < 0 || dsym >= 30) {
        return PdfResult::Format;
      }
      int distance = distBase[dsym];
      if (distExtra[dsym] != 0) {
        const int extra = inflateBits(inflate, distExtra[dsym]);
        if (extra < 0) {
          return PdfResult::Format;
        }
        distance += extra;
      }
      if (distance <= 0 || static_cast<uint32_t>(distance) > inflate->outCount ||
          distance > 32768) {
        return PdfResult::Format;
      }
      while (length-- > 0) {
        const uint8_t b =
            inflate->window[static_cast<uint16_t>(inflate->winPos - distance) &
                            32767U];
        const PdfResult result = inflateEmit(inflate, b);
        if (result != PdfResult::Ok) {
          return result;
        }
      }
    } else {
      return PdfResult::Format;
    }
  }
}

bool fixedTrees(PdfHuff *lit, PdfHuff *dist) {
  static uint8_t ll[288];
  static uint8_t dd[32];
  for (uint16_t i = 0; i < 288; i++) {
    ll[i] = i <= 143 ? 8 : (i <= 255 ? 9 : (i <= 279 ? 7 : 8));
  }
  for (uint8_t i = 0; i < 32; i++) {
    dd[i] = 5;
  }
  return huffBuild(lit, ll, 288) && huffBuild(dist, dd, 32);
}

PdfResult dynamicTrees(PdfInflate *inflate, PdfHuff *lit, PdfHuff *dist) {
  static const uint8_t order[19] = {16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
  uint8_t clen[19] = {0};
  static uint8_t lengths[320];
  PdfHuff *codeTree = static_cast<PdfHuff *>(inkPdfMalloc(sizeof(PdfHuff)));
  if (codeTree == nullptr) {
    return PdfResult::Memory;
  }

  const int hlit = inflateBits(inflate, 5);
  const int hdist = inflateBits(inflate, 5);
  const int hclen = inflateBits(inflate, 4);
  if (hlit < 0 || hdist < 0 || hclen < 0) {
    inkPdfFree(codeTree);
    return PdfResult::Format;
  }
  const uint16_t litCount = static_cast<uint16_t>(hlit + 257);
  const uint16_t distCount = static_cast<uint16_t>(hdist + 1);
  const uint16_t total = static_cast<uint16_t>(litCount + distCount);

  for (uint8_t i = 0; i < static_cast<uint8_t>(hclen + 4); i++) {
    const int value = inflateBits(inflate, 3);
    if (value < 0) {
      inkPdfFree(codeTree);
      return PdfResult::Format;
    }
    clen[order[i]] = static_cast<uint8_t>(value);
  }
  if (!huffBuild(codeTree, clen, 19)) {
    inkPdfFree(codeTree);
    return PdfResult::Format;
  }

  for (uint16_t i = 0; i < total;) {
    const int sym = huffDecode(inflate, codeTree);
    uint8_t value = 0;
    uint16_t repeat = 0;
    if (sym < 0) {
      inkPdfFree(codeTree);
      return PdfResult::Format;
    }
    if (sym <= 15) {
      lengths[i++] = static_cast<uint8_t>(sym);
      continue;
    }
    if (sym == 16) {
      const int extra = inflateBits(inflate, 2);
      if (i == 0 || extra < 0) {
        inkPdfFree(codeTree);
        return PdfResult::Format;
      }
      value = lengths[i - 1U];
      repeat = static_cast<uint16_t>(extra + 3);
    } else if (sym == 17) {
      const int extra = inflateBits(inflate, 3);
      if (extra < 0) {
        inkPdfFree(codeTree);
        return PdfResult::Format;
      }
      repeat = static_cast<uint16_t>(extra + 3);
    } else {
      const int extra = inflateBits(inflate, 7);
      if (extra < 0) {
        inkPdfFree(codeTree);
        return PdfResult::Format;
      }
      repeat = static_cast<uint16_t>(extra + 11);
    }
    if (repeat > total - i) {
      inkPdfFree(codeTree);
      return PdfResult::Format;
    }
    while (repeat-- > 0 && i < total) {
      lengths[i++] = value;
    }
  }

  const bool ok = huffBuild(lit, lengths, litCount) &&
                  huffBuild(dist, lengths + litCount, distCount);
  inkPdfFree(codeTree);
  return ok ? PdfResult::Ok : PdfResult::Format;
}

PdfResult inflateStored(PdfInflate *inflate) {
  inflate->bits = 0;
  inflate->bitCount = 0;
  const int l0 = pdfStreamByte(inflate->source);
  const int l1 = pdfStreamByte(inflate->source);
  const int n0 = pdfStreamByte(inflate->source);
  const int n1 = pdfStreamByte(inflate->source);
  if (l0 < 0 || l1 < 0 || n0 < 0 || n1 < 0) {
    return PdfResult::Format;
  }
  const uint16_t len = static_cast<uint16_t>(l0 | (l1 << 8));
  const uint16_t nlen = static_cast<uint16_t>(n0 | (n1 << 8));
  if (static_cast<uint16_t>(~len) != nlen) {
    return PdfResult::Format;
  }
  for (uint16_t i = 0; i < len; i++) {
    const int byte = pdfStreamByte(inflate->source);
    if (byte < 0) {
      return PdfResult::Format;
    }
    const PdfResult result =
        inflateEmit(inflate, static_cast<uint8_t>(byte));
    if (result != PdfResult::Ok) {
      return result;
    }
  }
  return PdfResult::Ok;
}

PdfResult inflateZlib(PdfStreamSource *source, PdfByteOutput *out) {
  const int cmf = pdfStreamByte(source);
  const int flg = pdfStreamByte(source);
  if (cmf < 0 || flg < 0 || (cmf & 15) != 8 ||
      (((cmf << 8) + flg) % 31) != 0 || (flg & 32) != 0) {
    return PdfResult::Format;
  }

  PdfInflate inflate;
  inflate.source = source;
  inflate.out = out;
  inflate.window = static_cast<uint8_t *>(inkPdfMalloc(32768));
  PdfHuff *lit = static_cast<PdfHuff *>(inkPdfMalloc(sizeof(PdfHuff)));
  PdfHuff *dist = static_cast<PdfHuff *>(inkPdfMalloc(sizeof(PdfHuff)));
  if (inflate.window == nullptr || lit == nullptr || dist == nullptr) {
    inkPdfFree(inflate.window);
    inkPdfFree(lit);
    inkPdfFree(dist);
    return PdfResult::Memory;
  }

  for (;;) {
    const int final = inflateBits(&inflate, 1);
    const int type = inflateBits(&inflate, 2);
    PdfResult result = PdfResult::Ok;
    if (final < 0 || type < 0 || type == 3) {
      result = PdfResult::Format;
    } else if (type == 0) {
      result = inflateStored(&inflate);
    } else {
      if (type == 1) {
        if (!fixedTrees(lit, dist)) {
          result = PdfResult::Format;
        }
      } else {
        result = dynamicTrees(&inflate, lit, dist);
      }
      if (result == PdfResult::Ok) {
        result = inflateCodes(&inflate, lit, dist);
      }
    }
    if (result != PdfResult::Ok || final) {
      if (result == PdfResult::Ok) {
        inflate.bits = 0;
        inflate.bitCount = 0;
        for (uint8_t i = 0; i < 4; i++) {
          if (pdfStreamByte(source) < 0) {
            result = PdfResult::Format;
            break;
          }
        }
      }
      inkPdfFree(inflate.window);
      inkPdfFree(lit);
      inkPdfFree(dist);
      return result;
    }
  }
}

bool readAt(File &file, uint32_t offset, uint8_t *buffer, size_t length) {
  return file.seek(offset) && file.read(buffer, length) == length;
}

int readByteAt(File &file, uint32_t offset) {
  if (!file.seek(offset)) {
    return -1;
  }
  return file.read();
}

bool findPattern(File &file, uint32_t start, uint32_t size, const char *pattern,
                 uint32_t *outOffset) {
  const size_t patternLen = strlen(pattern);
  if (patternLen == 0 || start >= size) {
    return false;
  }
  uint8_t matched = 0;
  if (!file.seek(start)) {
    return false;
  }
  for (uint32_t offset = start; offset < size; offset++) {
    const int byte = file.read();
    if (byte < 0) {
      return false;
    }
    if (static_cast<char>(byte) == pattern[matched]) {
      matched++;
      if (matched == patternLen) {
        *outOffset = offset + 1U - static_cast<uint32_t>(patternLen);
        return true;
      }
    } else {
      matched = static_cast<char>(byte) == pattern[0] ? 1 : 0;
    }
  }
  return false;
}

bool isStreamKeyword(File &file, uint32_t offset, uint32_t size,
                     uint32_t *streamStart) {
  if (streamStart == nullptr || offset > size || size - offset <= 6U) {
    return false;
  }
  const uint32_t after = offset + 6U;
  const int next = readByteAt(file, after);
  if (next == '\r') {
    const int second = after + 1 < size ? readByteAt(file, after + 1) : -1;
    *streamStart = after + (second == '\n' ? 2U : 1U);
    return true;
  }
  if (next == '\n') {
    *streamStart = after + 1U;
    return true;
  }
  return false;
}

bool checkedStreamEnd(uint32_t start, uint32_t length, uint32_t size,
                      uint32_t *outEnd) {
  if (outEnd == nullptr || start > size || length > size - start) {
    return false;
  }
  *outEnd = start + length;
  return true;
}

bool containsText(const char *text, size_t length, const char *needle) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0 || length < needleLen) {
    return false;
  }
  for (size_t i = 0; i + needleLen <= length; i++) {
    if (memcmp(text + i, needle, needleLen) == 0) {
      return true;
    }
  }
  return false;
}

bool parseDirectLength(const char *dict, size_t length, uint32_t *outLength) {
  const char *best = nullptr;
  for (size_t i = 0; i + 7 <= length; i++) {
    if (memcmp(dict + i, "/Length", 7) == 0) {
      best = dict + i + 7;
    }
  }
  if (best == nullptr) {
    return false;
  }
  while (best < dict + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= dict + length || !isdigit(static_cast<unsigned char>(*best))) {
    return false;
  }
  uint32_t value = 0;
  while (best < dict + length && isdigit(static_cast<unsigned char>(*best))) {
    if (value > (UINT32_MAX - static_cast<uint32_t>(*best - '0')) / 10U) {
      return false;
    }
    value = value * 10U + static_cast<uint32_t>(*best - '0');
    best++;
  }
  const char *afterNumber = best;
  while (afterNumber < dict + length &&
         isspace(static_cast<unsigned char>(*afterNumber))) {
    afterNumber++;
  }
  if (afterNumber < dict + length &&
      (isdigit(static_cast<unsigned char>(*afterNumber)) ||
       *afterNumber == 'R')) {
    return false;
  }
  *outLength = value;
  return true;
}

bool parseLengthReference(const char *dict, size_t length, uint32_t *outObject,
                          uint16_t *outGeneration) {
  const char *best = nullptr;
  for (size_t i = 0; i + 7 <= length; i++) {
    if (memcmp(dict + i, "/Length", 7) == 0) {
      best = dict + i + 7;
    }
  }
  if (best == nullptr) {
    return false;
  }

  while (best < dict + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= dict + length || !isdigit(static_cast<unsigned char>(*best))) {
    return false;
  }
  uint32_t object = 0;
  while (best < dict + length && isdigit(static_cast<unsigned char>(*best))) {
    if (object > (UINT32_MAX - static_cast<uint32_t>(*best - '0')) / 10U) {
      return false;
    }
    object = object * 10U + static_cast<uint32_t>(*best - '0');
    best++;
  }
  while (best < dict + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= dict + length || !isdigit(static_cast<unsigned char>(*best))) {
    return false;
  }
  uint32_t generation = 0;
  while (best < dict + length && isdigit(static_cast<unsigned char>(*best))) {
    if (generation >
        (UINT32_MAX - static_cast<uint32_t>(*best - '0')) / 10U) {
      return false;
    }
    generation = generation * 10U + static_cast<uint32_t>(*best - '0');
    best++;
  }
  while (best < dict + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= dict + length || *best != 'R' || object == 0 ||
      generation > 65535U) {
    return false;
  }
  *outObject = object;
  *outGeneration = static_cast<uint16_t>(generation);
  return true;
}

bool resolveIndirectLength(File &file, uint32_t size, uint32_t object,
                           uint16_t generation, uint32_t *outLength) {
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "%lu %u obj",
           static_cast<unsigned long>(object), static_cast<unsigned>(generation));

  uint32_t objectOffset = 0;
  if (!findPattern(file, 0, size, pattern, &objectOffset)) {
    return false;
  }
  uint32_t offset = objectOffset + strlen(pattern);
  while (offset < size) {
    const int byte = readByteAt(file, offset);
    if (byte < 0) {
      return false;
    }
    if (!isspace(static_cast<unsigned char>(byte))) {
      break;
    }
    offset++;
  }

  uint32_t value = 0;
  bool digit = false;
  while (offset < size) {
    const int byte = readByteAt(file, offset);
    if (byte < 0 || !isdigit(static_cast<unsigned char>(byte))) {
      break;
    }
    if (value > (UINT32_MAX - static_cast<uint32_t>(byte - '0')) / 10U) {
      return false;
    }
    digit = true;
    value = value * 10U + static_cast<uint32_t>(byte - '0');
    offset++;
  }
  if (!digit) {
    return false;
  }
  *outLength = value;
  return true;
}

bool hasUnsupportedFilter(const char *dict, size_t length) {
  static const char *const unsupported[] = {
      "/ASCIIHexDecode", "/ASCII85Decode", "/LZWDecode", "/RunLengthDecode",
      "/DCTDecode",      "/JPXDecode",    "/CCITTFaxDecode"};
  for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
    if (containsText(dict, length, unsupported[i])) {
      return true;
    }
  }
  return false;
}

bool sameBoundarySignature(const PdfStreamInfo &left, bool firstLeft,
                           const PdfStreamInfo &right, bool firstRight) {
  const uint32_t leftHash = firstLeft ? left.firstHash : left.lastHash;
  const uint32_t rightHash = firstRight ? right.firstHash : right.lastHash;
  const uint8_t leftLen = firstLeft ? left.firstLen : left.lastLen;
  const uint8_t rightLen = firstRight ? right.firstLen : right.lastLen;
  const bool leftPos = firstLeft ? left.firstHasPosition : left.lastHasPosition;
  const bool rightPos =
      firstRight ? right.firstHasPosition : right.lastHasPosition;
  const float leftX = firstLeft ? left.firstX : left.lastX;
  const float leftY = firstLeft ? left.firstY : left.lastY;
  const float rightX = firstRight ? right.firstX : right.lastX;
  const float rightY = firstRight ? right.firstY : right.lastY;

  return leftLen > 0 && leftLen < 128 && leftLen == rightLen &&
         leftHash == rightHash && leftPos && rightPos &&
         inHeaderFooterBand(leftY) && sameCoord(leftX, rightX) &&
         sameCoord(leftY, rightY);
}

void detectRepeatedBoundaries() {
  for (uint16_t i = 1; i < pdfCache.streamCount; i++) {
    PdfStreamInfo &stream = pdfCache.streams[i];
    if (i + 2 < pdfCache.streamCount &&
        sameBoundarySignature(stream, true, pdfCache.streams[i + 1], true) &&
        sameBoundarySignature(stream, true, pdfCache.streams[i + 2], true)) {
      stream.suppressFirst = true;
    } else if (i + 4 < pdfCache.streamCount &&
               sameBoundarySignature(stream, true, pdfCache.streams[i + 2],
                                     true) &&
               sameBoundarySignature(stream, true, pdfCache.streams[i + 4],
                                     true)) {
      stream.suppressFirst = true;
    }

    if (i + 2 < pdfCache.streamCount &&
        sameBoundarySignature(stream, false, pdfCache.streams[i + 1], false) &&
        sameBoundarySignature(stream, false, pdfCache.streams[i + 2], false)) {
      stream.suppressLast = true;
    } else if (i + 4 < pdfCache.streamCount &&
               sameBoundarySignature(stream, false, pdfCache.streams[i + 2],
                                     false) &&
               sameBoundarySignature(stream, false, pdfCache.streams[i + 4],
                                     false)) {
      stream.suppressLast = true;
    }
  }
}

bool readStreamDict(File &file, uint32_t streamOffset, char *dict,
                    size_t dictSize, size_t *outLength) {
  if (dict == nullptr || outLength == nullptr || dictSize == 0 ||
      streamOffset == 0) {
    return false;
  }
  const uint32_t start =
      streamOffset > kDictLookback ? streamOffset - kDictLookback : 0;
  const uint32_t length = streamOffset - start;
  const size_t copyLength =
      length < dictSize - 1 ? static_cast<size_t>(length) : dictSize - 1;
  if (!readAt(file, start + length - copyLength,
              reinterpret_cast<uint8_t *>(dict), copyLength)) {
    return false;
  }
  dict[copyLength] = '\0';

  char *objectStart = nullptr;
  for (size_t i = 0; i + 3 <= copyLength; i++) {
    if (memcmp(dict + i, "obj", 3) == 0) {
      objectStart = dict + i + 3;
    }
  }
  if (objectStart != nullptr) {
    char *dictStart = nullptr;
    for (char *scan = objectStart; scan + 1 < dict + copyLength; scan++) {
      if (scan[0] == '<' && scan[1] == '<') {
        dictStart = scan;
      }
    }
    if (dictStart != nullptr) {
      const size_t trimmed = strlen(dictStart);
      memmove(dict, dictStart, trimmed + 1);
      *outLength = trimmed;
      return true;
    }
  }

  *outLength = copyLength;
  return true;
}

PdfResult parseRawStream(File &file, const PdfStreamInfo &stream,
                         PdfTextOutput &out) {
  if (!file.seek(stream.start)) {
    return PdfResult::Io;
  }
  PdfTextNormalizer normalizer(out);
  PdfTextOutput &textOut = inkPdfHooks.normalizeText
                               ? static_cast<PdfTextOutput &>(normalizer)
                               : out;
  PdfContentParser parser(textOut);
  for (uint32_t offset = stream.start; offset < stream.end && !textOut.complete();
       offset++) {
    const int byte = file.read();
    if (byte < 0) {
      return PdfResult::Io;
    }
    parser.feed(static_cast<uint8_t>(byte));
  }
  parser.finish();
  if (inkPdfHooks.normalizeText) {
    normalizer.finish();
  }
  return PdfResult::Ok;
}

PdfResult parseFlateStream(File &file, const PdfStreamInfo &stream,
                           PdfTextOutput &out) {
  if (stream.end <= stream.start || !file.seek(stream.start)) {
    return PdfResult::Io;
  }
  PdfTextNormalizer normalizer(out);
  PdfTextOutput &textOut = inkPdfHooks.normalizeText
                               ? static_cast<PdfTextOutput &>(normalizer)
                               : out;
  PdfContentParser parser(textOut);
  PdfContentByteOutput byteOut(parser);
  PdfStreamSource source;
  source.file = &file;
  source.remaining = stream.end - stream.start;
  const PdfResult result = inflateZlib(&source, &byteOut);
  parser.finish();
  if (inkPdfHooks.normalizeText) {
    normalizer.finish();
  }
  return result == PdfResult::Done ? PdfResult::Ok : result;
}

PdfResult parseStream(File &file, const PdfStreamInfo &stream,
                      PdfTextOutput &out) {
  return stream.flate ? parseFlateStream(file, stream, out)
                      : parseRawStream(file, stream, out);
}

bool findObjectOffset(File &file, uint32_t size, uint32_t object,
                      uint16_t generation, uint32_t *outOffset) {
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "%lu %u obj",
           static_cast<unsigned long>(object), static_cast<unsigned>(generation));
  return findPattern(file, 0, size, pattern, outOffset);
}

bool parseObjectRefAfterKey(const char *text, size_t length, const char *key,
                            uint32_t *outObject, uint16_t *outGeneration) {
  const size_t keyLen = strlen(key);
  const char *best = nullptr;
  for (size_t i = 0; i + keyLen <= length; i++) {
    if (memcmp(text + i, key, keyLen) == 0) {
      best = text + i + keyLen;
    }
  }
  if (best == nullptr) {
    return false;
  }
  while (best < text + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= text + length || !isdigit(static_cast<unsigned char>(*best))) {
    return false;
  }
  uint32_t object = 0;
  while (best < text + length && isdigit(static_cast<unsigned char>(*best))) {
    if (object > (UINT32_MAX - static_cast<uint32_t>(*best - '0')) / 10U) {
      return false;
    }
    object = object * 10U + static_cast<uint32_t>(*best - '0');
    best++;
  }
  while (best < text + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= text + length || !isdigit(static_cast<unsigned char>(*best))) {
    return false;
  }
  uint32_t generation = 0;
  while (best < text + length && isdigit(static_cast<unsigned char>(*best))) {
    if (generation >
        (UINT32_MAX - static_cast<uint32_t>(*best - '0')) / 10U) {
      return false;
    }
    generation = generation * 10U + static_cast<uint32_t>(*best - '0');
    best++;
  }
  while (best < text + length && isspace(static_cast<unsigned char>(*best))) {
    best++;
  }
  if (best >= text + length || *best != 'R' || object == 0 ||
      generation > 65535U) {
    return false;
  }
  *outObject = object;
  *outGeneration = static_cast<uint16_t>(generation);
  return true;
}

bool addCMapEntry(PdfFontMap &map, uint16_t code, uint8_t codeBytes,
                  uint16_t unicode) {
  const char c = unicodeToAscii(unicode);
  if (c == '\0') {
    return true;
  }
  if (map.entryStart > pdfCache.fontEntryCount ||
      map.entryCount > pdfCache.fontEntryCount - map.entryStart) {
    return false;
  }
  for (uint16_t i = 0; i < map.entryCount; i++) {
    PdfCMapEntry &entry = pdfCache.fontEntries[map.entryStart + i];
    if (entry.code == code) {
      entry.text = c;
      return true;
    }
  }
  if (pdfCache.fontEntryCount >= kMaxPdfCMapEntries) {
    return false;
  }
  if (codeBytes > map.codeBytes) {
    map.codeBytes = codeBytes;
  }
  if (map.entryCount == 0) {
    map.entryStart = pdfCache.fontEntryCount;
  }
  PdfCMapEntry &entry = pdfCache.fontEntries[pdfCache.fontEntryCount++];
  entry.code = code;
  entry.text = c;
  map.entryCount++;
  return true;
}

bool parseHexToken(const char *token, uint16_t *value, uint8_t *byteCount) {
  if (token == nullptr || token[0] != '<' || token[1] == '<') {
    return false;
  }
  uint32_t out = 0;
  uint8_t digits = 0;
  for (const char *p = token + 1; *p != '\0' && *p != '>'; p++) {
    int digit = -1;
    if (*p >= '0' && *p <= '9') {
      digit = *p - '0';
    } else if (*p >= 'A' && *p <= 'F') {
      digit = *p - 'A' + 10;
    } else if (*p >= 'a' && *p <= 'f') {
      digit = *p - 'a' + 10;
    }
    if (digit < 0 || digits >= 4) {
      return false;
    }
    out = (out << 4) | static_cast<uint32_t>(digit);
    digits++;
  }
  if (digits == 0 || digits > 4) {
    return false;
  }
  if ((digits & 1U) != 0) {
    out <<= 4;
    digits++;
  }
  *value = static_cast<uint16_t>(out);
  *byteCount = digits > 2 ? 2 : 1;
  return true;
}

bool nextCMapToken(const char *data, size_t length, size_t *pos, char *token,
                   size_t tokenSize) {
  while (*pos < length) {
    const char c = data[*pos];
    if (c == '%') {
      while (*pos < length && data[*pos] != '\n' && data[*pos] != '\r') {
        (*pos)++;
      }
      continue;
    }
    if (!isspace(static_cast<unsigned char>(c))) {
      break;
    }
    (*pos)++;
  }
  if (*pos >= length || tokenSize < 2) {
    return false;
  }

  size_t out = 0;
  const char first = data[(*pos)++];
  token[out++] = first;
  if (first == '[' || first == ']') {
    token[out] = '\0';
    return true;
  }
  if (first == '<') {
    while (*pos < length && out + 1 < tokenSize) {
      const char c = data[(*pos)++];
      token[out++] = c;
      if (c == '>') {
        break;
      }
    }
    token[out] = '\0';
    return true;
  }
  while (*pos < length && out + 1 < tokenSize) {
    const char c = data[*pos];
    if (isspace(static_cast<unsigned char>(c)) || c == '[' || c == ']') {
      break;
    }
    token[out++] = c;
    (*pos)++;
  }
  token[out] = '\0';
  return true;
}

void parseCMapBuffer(const char *data, size_t length, PdfFontMap &map) {
  enum class Mode : uint8_t { None, BfChar, BfRange };
  Mode mode = Mode::None;
  uint8_t rangeState = 0;
  uint16_t rangeStart = 0;
  uint16_t rangeEnd = 0;
  uint16_t arrayCode = 0;
  uint8_t rangeCodeBytes = 1;
  bool rangeArray = false;
  uint16_t pendingCode = 0;
  uint8_t pendingBytes = 1;
  bool havePending = false;

  size_t pos = 0;
  char token[32];
  while (nextCMapToken(data, length, &pos, token, sizeof(token))) {
    if (strcmp(token, "beginbfchar") == 0) {
      mode = Mode::BfChar;
      havePending = false;
      continue;
    }
    if (strcmp(token, "endbfchar") == 0) {
      mode = Mode::None;
      havePending = false;
      continue;
    }
    if (strcmp(token, "beginbfrange") == 0) {
      mode = Mode::BfRange;
      rangeState = 0;
      rangeArray = false;
      continue;
    }
    if (strcmp(token, "endbfrange") == 0) {
      mode = Mode::None;
      rangeState = 0;
      rangeArray = false;
      continue;
    }

    uint16_t value = 0;
    uint8_t bytes = 1;
    const bool hex = parseHexToken(token, &value, &bytes);
    if (mode == Mode::BfChar && hex) {
      if (!havePending) {
        pendingCode = value;
        pendingBytes = bytes;
        havePending = true;
      } else {
        addCMapEntry(map, pendingCode, pendingBytes, value);
        havePending = false;
      }
      continue;
    }

    if (mode != Mode::BfRange) {
      continue;
    }
    if (rangeArray) {
      if (strcmp(token, "]") == 0) {
        rangeArray = false;
        rangeState = 0;
      } else if (hex && arrayCode <= rangeEnd) {
        if (!addCMapEntry(map, arrayCode++, rangeCodeBytes, value)) {
          rangeArray = false;
          rangeState = 0;
        }
      }
      continue;
    }
    if (!hex && strcmp(token, "[") == 0 && rangeState == 2) {
      rangeArray = true;
      arrayCode = rangeStart;
      continue;
    }
    if (!hex) {
      continue;
    }
    if (rangeState == 0) {
      rangeStart = value;
      rangeCodeBytes = bytes;
      rangeState = 1;
    } else if (rangeState == 1) {
      rangeEnd = value;
      rangeState = 2;
    } else {
      for (uint16_t code = rangeStart; code <= rangeEnd; code++) {
        if (!addCMapEntry(map, code, rangeCodeBytes,
                          static_cast<uint16_t>(value + code - rangeStart))) {
          break;
        }
        if (code == 0xffff) {
          break;
        }
      }
      rangeState = 0;
    }
  }
}

bool loadCMapForFont(File &file, uint32_t size, PdfFontMap &map) {
  uint32_t fontOffset = 0;
  if (!findObjectOffset(file, size, map.fontObject, map.fontGeneration,
                        &fontOffset)) {
    return false;
  }
  static const size_t kFontObjectBytes = 4096;
  char *objectText = static_cast<char *>(inkPdfMalloc(kFontObjectBytes));
  if (objectText == nullptr) {
    return false;
  }
  const size_t objectLength =
      size - fontOffset < kFontObjectBytes - 1
          ? static_cast<size_t>(size - fontOffset)
          : kFontObjectBytes - 1;
  if (!readAt(file, fontOffset, reinterpret_cast<uint8_t *>(objectText),
              objectLength)) {
    inkPdfFree(objectText);
    return false;
  }
  objectText[objectLength] = '\0';

  uint32_t cmapObject = 0;
  uint16_t cmapGeneration = 0;
  if (!parseObjectRefAfterKey(objectText, objectLength, "/ToUnicode",
                              &cmapObject, &cmapGeneration)) {
    inkPdfFree(objectText);
    return false;
  }
  inkPdfFree(objectText);

  uint32_t cmapOffset = 0;
  if (!findObjectOffset(file, size, cmapObject, cmapGeneration, &cmapOffset)) {
    return false;
  }
  uint32_t streamOffset = 0;
  if (!findPattern(file, cmapOffset, size, "stream", &streamOffset)) {
    return false;
  }
  uint32_t streamStart = 0;
  if (!isStreamKeyword(file, streamOffset, size, &streamStart)) {
    return false;
  }
  char *dict = static_cast<char *>(inkPdfMalloc(kDictLookback + 1U));
  if (dict == nullptr) {
    return false;
  }
  size_t dictLength = 0;
  if (!readStreamDict(file, streamOffset, dict, kDictLookback + 1U,
                      &dictLength)) {
    inkPdfFree(dict);
    return false;
  }

  const bool flate = containsText(dict, dictLength, "/FlateDecode");
  uint32_t streamLength = 0;
  uint32_t streamEnd = 0;
  uint32_t lengthObject = 0;
  uint16_t lengthGeneration = 0;
  if (parseDirectLength(dict, dictLength, &streamLength) &&
      checkedStreamEnd(streamStart, streamLength, size, &streamEnd)) {
  } else if (parseLengthReference(dict, dictLength, &lengthObject,
                                  &lengthGeneration) &&
             resolveIndirectLength(file, size, lengthObject, lengthGeneration,
                                   &streamLength) &&
             checkedStreamEnd(streamStart, streamLength, size, &streamEnd)) {
  } else if (!findPattern(file, streamStart, size, "endstream", &streamEnd)) {
    inkPdfFree(dict);
    return false;
  }
  inkPdfFree(dict);

  char *buffer = static_cast<char *>(inkPdfMalloc(kMaxPdfCMapBytes));
  if (buffer == nullptr) {
    return false;
  }
  PdfBufferByteOutput out(buffer, kMaxPdfCMapBytes);
  PdfResult result = PdfResult::Ok;
  if (flate) {
    if (!file.seek(streamStart)) {
      result = PdfResult::Io;
    } else {
      PdfStreamSource source;
      source.file = &file;
      source.remaining = streamEnd - streamStart;
      result = inflateZlib(&source, &out);
    }
  } else {
    if (!file.seek(streamStart)) {
      result = PdfResult::Io;
    } else {
      for (uint32_t offset = streamStart; offset < streamEnd &&
           !out.completeBytes(); offset++) {
        const int byte = file.read();
        if (byte < 0) {
          result = PdfResult::Io;
          break;
        }
        out.feedByte(static_cast<uint8_t>(byte));
      }
    }
  }
  if (result == PdfResult::Ok || result == PdfResult::Done) {
    parseCMapBuffer(buffer, out.size(), map);
  }
  inkPdfFree(buffer);
  return map.entryCount > 0;
}

void addFontResource(const char *resource, uint32_t object,
                     uint16_t generation) {
  if (resource == nullptr || resource[0] == '\0' || object == 0) {
    return;
  }
  for (uint8_t i = 0; i < pdfCache.fontMapCount; i++) {
    PdfFontMap &map = pdfCache.fontMaps[i];
    if (strcmp(map.resource, resource) == 0 && map.fontObject == object &&
        map.fontGeneration == generation) {
      return;
    }
  }
  if (pdfCache.fontMapCount >= kMaxPdfFontMaps) {
    return;
  }
  PdfFontMap &map = pdfCache.fontMaps[pdfCache.fontMapCount++];
  copyText(map.resource, sizeof(map.resource), resource);
  map.fontObject = object;
  map.fontGeneration = generation;
}

bool isPdf(File &file) {
  uint8_t header[8] = {};
  if (!readAt(file, 0, header, sizeof(header))) {
    return false;
  }
  return memcmp(header, "%PDF-", 5) == 0;
}

bool cacheMatches(const char *providerId, const char *path, uint32_t size) {
  return (pdfCache.valid || pdfCache.indexing) && pdfCache.size == size &&
         strcmp(pdfCache.providerId, providerId != nullptr ? providerId : "") == 0 &&
         strcmp(pdfCache.path, path != nullptr ? path : "") == 0;
}

void beginPdfIndex(const char *providerId, const char *path, uint32_t size) {
  resetPdfCache();
  pdfCache.indexing = true;
  pdfCache.size = size;
  pdfCache.startedAt = inkPdfMicros();
  copyText(pdfCache.providerId, sizeof(pdfCache.providerId), providerId);
  copyText(pdfCache.path, sizeof(pdfCache.path), path);
}

bool cacheReadyForPath(const char *providerId, const char *path) {
  return pdfCache.valid &&
         strcmp(pdfCache.providerId, providerId != nullptr ? providerId : "") == 0 &&
         strcmp(pdfCache.path, path != nullptr ? path : "") == 0;
}

uint8_t progressForPath(const char *providerId, const char *path) {
  if ((!pdfCache.indexing && !pdfCache.valid) || pdfCache.size == 0 ||
      strcmp(pdfCache.providerId, providerId != nullptr ? providerId : "") != 0 ||
      strcmp(pdfCache.path, path != nullptr ? path : "") != 0) {
    return 0;
  }
  if (pdfCache.valid) {
    return 100;
  }
  const uint32_t percent = (pdfCache.searchFrom * 100ULL) / pdfCache.size;
  return percent > 99U ? 99U : static_cast<uint8_t>(percent);
}

PdfResult buildPdfIndex(const char *providerId, const char *path,
                        uint32_t budgetUs) {
  File file = inkPdfOpenFile(providerId, path);
  if (!file || file.isDirectory()) {
    return PdfResult::Io;
  }
  if (!isPdf(file)) {
    return PdfResult::Format;
  }

  const uint32_t size = file.size();
  if (pdfCache.valid && cacheMatches(providerId, path, size)) {
    return PdfResult::Ok;
  }

  if (!pdfCache.indexing || !cacheMatches(providerId, path, size)) {
    beginPdfIndex(providerId, path, size);
  }

  if (budgetUs == 0) {
    return PdfResult::Done;
  }

  const uint32_t budgetStartedAt = inkPdfMicros();
  while (pdfCache.searchFrom < size) {
    uint32_t streamOffset = 0;
    if (!findPattern(file, pdfCache.searchFrom, size, "stream", &streamOffset)) {
      break;
    }

    uint32_t streamStart = 0;
    if (!isStreamKeyword(file, streamOffset, size, &streamStart)) {
      pdfCache.searchFrom = streamOffset < size ? streamOffset + 1U : size;
      continue;
    }

    char *dict = static_cast<char *>(inkPdfMalloc(kDictLookback + 1U));
    if (dict == nullptr) {
      return PdfResult::Memory;
    }
    size_t dictLength = 0;
    if (!readStreamDict(file, streamOffset, dict, kDictLookback + 1U,
                        &dictLength)) {
      inkPdfFree(dict);
      return PdfResult::Io;
    }

    uint32_t streamLength = 0;
    uint32_t streamEnd = 0;
    uint32_t lengthObject = 0;
    uint16_t lengthGeneration = 0;
    if (parseDirectLength(dict, dictLength, &streamLength) &&
        checkedStreamEnd(streamStart, streamLength, size, &streamEnd)) {
    } else if (parseLengthReference(dict, dictLength, &lengthObject,
                                    &lengthGeneration) &&
               resolveIndirectLength(file, size, lengthObject, lengthGeneration,
                                     &streamLength) &&
               checkedStreamEnd(streamStart, streamLength, size, &streamEnd)) {
    } else if (!findPattern(file, streamStart, size, "endstream", &streamEnd)) {
      inkPdfFree(dict);
      return PdfResult::Format;
    }

    const bool hasFilter = containsText(dict, dictLength, "/Filter");
    const bool flate = containsText(dict, dictLength, "/FlateDecode");
    const bool image = containsText(dict, dictLength, "/Subtype") &&
                       containsText(dict, dictLength, "/Image");
    const bool supportedFilter =
        !hasFilter || (flate && !hasUnsupportedFilter(dict, dictLength));

    const bool likelyContent =
        containsText(dict, dictLength, "/Length") &&
        !containsText(dict, dictLength, "/FontFile") &&
        !containsText(dict, dictLength, "/Length1") &&
        !containsText(dict, dictLength, "/Length2") &&
        !containsText(dict, dictLength, "/Length3") &&
        !containsText(dict, dictLength, "/Metadata") &&
        !containsText(dict, dictLength, "/ObjStm") &&
        !containsText(dict, dictLength, "/XRef") &&
        !containsText(dict, dictLength, "/CIDToGIDMap") &&
        !(containsText(dict, dictLength, "/Width") &&
          containsText(dict, dictLength, "/Height") &&
          containsText(dict, dictLength, "/BitsPerComponent"));
    inkPdfFree(dict);

    if (!image && supportedFilter && likelyContent) {
      pdfCache.candidates++;
      PdfStreamInfo info;
      info.start = streamStart;
      info.end = streamEnd;
      info.flate = flate;

      PdfLineCountSink counter;
      const uint32_t decodeStartedAt = inkPdfMicros();
      const PdfResult countResult = parseStream(file, info, counter);
      const uint32_t decodeUs = static_cast<uint32_t>(inkPdfMicros() - decodeStartedAt);
      pdfCache.totalDecodeUs += decodeUs;
      counter.finish();
      if (countResult == PdfResult::Memory) {
        return countResult;
      }
      if (countResult == PdfResult::Ok && counter.anyText() &&
          counter.count() > 0) {
        pdfCache.decoded++;
        counter.applyTo(info);
        info.screenStart = pdfCache.screenCount;
        const uint32_t screens =
            (static_cast<uint32_t>(info.lineCount) + kTextRows - 1U) /
            kTextRows;
        if (pdfCache.streamCount < kMaxPdfStreams) {
          pdfCache.streams[pdfCache.streamCount++] = info;
          pdfCache.screenCount += screens > 0 ? screens : 1U;
        } else {
          pdfCache.truncated = true;
        }
      } else if (!hasFilter) {
        PdfLineCountSink rawCounter;
        info.flate = false;
        const PdfResult rawResult = parseRawStream(file, info, rawCounter);
        rawCounter.finish();
        if (rawResult == PdfResult::Ok && rawCounter.anyText() &&
            rawCounter.count() > 0) {
          rawCounter.applyTo(info);
          info.screenStart = pdfCache.screenCount;
          const uint32_t screens =
              (static_cast<uint32_t>(info.lineCount) + kTextRows - 1U) /
              kTextRows;
          if (pdfCache.streamCount < kMaxPdfStreams) {
            pdfCache.streams[pdfCache.streamCount++] = info;
            pdfCache.screenCount += screens > 0 ? screens : 1U;
          } else {
            pdfCache.truncated = true;
          }
        }
      } else {
        // Ignore malformed candidate streams; other streams may still carry text.
        if (countResult == PdfResult::Ok) {
          pdfCache.noText++;
        } else {
          pdfCache.decodeFailed++;
        }
      }
    }

    pdfCache.searchFrom = streamEnd;
    uint32_t endstream = 0;
    if (findPattern(file, pdfCache.searchFrom, size, "endstream", &endstream)) {
      pdfCache.searchFrom =
          endstream <= UINT32_MAX - 9U ? endstream + 9U : size;
    } else {
      pdfCache.searchFrom =
          pdfCache.searchFrom < size ? pdfCache.searchFrom + 1U : size;
    }

    if (static_cast<uint32_t>(inkPdfMicros() - budgetStartedAt) > budgetUs) {
      return PdfResult::Done;
    }
  }

  detectRepeatedBoundaries();
  pdfCache.valid = true;
  pdfCache.indexing = false;
  return PdfResult::Ok;
}

const PdfStreamInfo *streamForScreen(uint32_t screen, uint16_t *outIndex,
                                     uint16_t *outSkipLines) {
  if (!pdfCache.valid || pdfCache.streamCount == 0) {
    return nullptr;
  }
  if (screen >= pdfCache.screenCount) {
    screen = pdfCache.screenCount - 1;
  }
  for (uint16_t i = 0; i < pdfCache.streamCount; i++) {
    const PdfStreamInfo &stream = pdfCache.streams[i];
    const uint32_t screens =
        (static_cast<uint32_t>(stream.lineCount) + kTextRows - 1U) / kTextRows;
    if (screen >= stream.screenStart && screen < stream.screenStart + screens) {
      if (outIndex != nullptr) {
        *outIndex = i;
      }
      if (outSkipLines != nullptr) {
        *outSkipLines =
            static_cast<uint16_t>((screen - stream.screenStart) * kTextRows);
      }
      return &stream;
    }
  }
  return nullptr;
}

PdfResult loadStreamText(File &file, uint16_t streamIndex, PdfPageText &text) {
  if (!pdfCache.valid || streamIndex >= pdfCache.streamCount) {
    return PdfResult::Format;
  }
  const PdfResult result = parseStream(file, pdfCache.streams[streamIndex], text);
  text.finish();
  return result;
}

} // namespace

InkPdfResult inkPdfOpen(const char *providerId, const char *path,
                        uint32_t *outSize) {
  File file = inkPdfOpenFile(providerId, path);
  if (!file || file.isDirectory()) {
    return InkPdfResult::Io;
  }
  if (!isPdf(file)) {
    return InkPdfResult::Format;
  }

  const uint32_t size = file.size();
  if (outSize != nullptr) {
    *outSize = size;
  }
  if (!pdfCache.valid || !cacheMatches(providerId, path, size)) {
    beginPdfIndex(providerId, path, size);
  }
  return InkPdfResult::Ok;
}

InkPdfResult inkPdfContinueIndex(const char *providerId, const char *path,
                                 uint32_t budgetUs) {
  return buildPdfIndex(providerId, path, budgetUs);
}

bool inkPdfReady(const char *providerId, const char *path) {
  return cacheReadyForPath(providerId, path);
}

bool inkPdfLoading(const char *providerId, const char *path) {
  return pdfCache.indexing &&
         strcmp(pdfCache.providerId, providerId != nullptr ? providerId : "") == 0 &&
         strcmp(pdfCache.path, path != nullptr ? path : "") == 0;
}

uint8_t inkPdfProgress(const char *providerId, const char *path) {
  return progressForPath(providerId, path);
}

uint32_t inkPdfScreenCount(const char *providerId, const char *path) {
  if (!cacheReadyForPath(providerId, path)) {
    return 0;
  }
  return pdfCache.screenCount;
}

InkPdfResult inkPdfExtractScreenText(const char *providerId, const char *path,
                                     uint32_t screen,
                                     InkPdfTextLineHandler handler,
                                     void *context,
                                     InkPdfScreenInfo *outInfo) {
  if (!cacheReadyForPath(providerId, path)) {
    return InkPdfResult::Done;
  }

  File file = inkPdfOpenFile(providerId, path);
  if (!file || file.isDirectory()) {
    return InkPdfResult::Io;
  }

  uint16_t streamIndex = 0;
  uint16_t skipLines = 0;
  const PdfStreamInfo *stream = streamForScreen(screen, &streamIndex, &skipLines);
  if (stream == nullptr) {
    return InkPdfResult::Format;
  }

  PdfPageText *current =
      static_cast<PdfPageText *>(inkPdfMalloc(sizeof(PdfPageText)));
  if (current == nullptr) {
    return InkPdfResult::Memory;
  }
  new (current) PdfPageText(skipLines, kTextRows);

  const uint32_t decodeStartedAt = inkPdfMicros();
  const PdfResult result = loadStreamText(file, streamIndex, *current);
  const uint32_t decodeUs = static_cast<uint32_t>(inkPdfMicros() - decodeStartedAt);
  if (result != PdfResult::Ok) {
    current->~PdfPageText();
    inkPdfFree(current);
    return result;
  }

  const bool firstScreenInStream = skipLines == 0;
  const bool lastScreenInStream = skipLines + kTextRows >= stream->lineCount;
  if (outInfo != nullptr) {
    outInfo->streamIndex = streamIndex;
    outInfo->skipLines = skipLines;
    outInfo->lineCount = current->count();
    outInfo->firstScreenInStream = firstScreenInStream;
    outInfo->lastScreenInStream = lastScreenInStream;
    outInfo->hasNextStream = streamIndex + 1 < pdfCache.streamCount;
    outInfo->decodeUs = decodeUs;
  }

  if (handler != nullptr) {
    for (uint8_t i = 0; i < current->count(); i++) {
      const bool first = firstScreenInStream && i == 0;
      const bool last = lastScreenInStream && i + 1 == current->count();
      InkPdfTextLine line;
      line.text = current->line(i);
      line.firstInStream = first;
      line.lastInStream = last;
      line.suppress = (first && stream->suppressFirst) ||
                      (last && stream->suppressLast);
      handler(line, context);
    }
  }

  current->~PdfPageText();
  inkPdfFree(current);
  return InkPdfResult::Ok;
}
#endif
#endif
