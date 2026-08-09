/// Lifecycle facade for hotspot-bound DNS, HTTP, and FTP services.
#ifndef POCKETINK_NET_NETWORK_SERVICES_H
#define POCKETINK_NET_NETWORK_SERVICES_H

#include <stdint.h>

namespace net {

struct NetworkServicesSnapshot {
  bool http = false;
  bool ftp = false;
  bool dns = false;
  bool proxy = false;
};

bool startHttpService();
void stopHttpService();
bool toggleHttpService();
bool startFtpService();
void stopFtpService();
bool toggleFtpService();
void networkServicesUpdate(uint32_t nowMs);
/// Invalidates lazily cached regular cfg values after an in-app edit.
void networkServicesConfigChanged();
void networkServicesShutdown();
NetworkServicesSnapshot networkServicesSnapshot();

} // namespace net

#endif
