/// On-demand OpenAI-compatible HTTP/TLS transport shared by native and .pink
/// agent clients. The implementation owns no network state until begin().
#ifndef SYS_AGENTFIESTA_OPENAI_TRANSPORT_H
#define SYS_AGENTFIESTA_OPENAI_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

namespace agentfiesta {

enum class TransportStatus : int8_t {
  Error = -1,
  Unsupported = -3,
  Pending = 0,
  Ok = 1,
  End = 2,
};

class OpenAiTransport {
public:
  OpenAiTransport() = default;
  ~OpenAiTransport();

  OpenAiTransport(const OpenAiTransport &) = delete;
  OpenAiTransport &operator=(const OpenAiTransport &) = delete;

  /// Opens the configured base URL and writes a JSON POST header. endpoint is
  /// appended to the base path, for example "/chat/completions".
  TransportStatus begin(const char *baseUrl, const char *apiKey,
                        const char *endpoint, size_t contentLength,
                        const char *userAgent = "PocketInk/AgentFiesta");
  /// Writes the complete request payload after begin().
  bool writeAll(const uint8_t *data, size_t length);
  bool writeAll(const char *text);
  /// Parses the HTTP status and response framing after the payload is written.
  TransportStatus finishRequest();
  /// Reads one decoded response-body byte without blocking. Chunk framing is
  /// consumed internally.
  bool readBodyByte(char &out);
  /// Reports End after content-length/chunk completion or connection close.
  TransportStatus responseStatus();
  bool connected() const;
  bool timedOut(uint32_t timeoutMs) const;
  const char *error() const;
  void stop();

private:
  struct Impl;
  Impl *impl = nullptr;
};

/// Computes the exact RFC 4648 base64 length, or zero on overflow.
size_t base64EncodedSize(size_t inputBytes);
/// Encodes bytes into a caller-provided buffer including a trailing NUL.
bool base64Encode(const uint8_t *input, size_t inputBytes, char *out,
                  size_t outSize);

} // namespace agentfiesta

#endif
