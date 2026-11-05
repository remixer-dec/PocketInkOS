#include "net/network_services.h"

#include "net/dns_forwarder.h"
#include "net/ftp_server.h"
#include "net/network_config.h"
#include "net/network_interface.h"
#include "net/socks5_tether.h"
#include "net/web_service.h"
#include "sys/psram_allocator.h"

#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <new>

namespace net {
namespace {

struct State {
  DnsForwarder dns;
  Socks5Tether proxy;
  WebService web;
  FtpServer ftp;
  TetherRoute route = TetherRoute::Direct;
  DnsMode dnsMode = DnsMode::Default;
  uint32_t policyRefreshAt = 0;
  uint32_t proxyRetryAt = 0;
  bool policyKnown = false;
};

State *state = nullptr;
uint32_t nextDiscoveryAt = 0;
std::atomic_flag serviceLock = ATOMIC_FLAG_INIT;

class ServiceGuard {
public:
  ServiceGuard() {
    while (serviceLock.test_and_set(std::memory_order_acquire)) {
      delay(1);
    }
  }
  ~ServiceGuard() { serviceLock.clear(std::memory_order_release); }
};

void stopAll(State &current) {
  current.dns.stop();
  current.proxy.stop();
  current.web.stop();
  current.ftp.stop();
}

bool due(uint32_t now, uint32_t deadline) {
  return deadline == 0 || static_cast<int32_t>(now - deadline) >= 0;
}

void refreshPolicy(State &current, uint32_t now) {
  if (current.policyKnown && !due(now, current.policyRefreshAt)) {
    return;
  }
  current.route = configuredTetherRoute();
  current.dnsMode = configuredDnsMode();
  current.policyKnown = true;
  current.policyRefreshAt = now + 30000U;
}

State *ensureState() {
  if (state != nullptr) {
    return state;
  }
  void *memory = psram::allocate(sizeof(State));
  if (memory == nullptr) {
    return nullptr;
  }
  state = new (memory) State();
  return state;
}

bool startHttpServiceLocked() {
  if (!tetheringActive() && !startGeneratedTethering()) {
    return false;
  }
  State *current = ensureState();
  return current != nullptr &&
         current->web.start(configPort("network.http.port", 80));
}

void stopHttpServiceLocked() {
  if (state != nullptr) {
    state->web.stop();
  }
}

bool startFtpServiceLocked() {
  char username[33] = {};
  char password[65] = {};
  configValue("network.server.username", "pocketink", username,
              sizeof(username));
  configValue("network.server.password", "", password, sizeof(password));
  const size_t usernameLength = strnlen(username, sizeof(username));
  const size_t passwordLength = strnlen(password, sizeof(password));
  if (usernameLength == 0 || usernameLength >= sizeof(username) ||
      passwordLength < 8 || passwordLength >= sizeof(password) ||
      (!tetheringActive() && !startGeneratedTethering())) {
    memset(password, 0, sizeof(password));
    return false;
  }
  State *current = ensureState();
  const bool started = current != nullptr && current->ftp.start(
      configPort("network.ftp.port", 21),
      configPort("network.ftp.dataPort", 2122), username, password);
  memset(password, 0, sizeof(password));
  return started;
}

void stopFtpServiceLocked() {
  if (state != nullptr) {
    state->ftp.stop();
    state->web.setUploadsEnabled(false);
  }
}

} // namespace

bool startHttpService() {
  ServiceGuard guard;
  return startHttpServiceLocked();
}

void stopHttpService() {
  ServiceGuard guard;
  stopHttpServiceLocked();
}

bool toggleHttpService() {
  ServiceGuard guard;
  if (state != nullptr && state->web.running()) {
    stopHttpServiceLocked();
    return false;
  }
  return startHttpServiceLocked();
}

bool startFtpService() {
  ServiceGuard guard;
  return startFtpServiceLocked();
}

void stopFtpService() {
  ServiceGuard guard;
  stopFtpServiceLocked();
}

bool toggleFtpService() {
  ServiceGuard guard;
  if (state != nullptr && state->ftp.running()) {
    stopFtpServiceLocked();
    return false;
  }
  return startFtpServiceLocked();
}

void networkServicesUpdate(uint32_t nowMs) {
  ServiceGuard guard;
  if (state == nullptr) {
    if (!due(nowMs, nextDiscoveryAt)) {
      return;
    }
    nextDiscoveryAt = nowMs + 5000U;
    if (tetheringActive()) {
      const TetherRoute route = configuredTetherRoute();
      const DnsMode dnsMode = configuredDnsMode();
      if (proxyRoute(route) ||
          (tetherRoutingActive() && route == TetherRoute::Direct &&
           dnsMode == DnsMode::Doh)) {
        State *created = ensureState();
        if (created != nullptr) {
          created->route = route;
          created->dnsMode = dnsMode;
          created->policyKnown = true;
          created->policyRefreshAt = nowMs + 30000U;
        }
      }
    }
    if (state == nullptr) {
      return;
    }
  }
  if (!tetheringActive()) {
    stopAll(*state);
    return;
  }
  refreshPolicy(*state, nowMs);
  const InterfaceSnapshot interface = interfaceSnapshot();
  if (proxyRoute(state->route) && interface.stationConnected) {
    if (!state->proxy.running() && due(nowMs, state->proxyRetryAt)) {
      if (!state->proxy.start()) {
        state->proxyRetryAt = nowMs + 5000U;
      }
    }
    state->proxy.update(nowMs);
  } else {
    state->proxy.stop();
    state->proxyRetryAt = 0;
  }
  if (tetherRoutingActive() && state->route == TetherRoute::Direct &&
      state->dnsMode == DnsMode::Doh) {
    if (!state->dns.running()) {
      state->dns.start();
    }
    state->dns.update();
  } else {
    state->dns.stop();
  }
  state->ftp.update(nowMs);
  state->web.setUploadsEnabled(state->ftp.running());
  state->web.update(nowMs);
}

void networkServicesConfigChanged() {
  ServiceGuard guard;
  nextDiscoveryAt = 0;
  if (state != nullptr) {
    // Credentials, ports, route and trust settings are read only at service
    // start. Stop every live consumer before applying the new policy so old
    // authorization cannot remain usable and proxy routes never fall back.
    stopAll(*state);
    state->proxyRetryAt = 0;
    state->policyKnown = false;
    state->policyRefreshAt = 0;
  }
  applyTetherRouting();
}

void networkServicesShutdown() {
  ServiceGuard guard;
  if (state == nullptr) {
    return;
  }
  stopAll(*state);
  state->~State();
  psram::release(state);
  state = nullptr;
  nextDiscoveryAt = 0;
}

NetworkServicesSnapshot networkServicesSnapshot() {
  ServiceGuard guard;
  NetworkServicesSnapshot result;
  if (state != nullptr) {
    result.http = state->web.running();
    result.ftp = state->ftp.running();
    result.dns = state->dns.running();
    result.proxy = state->proxy.running() && tetherRoutingActive();
  }
  return result;
}

} // namespace net
