/// Bounded SOCKS5 wire-format helpers shared by tethering and host tests.
#ifndef POCKETINK_NET_SOCKS5_PROTOCOL_H
#define POCKETINK_NET_SOCKS5_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace net::socks5 {

enum class Command : uint8_t { Connect = 1, UdpAssociate = 3 };
enum class ParseState : uint8_t {
  Incomplete,
  Accepted,
  Rejected,
  Malformed,
};

struct Ipv4Endpoint {
  uint8_t address[4] = {};
  uint16_t port = 0;
};

size_t buildGreeting(bool passwordAuth, uint8_t *out, size_t outSize);
ParseState parseMethodReply(const uint8_t *data, size_t size,
                            bool passwordAuth);

size_t buildPasswordAuth(const char *username, const char *password,
                         uint8_t *out, size_t outSize);
ParseState parsePasswordReply(const uint8_t *data, size_t size);

size_t buildIpv4Request(Command command, const Ipv4Endpoint &target,
                        uint8_t *out, size_t outSize);
ParseState parseCommandReply(const uint8_t *data, size_t size,
                             Ipv4Endpoint &bound, size_t &consumed);

size_t buildUdpDatagram(const Ipv4Endpoint &target, const uint8_t *payload,
                        size_t payloadSize, uint8_t *out, size_t outSize);
ParseState parseUdpDatagram(const uint8_t *data, size_t size,
                            Ipv4Endpoint &source, const uint8_t *&payload,
                            size_t &payloadSize);

} // namespace net::socks5

#endif
