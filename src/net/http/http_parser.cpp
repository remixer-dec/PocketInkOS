#include "net/http/http_parser.h"

#include "sys/psram_allocator.h"

#include <cstring>

namespace net::http {
namespace {

bool equalText(const char *text, size_t length, const char *expected) {
  return strlen(expected) == length && memcmp(text, expected, length) == 0;
}

bool tokenCharacter(unsigned char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return true;
  }
  switch (c) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '\'':
  case '*':
  case '+':
  case '-':
  case '.':
  case '^':
  case '_':
  case '`':
  case '|':
  case '~':
    return true;
  default:
    return false;
  }
}

bool validTargetByte(unsigned char c) {
  return c >= 0x21U && c != 0x7fU;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool normalizedAbsolutePath(const char *path) {
  if (path == nullptr || path[0] != '/') {
    return false;
  }
  if (path[1] == '\0') {
    return true;
  }
  const char *component = path + 1;
  for (const char *cursor = component;; cursor++) {
    const char c = *cursor;
    if (c == '\\' ||
        (c != '\0' && (static_cast<unsigned char>(c) < 0x20U ||
                       static_cast<unsigned char>(c) == 0x7fU))) {
      return false;
    }
    if (c == '/' || c == '\0') {
      const size_t length = static_cast<size_t>(cursor - component);
      if (length == 0 || (length == 1 && component[0] == '.') ||
          (length == 2 && component[0] == '.' && component[1] == '.')) {
        return false;
      }
      if (c == '\0') {
        return true;
      }
      component = cursor + 1;
    }
  }
}

} // namespace

const char *methodName(Method method) {
  switch (method) {
  case Method::Get:
    return "GET";
  case Method::Post:
    return "POST";
  case Method::Put:
    return "PUT";
  case Method::Delete:
    return "DELETE";
  case Method::Options:
    return "OPTIONS";
  case Method::Head:
    return "HEAD";
  case Method::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

Method parseMethod(const char *text, size_t length) {
  if (equalText(text, length, "GET")) {
    return Method::Get;
  }
  if (equalText(text, length, "POST")) {
    return Method::Post;
  }
  if (equalText(text, length, "PUT")) {
    return Method::Put;
  }
  if (equalText(text, length, "DELETE")) {
    return Method::Delete;
  }
  if (equalText(text, length, "OPTIONS")) {
    return Method::Options;
  }
  if (equalText(text, length, "HEAD")) {
    return Method::Head;
  }
  return Method::Unknown;
}

const char *Request::header(const char *lowerCaseName) const {
  if (lowerCaseName == nullptr || lowerCaseName[0] == '\0') {
    return nullptr;
  }
  const char *found = nullptr;
  for (uint8_t i = 0; i < headerCount; i++) {
    if (strcmp(headers[i].name, lowerCaseName) != 0) {
      continue;
    }
    if (found != nullptr) {
      return nullptr;
    }
    found = headers[i].value;
  }
  return found;
}

RequestParser::RequestParser(ParserLimits limits) : limits(limits) {
  if (this->limits.headers > Request::kHeaderCapacity) {
    this->limits.headers = Request::kHeaderCapacity;
  }
}

RequestParser::~RequestParser() {
  if (bodyBuffer != nullptr && expectedBody > 0) {
    memset(bodyBuffer, 0, expectedBody);
  }
  psram::release(bodyBuffer);
  if (headerBuffer != nullptr && headerSize > 0) {
    memset(headerBuffer, 0, headerSize);
  }
  psram::release(headerBuffer);
}

bool RequestParser::ensureHeaderBuffer() {
  if (headerBuffer != nullptr) {
    return true;
  }
  if (limits.headerBytes < 32 || limits.headerBytes == SIZE_MAX) {
    return false;
  }
  headerBuffer =
      static_cast<char *>(psram::allocate(limits.headerBytes + 1U));
  return headerBuffer != nullptr;
}

bool RequestParser::fail(ParseError error) {
  failure = error;
  currentState = ParseState::Failed;
  return false;
}

void RequestParser::reset() {
  if (bodyBuffer != nullptr && expectedBody > 0) {
    memset(bodyBuffer, 0, expectedBody);
  }
  psram::release(bodyBuffer);
  bodyBuffer = nullptr;
  if (headerBuffer != nullptr && headerSize > 0) {
    memset(headerBuffer, 0, headerSize);
  }
  parsed = {};
  headerSize = 0;
  expectedBody = 0;
  bodyReceived = 0;
  failure = ParseError::None;
  currentState = ParseState::Incomplete;
  headersComplete = false;
}

ParseResult RequestParser::feed(const uint8_t *data, size_t size) {
  ParseResult result{currentState, failure, 0};
  if (currentState != ParseState::Incomplete ||
      (size > 0 && data == nullptr)) {
    if (size > 0 && data == nullptr && currentState == ParseState::Incomplete) {
      fail(ParseError::InvalidHeader);
      result.state = currentState;
      result.error = failure;
    }
    return result;
  }
  if (!ensureHeaderBuffer()) {
    fail(ParseError::NoMemory);
    return {currentState, failure, 0};
  }

  while (!headersComplete && result.consumed < size) {
    if (headerSize >= limits.headerBytes) {
      fail(ParseError::HeaderTooLarge);
      return {currentState, failure, result.consumed};
    }
    headerBuffer[headerSize++] =
        static_cast<char>(data[result.consumed++]);
    if (headerSize >= 4 &&
        memcmp(headerBuffer + headerSize - 4U, "\r\n\r\n", 4) == 0) {
      headerBuffer[headerSize] = '\0';
      headersComplete = true;
      if (!parseHeaders()) {
        return {currentState, failure, result.consumed};
      }
    }
  }

  if (headersComplete && expectedBody > bodyReceived) {
    const size_t remaining = expectedBody - bodyReceived;
    const size_t available = size - result.consumed;
    const size_t amount = remaining < available ? remaining : available;
    if (amount > 0) {
      memcpy(bodyBuffer + bodyReceived, data + result.consumed, amount);
      bodyReceived += amount;
      result.consumed += amount;
    }
  }

  if (headersComplete && bodyReceived == expectedBody) {
    parsed.body = bodyBuffer;
    parsed.bodySize = expectedBody;
    currentState = ParseState::Complete;
  }
  result.state = currentState;
  result.error = failure;
  return result;
}

bool RequestParser::parseHeaders() {
  // Bare LF/CR makes framing interpretation dependent on the downstream parser.
  for (size_t i = 0; i < headerSize; i++) {
    if (headerBuffer[i] == '\r' &&
        (i + 1U >= headerSize || headerBuffer[i + 1U] != '\n')) {
      return fail(ParseError::InvalidHeader);
    }
    if (headerBuffer[i] == '\n' &&
        (i == 0 || headerBuffer[i - 1U] != '\r')) {
      return fail(ParseError::InvalidHeader);
    }
  }

  char *lineEnd = strstr(headerBuffer, "\r\n");
  if (lineEnd == nullptr) {
    return fail(ParseError::MalformedRequestLine);
  }
  *lineEnd = '\0';
  char *firstSpace = strchr(headerBuffer, ' ');
  if (firstSpace == nullptr || firstSpace == headerBuffer) {
    return fail(ParseError::MalformedRequestLine);
  }
  char *secondSpace = strchr(firstSpace + 1, ' ');
  if (secondSpace == nullptr || secondSpace == firstSpace + 1 ||
      strchr(secondSpace + 1, ' ') != nullptr || secondSpace[1] == '\0') {
    return fail(ParseError::MalformedRequestLine);
  }
  *firstSpace = '\0';
  *secondSpace = '\0';
  parsed.method = parseMethod(headerBuffer,
                              static_cast<size_t>(firstSpace - headerBuffer));
  if (parsed.method == Method::Unknown) {
    return fail(ParseError::UnsupportedMethod);
  }
  parsed.target = firstSpace + 1;
  const size_t targetLength = strlen(parsed.target);
  if (targetLength == 0 || targetLength > limits.targetBytes ||
      (parsed.target[0] != '/' &&
       !(parsed.method == Method::Options &&
         strcmp(parsed.target, "*") == 0)) ||
      strchr(parsed.target, '#') != nullptr) {
    return fail(ParseError::InvalidTarget);
  }
  for (size_t i = 0; i < targetLength; i++) {
    if (!validTargetByte(static_cast<unsigned char>(parsed.target[i]))) {
      return fail(ParseError::InvalidTarget);
    }
  }
  parsed.version = secondSpace + 1;
  const bool http11 = strcmp(parsed.version, "HTTP/1.1") == 0;
  if (!http11 && strcmp(parsed.version, "HTTP/1.0") != 0) {
    return fail(ParseError::UnsupportedVersion);
  }

  bool sawHost = false;
  bool sawLength = false;
  bool sawAuthorization = false;
  char *cursor = lineEnd + 2;
  while (cursor[0] != '\r' || cursor[1] != '\n') {
    char *end = strstr(cursor, "\r\n");
    if (end == nullptr || end == cursor || cursor[0] == ' ' ||
        cursor[0] == '\t') {
      return fail(ParseError::InvalidHeader);
    }
    *end = '\0';
    char *colon = strchr(cursor, ':');
    if (colon == nullptr || colon == cursor) {
      return fail(ParseError::InvalidHeader);
    }
    *colon = '\0';
    for (char *name = cursor; *name != '\0'; name++) {
      const unsigned char c = static_cast<unsigned char>(*name);
      if (!tokenCharacter(c)) {
        return fail(ParseError::InvalidHeader);
      }
      if (*name >= 'A' && *name <= 'Z') {
        *name = static_cast<char>(*name - 'A' + 'a');
      }
    }
    char *value = colon + 1;
    while (*value == ' ' || *value == '\t') {
      value++;
    }
    char *valueEnd = end;
    while (valueEnd > value &&
           (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) {
      *--valueEnd = '\0';
    }
    for (char *at = value; *at != '\0'; at++) {
      const unsigned char c = static_cast<unsigned char>(*at);
      if ((c < 0x20U && c != '\t') || c == 0x7fU) {
        return fail(ParseError::InvalidHeader);
      }
    }
    if (parsed.headerCount >= limits.headers) {
      return fail(ParseError::TooManyHeaders);
    }
    parsed.headers[parsed.headerCount++] = {cursor, value};

    if (strcmp(cursor, "host") == 0) {
      if (sawHost || value[0] == '\0') {
        return fail(ParseError::AmbiguousFraming);
      }
      sawHost = true;
    } else if (strcmp(cursor, "content-length") == 0) {
      if (sawLength || value[0] == '\0') {
        return fail(ParseError::AmbiguousFraming);
      }
      sawLength = true;
      size_t length = 0;
      for (const char *digit = value; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '9') {
          return fail(ParseError::AmbiguousFraming);
        }
        const size_t valueDigit = static_cast<size_t>(*digit - '0');
        if (valueDigit > limits.bodyBytes ||
            length > (limits.bodyBytes - valueDigit) / 10U) {
          return fail(ParseError::BodyTooLarge);
        }
        length = length * 10U + valueDigit;
      }
      expectedBody = length;
    } else if (strcmp(cursor, "transfer-encoding") == 0) {
      // Supporting both Content-Length and transfer codings safely adds request
      // smuggling surface. This small server deliberately supports neither
      // chunked requests nor other transfer codings.
      return fail(ParseError::UnsupportedTransferEncoding);
    } else if (strcmp(cursor, "authorization") == 0) {
      if (sawAuthorization) {
        return fail(ParseError::AmbiguousFraming);
      }
      sawAuthorization = true;
    }
    cursor = end + 2;
  }

  if (http11 && !sawHost) {
    return fail(ParseError::MissingHost);
  }
  if (expectedBody > 0) {
    bodyBuffer = static_cast<uint8_t *>(psram::allocate(expectedBody));
    if (bodyBuffer == nullptr) {
      return fail(ParseError::NoMemory);
    }
  }
  return true;
}

bool normalizeUrlPath(const char *target, char *out, size_t outSize) {
  if (target == nullptr || out == nullptr || outSize < 2 ||
      target[0] != '/') {
    return false;
  }
  size_t written = 0;
  size_t componentStart = 1;
  for (size_t i = 0; target[i] != '\0' && target[i] != '?'; i++) {
    unsigned char value = static_cast<unsigned char>(target[i]);
    if (target[i] == '%') {
      if (target[i + 1U] == '\0' || target[i + 2U] == '\0') {
        return false;
      }
      const int high = hexValue(target[i + 1U]);
      const int low = high >= 0 ? hexValue(target[i + 2U]) : -1;
      if (high < 0 || low < 0) {
        return false;
      }
      value = static_cast<unsigned char>((high << 4U) | low);
      i += 2U;
      // Encoded separators are rejected even though literal '/' is allowed.
      if (value == '/' || value == '\\') {
        return false;
      }
    }
    if (value == 0 || value < 0x20U || value == 0x7fU || value == '\\' ||
        written + 1U >= outSize) {
      return false;
    }
    if (value == '/') {
      if (written > 0 && out[written - 1U] == '/') {
        return false;
      }
      const size_t componentLength =
          written >= componentStart ? written - componentStart : 0;
      if ((componentLength == 1 && out[componentStart] == '.') ||
          (componentLength == 2 && out[componentStart] == '.' &&
           out[componentStart + 1U] == '.')) {
        return false;
      }
      componentStart = written + 1U;
    }
    out[written++] = static_cast<char>(value);
  }
  if (written > 1 && out[written - 1U] == '/') {
    written--;
  }
  const size_t componentLength =
      written >= componentStart ? written - componentStart : 0;
  if ((componentLength == 1 && out[componentStart] == '.') ||
      (componentLength == 2 && out[componentStart] == '.' &&
       out[componentStart + 1U] == '.')) {
    return false;
  }
  out[written] = '\0';
  return normalizedAbsolutePath(out);
}

bool staticFilePath(const char *root, const char *prefix,
                    const char *normalizedUrlPath, char *out,
                    size_t outSize) {
  if (!normalizedAbsolutePath(root) || !normalizedAbsolutePath(prefix) ||
      !normalizedAbsolutePath(normalizedUrlPath) || out == nullptr ||
      outSize == 0) {
    return false;
  }
  const size_t prefixLength = strlen(prefix);
  const bool rootPrefix = prefixLength == 1;
  if (!rootPrefix &&
      (strncmp(normalizedUrlPath, prefix, prefixLength) != 0 ||
       (normalizedUrlPath[prefixLength] != '\0' &&
        normalizedUrlPath[prefixLength] != '/'))) {
    return false;
  }
  const char *relative =
      rootPrefix ? normalizedUrlPath + 1U
                 : normalizedUrlPath + prefixLength +
                       (normalizedUrlPath[prefixLength] == '/' ? 1U : 0U);
  const size_t rootLength = strlen(root);
  const bool rootSlash = rootLength == 1;
  const size_t relativeLength = strlen(relative);
  const size_t separator = !rootSlash && relativeLength > 0 ? 1U : 0U;
  if (rootLength + separator + relativeLength >= outSize) {
    return false;
  }
  memcpy(out, root, rootLength);
  size_t written = rootLength;
  if (separator != 0) {
    out[written++] = '/';
  }
  if (relativeLength > 0) {
    memcpy(out + written, relative, relativeLength);
    written += relativeLength;
  }
  out[written] = '\0';
  return normalizedAbsolutePath(out);
}

} // namespace net::http
