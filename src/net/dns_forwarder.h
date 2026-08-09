/// Bounded UDP DNS-to-DoH adapter for clients on the device hotspot.
#ifndef POCKETINK_NET_DNS_FORWARDER_H
#define POCKETINK_NET_DNS_FORWARDER_H

#include "net/network_stack.h"

#include <stdint.h>

namespace net {

struct DnsForwarderImpl;

class DnsForwarder {
public:
  DnsForwarder() = default;
  ~DnsForwarder();

  DnsForwarder(const DnsForwarder &) = delete;
  DnsForwarder &operator=(const DnsForwarder &) = delete;

  bool start();
  void stop();
  bool running() const;
  void update();

private:
  SocketHandle socket = kInvalidSocket;
  DnsForwarderImpl *impl = nullptr;
};

} // namespace net

#endif
