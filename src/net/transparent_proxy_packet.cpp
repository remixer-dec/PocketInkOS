#include "net/transparent_proxy_packet.h"

#include <cstring>

namespace net::transparent {
namespace {

constexpr size_t kEthernetHeaderSize = 14;
constexpr uint16_t kIpv4EtherType = 0x0800;

uint16_t read16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) << 8U | data[1];
}

void write16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value >> 8U);
  data[1] = static_cast<uint8_t>(value);
}

uint32_t addWords(uint32_t sum, const uint8_t *data, size_t size) {
  while (size >= 2) {
    sum += read16(data);
    data += 2;
    size -= 2;
  }
  if (size != 0) {
    sum += static_cast<uint16_t>(data[0]) << 8U;
  }
  return sum;
}

uint16_t finishChecksum(uint32_t sum) {
  while ((sum >> 16U) != 0) {
    sum = (sum & 0xffffU) + (sum >> 16U);
  }
  return static_cast<uint16_t>(~sum);
}

uint16_t transportChecksum(const uint8_t *ip, size_t ipHeaderSize,
                           size_t ipPacketSize, uint8_t protocol) {
  const size_t transportSize = ipPacketSize - ipHeaderSize;
  uint32_t sum = addWords(0, ip + 12, 8);
  sum += protocol;
  sum += static_cast<uint16_t>(transportSize);
  sum = addWords(sum, ip + ipHeaderSize, transportSize);
  return finishChecksum(sum);
}

bool validateView(const uint8_t *data, size_t size, const PacketView &view,
                  const uint8_t *&ip, const uint8_t *&transport,
                  size_t &transportSize) {
  ip = nullptr;
  transport = nullptr;
  transportSize = 0;
  const size_t ipOffset = view.ipOffset;
  const size_t ipHeaderSize = view.ipHeaderSize;
  const size_t ipPacketSize = view.ipPacketSize;
  if (data == nullptr || ipOffset > size || size - ipOffset < 20U ||
      ipHeaderSize < 20U || ipHeaderSize > 60U ||
      (ipHeaderSize & 3U) != 0 || ipPacketSize < ipHeaderSize ||
      ipPacketSize > size - ipOffset) {
    return false;
  }

  ip = data + ipOffset;
  if ((ip[0] >> 4U) != 4U ||
      static_cast<size_t>(ip[0] & 0x0fU) * 4U != ipHeaderSize ||
      read16(ip + 2U) != ipPacketSize) {
    return false;
  }
  const uint8_t expectedProtocol = static_cast<uint8_t>(view.protocol);
  if ((view.protocol != Protocol::Tcp && view.protocol != Protocol::Udp) ||
      ip[9] != expectedProtocol ||
      memcmp(ip + 12U, view.sourceAddress, 4U) != 0 ||
      memcmp(ip + 16U, view.destinationAddress, 4U) != 0) {
    return false;
  }
  const bool fragmented = (read16(ip + 6U) & 0x3fffU) != 0;
  if (fragmented || view.fragmented != fragmented) {
    return false;
  }

  transport = ip + ipHeaderSize;
  transportSize = ipPacketSize - ipHeaderSize;
  const size_t minimum = view.protocol == Protocol::Tcp ? 20U : 8U;
  if (transportSize < minimum || read16(transport) != view.sourcePort ||
      read16(transport + 2U) != view.destinationPort) {
    return false;
  }
  if (view.protocol == Protocol::Tcp) {
    const size_t tcpHeaderSize =
        static_cast<size_t>(transport[12] >> 4U) * 4U;
    return tcpHeaderSize >= 20U && tcpHeaderSize <= transportSize &&
           transport[13] == view.tcpFlags;
  }
  return read16(transport + 4U) == transportSize;
}

bool rewrite(uint8_t *data, size_t size, const PacketView &view,
             const uint8_t address[4], uint16_t port, bool source) {
  const uint8_t *checkedIp = nullptr;
  const uint8_t *checkedTransport = nullptr;
  size_t transportSize = 0;
  if (address == nullptr || port == 0 ||
      !validateView(data, size, view, checkedIp, checkedTransport,
                    transportSize)) {
    return false;
  }
  uint8_t *ip = data + view.ipOffset;
  uint8_t *transport = ip + view.ipHeaderSize;
  const size_t checksumOffset = view.protocol == Protocol::Tcp ? 16U : 6U;
  memcpy(ip + (source ? 12U : 16U), address, 4);
  write16(transport + (source ? 0U : 2U), port);

  write16(ip + 10, 0);
  write16(ip + 10, finishChecksum(addWords(0, ip, view.ipHeaderSize)));
  write16(transport + checksumOffset, 0);
  uint16_t transportValue =
      transportChecksum(ip, view.ipHeaderSize, view.ipPacketSize,
                        static_cast<uint8_t>(view.protocol));
  if (view.protocol == Protocol::Udp && transportValue == 0) {
    transportValue = 0xffffU;
  }
  write16(transport + checksumOffset, transportValue);
  return true;
}

} // namespace

bool inspectIpv4Header(const uint8_t *data, size_t size, bool ethernetFrame,
                       Ipv4HeaderView &out) {
  out = {};
  const size_t ipOffset = ethernetFrame ? kEthernetHeaderSize : 0;
  if (data == nullptr || ipOffset > size || size - ipOffset < 20U ||
      (ethernetFrame && read16(data + 12) != kIpv4EtherType)) {
    return false;
  }
  const uint8_t *ip = data + ipOffset;
  if ((ip[0] >> 4U) != 4) {
    return false;
  }
  const size_t ipHeaderSize = static_cast<size_t>(ip[0] & 0x0fU) * 4U;
  const size_t ipPacketSize = read16(ip + 2);
  if (ipHeaderSize < 20U || ipHeaderSize > 60U ||
      ipPacketSize < ipHeaderSize || ipPacketSize > size - ipOffset) {
    return false;
  }
  memcpy(out.sourceAddress, ip + 12, sizeof(out.sourceAddress));
  memcpy(out.destinationAddress, ip + 16, sizeof(out.destinationAddress));
  out.ipOffset = static_cast<uint16_t>(ipOffset);
  out.ipHeaderSize = static_cast<uint16_t>(ipHeaderSize);
  out.ipPacketSize = static_cast<uint16_t>(ipPacketSize);
  out.protocol = ip[9];
  const uint16_t fragment = read16(ip + 6);
  out.fragmented = (fragment & 0x3fffU) != 0;
  return true;
}

bool inspectPacket(const uint8_t *data, size_t size, bool ethernetFrame,
                   PacketView &out) {
  out = {};
  Ipv4HeaderView header;
  if (!inspectIpv4Header(data, size, ethernetFrame, header)) {
    return false;
  }
  const uint8_t *ip = data + header.ipOffset;
  Protocol protocol;
  size_t minimumTransport = 0;
  if (header.protocol == static_cast<uint8_t>(Protocol::Tcp)) {
    protocol = Protocol::Tcp;
    minimumTransport = 20;
  } else if (header.protocol == static_cast<uint8_t>(Protocol::Udp)) {
    protocol = Protocol::Udp;
    minimumTransport = 8;
  } else {
    return false;
  }
  if (static_cast<size_t>(header.ipPacketSize - header.ipHeaderSize) <
      minimumTransport) {
    return false;
  }
  const uint8_t *transport = ip + header.ipHeaderSize;
  const size_t transportSize =
      static_cast<size_t>(header.ipPacketSize - header.ipHeaderSize);
  if (protocol == Protocol::Tcp) {
    const size_t tcpHeaderSize =
        static_cast<size_t>(transport[12] >> 4U) * 4U;
    if (tcpHeaderSize < 20U || tcpHeaderSize > transportSize) {
      return false;
    }
  } else if (read16(transport + 4U) != transportSize) {
    return false;
  }
  memcpy(out.sourceAddress, header.sourceAddress, sizeof(out.sourceAddress));
  memcpy(out.destinationAddress, header.destinationAddress,
         sizeof(out.destinationAddress));
  out.sourcePort = read16(transport);
  out.destinationPort = read16(transport + 2);
  out.ipOffset = header.ipOffset;
  out.ipHeaderSize = header.ipHeaderSize;
  out.ipPacketSize = header.ipPacketSize;
  out.protocol = protocol;
  out.tcpFlags = protocol == Protocol::Tcp ? transport[13] : 0;
  out.fragmented = header.fragmented;
  return true;
}

bool rewriteDestination(uint8_t *data, size_t size, const PacketView &view,
                        const uint8_t address[4], uint16_t port) {
  return rewrite(data, size, view, address, port, false);
}

bool rewriteSource(uint8_t *data, size_t size, const PacketView &view,
                   const uint8_t address[4], uint16_t port) {
  return rewrite(data, size, view, address, port, true);
}

bool checksumsValid(const uint8_t *data, size_t size, const PacketView &view) {
  const uint8_t *ip = nullptr;
  const uint8_t *transport = nullptr;
  size_t transportSize = 0;
  if (!validateView(data, size, view, ip, transport, transportSize)) {
    return false;
  }
  if (finishChecksum(addWords(0, ip, view.ipHeaderSize)) != 0) {
    return false;
  }
  if (view.protocol == Protocol::Udp && read16(transport + 6) == 0) {
    return true;
  }
  return transportChecksum(ip, view.ipHeaderSize, view.ipPacketSize,
                           static_cast<uint8_t>(view.protocol)) == 0;
}

} // namespace net::transparent
