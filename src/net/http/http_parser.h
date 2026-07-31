/// Bounded HTTP/1 request parser. It intentionally accepts one request per
/// connection and rejects ambiguous framing instead of attempting recovery.
#ifndef POCKETINK_NET_HTTP_PARSER_H
#define POCKETINK_NET_HTTP_PARSER_H

#include <stddef.h>
#include <stdint.h>

namespace net::http {

enum class Method : uint8_t {
  Get,
  Post,
  Put,
  Delete,
  Options,
  Head,
  Unknown,
};

const char *methodName(Method method);
Method parseMethod(const char *text, size_t length);

struct Header {
  const char *name = nullptr;
  const char *value = nullptr;
};

struct Request {
  static constexpr uint8_t kHeaderCapacity = 24;

  const char *target = nullptr;
  const char *version = nullptr;
  const uint8_t *body = nullptr;
  size_t bodySize = 0;
  Method method = Method::Unknown;
  Header headers[kHeaderCapacity] = {};
  uint8_t headerCount = 0;

  /// Header names are stored lower-case. Returns null when absent or repeated.
  const char *header(const char *lowerCaseName) const;
};

struct ParserLimits {
  size_t headerBytes = 4096;
  size_t bodyBytes = 64U * 1024U;
  uint16_t targetBytes = 768;
  uint8_t headers = Request::kHeaderCapacity;
};

enum class ParseError : uint8_t {
  None,
  NoMemory,
  HeaderTooLarge,
  TooManyHeaders,
  MalformedRequestLine,
  UnsupportedVersion,
  UnsupportedMethod,
  InvalidTarget,
  InvalidHeader,
  MissingHost,
  AmbiguousFraming,
  UnsupportedTransferEncoding,
  BodyTooLarge,
};

enum class ParseState : uint8_t { Incomplete, Complete, Failed };

struct ParseResult {
  ParseState state = ParseState::Incomplete;
  ParseError error = ParseError::None;
  size_t consumed = 0;
};

class RequestParser {
public:
  explicit RequestParser(ParserLimits limits = {});
  ~RequestParser();

  RequestParser(const RequestParser &) = delete;
  RequestParser &operator=(const RequestParser &) = delete;

  /// Consumes no bytes after the first complete request, leaving pipelined data
  /// to the caller (the server closes the connection after one response).
  ParseResult feed(const uint8_t *data, size_t size);
  const Request &request() const { return parsed; }
  ParseError error() const { return failure; }
  ParseState state() const { return currentState; }
  void reset();

private:
  ParserLimits limits;
  Request parsed;
  char *headerBuffer = nullptr;
  uint8_t *bodyBuffer = nullptr;
  size_t headerSize = 0;
  size_t expectedBody = 0;
  size_t bodyReceived = 0;
  ParseError failure = ParseError::None;
  ParseState currentState = ParseState::Incomplete;
  bool headersComplete = false;

  bool ensureHeaderBuffer();
  bool parseHeaders();
  bool fail(ParseError error);
};

/// Decodes and normalizes the origin-form path from a request target. It
/// rejects encoded separators, dot components, backslashes, controls, and
/// repeated separators so static mounts have one unambiguous path.
bool normalizeUrlPath(const char *target, char *out, size_t outSize);

/// Joins one normalized URL path below an absolute static root. The prefix is a
/// normalized URL mount such as "/" or "/assets".
bool staticFilePath(const char *root, const char *prefix,
                    const char *normalizedUrlPath, char *out, size_t outSize);

} // namespace net::http

#endif
