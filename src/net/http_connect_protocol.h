/// Bounded HTTP/1 CONNECT request and response helpers.
#ifndef POCKETINK_NET_HTTP_CONNECT_PROTOCOL_H
#define POCKETINK_NET_HTTP_CONNECT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace net::http_connect {

enum class ParseState : uint8_t {
  Incomplete,
  Accepted,
  Rejected,
  Malformed,
};

constexpr size_t kMaximumResponseHeaderSize = 1024;

/// Basic-auth credentials are optional, but must be supplied as a pair.
bool validCredentials(const char *username, const char *password);

/// Builds a CONNECT request for an IPv4 authority. Returns zero on invalid
/// input or insufficient output capacity.
size_t buildIpv4Request(const uint8_t address[4], uint16_t port,
                        const char *username, const char *password,
                        uint8_t *out, size_t capacity);

/// Parses one bounded response header. `consumed` excludes any tunneled bytes
/// received in the same read.
ParseState parseResponse(const uint8_t *data, size_t size, size_t &consumed);

} // namespace net::http_connect

#endif
