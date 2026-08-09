/// IPv4 TCP/UDP inspection and checksum-safe endpoint rewriting.
#ifndef POCKETINK_NET_TRANSPARENT_PROXY_PACKET_H
#define POCKETINK_NET_TRANSPARENT_PROXY_PACKET_H

#include <stddef.h>
#include <stdint.h>

namespace net::transparent {

enum class Protocol : uint8_t { Tcp = 6, Udp = 17 };

struct Ipv4HeaderView {
  uint8_t sourceAddress[4] = {};
  uint8_t destinationAddress[4] = {};
  uint16_t ipOffset = 0;
  uint16_t ipHeaderSize = 0;
  uint16_t ipPacketSize = 0;
  uint8_t protocol = 0;
  bool fragmented = false;
};

struct PacketView {
  uint8_t sourceAddress[4] = {};
  uint8_t destinationAddress[4] = {};
  uint16_t sourcePort = 0;
  uint16_t destinationPort = 0;
  uint16_t ipOffset = 0;
  uint16_t ipHeaderSize = 0;
  uint16_t ipPacketSize = 0;
  uint8_t tcpFlags = 0;
  Protocol protocol = Protocol::Tcp;
  bool fragmented = false;
};

/// Inspects one complete Ethernet frame or bare IPv4 packet.
bool inspectIpv4Header(const uint8_t *data, size_t size, bool ethernetFrame,
                       Ipv4HeaderView &out);
bool inspectPacket(const uint8_t *data, size_t size, bool ethernetFrame,
                   PacketView &out);
bool rewriteDestination(uint8_t *data, size_t size, const PacketView &view,
                        const uint8_t address[4], uint16_t port);
bool rewriteSource(uint8_t *data, size_t size, const PacketView &view,
                   const uint8_t address[4], uint16_t port);
bool checksumsValid(const uint8_t *data, size_t size, const PacketView &view);

} // namespace net::transparent

#endif
