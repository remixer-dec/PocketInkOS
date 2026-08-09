#ifndef POCKETINK_NET_DNS_MESSAGE_H
#define POCKETINK_NET_DNS_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

namespace net::dns {

constexpr size_t kMaximumQuerySize = 1232;
constexpr size_t kMaximumResponseSize = 4096;

bool validQuery(const uint8_t *data, size_t size);
bool validResponse(const uint8_t *query, size_t querySize,
                   const uint8_t *response, size_t responseSize);

/// Builds a minimal SERVFAIL containing only the original question. This
/// deliberately removes EDNS and other additional records whose counts are
/// cleared in the response header.
size_t serviceFailure(const uint8_t *query, size_t querySize, uint8_t *out,
                      size_t capacity);

} // namespace net::dns

#endif
