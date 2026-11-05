#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#include "net/network_interface.h"
#include "net/network_config.h"
#include "sys/psram_allocator.h"

#if ENABLE_NETWORK_APPS
#include <WiFi.h>
#include <esp_random.h>
#endif

#include <cstdio>
#include <atomic>
#include <cstring>
#include <new>

namespace net {
namespace {

struct TetheringState {
  char ssid[33] = {};
  char password[64] = {};
};

TetheringState *tether = nullptr;
bool routingEnabled = false;
std::atomic_flag interfaceLock = ATOMIC_FLAG_INIT;

class InterfaceGuard {
public:
  InterfaceGuard() {
    while (interfaceLock.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~InterfaceGuard() { interfaceLock.clear(std::memory_order_release); }
};

bool validCredentialText(const char *text, size_t minimum, size_t maximum,
                         size_t &length) {
  length = 0;
  if (text == nullptr) {
    return false;
  }
  length = strnlen(text, maximum + 1U);
  if (length < minimum || length > maximum) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x20U || c == 0x7fU) {
      return false;
    }
  }
  return true;
}

#if ENABLE_NETWORK_APPS
bool copyAddress(char *out, size_t outSize, const IPAddress &address) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const int written =
      snprintf(out, outSize, "%u.%u.%u.%u", address[0], address[1],
               address[2], address[3]);
  if (written <= 0 || static_cast<size_t>(written) >= outSize) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// STA may be enabled while still associating or rediscovering; hotspot mode
// changes must keep it so repeater uplinks can recover.
bool stationEnabled() {
  return (WiFi.getMode() & WIFI_STA) != 0 || WiFi.status() == WL_CONNECTED;
}

IPAddress configuredApDns() {
  switch (configuredDnsMode()) {
  case DnsMode::Cloudflare:
    return IPAddress(1, 1, 1, 1);
  case DnsMode::Doh:
    // DoH clients query the device-local UDP forwarder.
    return IPAddress(192, 168, 4, 1);
  case DnsMode::Default:
    if (WiFi.status() == WL_CONNECTED) {
      return WiFi.dnsIP();
    }
    return IPAddress(1, 1, 1, 1);
  }
  return IPAddress(1, 1, 1, 1);
}

bool applyTetherRoutingLocked() {
  if (tether == nullptr) {
    routingEnabled = false;
    return false;
  }
  const IPAddress apAddress(192, 168, 4, 1);
  const IPAddress apMask(255, 255, 255, 0);
  const IPAddress leaseStart(192, 168, 4, 2);
  // Reconfiguration always starts closed so a failed AP/DHCP update cannot
  // leave the previous direct route active after selecting a proxy profile.
  WiFi.AP.enableNAPT(false);
  routingEnabled = false;
  if (!WiFi.AP.config(apAddress, apAddress, apMask, leaseStart,
                      configuredApDns())) {
    return false;
  }
  const bool direct = configuredTetherRoute() == TetherRoute::Direct;
  // Proxy profiles never fall back to direct forwarding: doing so would leak
  // client traffic outside the selected tunnel.
  routingEnabled = WiFi.AP.enableNAPT(direct) && direct;
  return routingEnabled;
}
#endif

} // namespace

bool startStationInterface() {
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  WiFi.persistent(false);
  return WiFi.mode(tether != nullptr ? WIFI_AP_STA : WIFI_STA);
#else
  return false;
#endif
}

void stopStationInterface() {
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  WiFi.mode(tether != nullptr ? WIFI_AP : WIFI_OFF);
#endif
}

bool startTethering(const char *ssid, const char *password) {
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  size_t ssidLength = 0;
  size_t passwordLength = 0;
  if (!validCredentialText(ssid, 1, 32, ssidLength) ||
      !validCredentialText(password, 8, 63, passwordLength)) {
    return false;
  }
  if (tether != nullptr && strcmp(tether->ssid, ssid) == 0 &&
      strcmp(tether->password, password) == 0) {
    return true;
  }
  void *memory = psram::allocate(sizeof(TetheringState));
  if (memory == nullptr) {
    return false;
  }
  TetheringState *created = new (memory) TetheringState();
  memcpy(created->ssid, ssid, ssidLength + 1U);
  memcpy(created->password, password, passwordLength + 1U);
  TetheringState *previous = tether;
  const bool station = stationEnabled();
  if (previous != nullptr) {
    WiFi.softAPdisconnect(false);
  }
  const IPAddress apAddress(192, 168, 4, 1);
  const IPAddress apMask(255, 255, 255, 0);
  const IPAddress leaseStart(192, 168, 4, 2);
  if (!WiFi.mode(station ? WIFI_AP_STA : WIFI_AP) ||
      !WiFi.AP.config(apAddress, apAddress, apMask, leaseStart,
                      configuredApDns()) ||
      !WiFi.softAP(ssid, password)) {
    WiFi.softAPdisconnect(false);
    if (previous != nullptr) {
      WiFi.mode(station ? WIFI_AP_STA : WIFI_AP);
      if (!WiFi.softAP(previous->ssid, previous->password)) {
        memset(previous->password, 0, sizeof(previous->password));
        previous->~TetheringState();
        psram::release(previous);
        tether = nullptr;
        WiFi.mode(station ? WIFI_STA : WIFI_OFF);
      } else {
        routingEnabled = false;
        applyTetherRoutingLocked();
      }
    } else {
      WiFi.mode(station ? WIFI_STA : WIFI_OFF);
    }
    memset(created->password, 0, sizeof(created->password));
    created->~TetheringState();
    psram::release(created);
    return false;
  }
  tether = created;
  routingEnabled = false;
  applyTetherRoutingLocked();
  if (previous != nullptr) {
    memset(previous->password, 0, sizeof(previous->password));
    previous->~TetheringState();
    psram::release(previous);
  }
  return true;
#else
  (void)ssid;
  (void)password;
  return false;
#endif
}

bool startGeneratedTethering() {
#if ENABLE_NETWORK_APPS
  if (tetheringActive()) {
    return true;
  }
  char ssid[33] = {};
  char password[13] = {};
  // Suffix with the final four MAC hex digits, keeping "INK0" if absent.
  const String mac = WiFi.macAddress();
  char suffix[5] = "INK0";
  size_t suffixAt = 4;
  for (size_t i = mac.length(); i > 0 && suffixAt > 0; i--) {
    const char c = mac.c_str()[i - 1U];
    if (c != ':') {
      suffix[--suffixAt] = c;
    }
  }
  snprintf(ssid, sizeof(ssid), "PocketInk-%s", suffix);
  const uint32_t first = esp_random();
  const uint32_t second = esp_random();
  // Twelve hexadecimal characters keep the credential readable while
  // retaining 48 bits of per-boot randomness.
  snprintf(password, sizeof(password), "%08lx%04lx",
           static_cast<unsigned long>(first),
           static_cast<unsigned long>(second & 0xffffU));
  const bool started = startTethering(ssid, password);
  memset(password, 0, sizeof(password));
  return started;
#else
  return false;
#endif
}

void stopTethering() {
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  if (tether == nullptr) {
    return;
  }
  WiFi.AP.enableNAPT(false);
  routingEnabled = false;
  const bool station = stationEnabled();
  WiFi.softAPdisconnect(!station);
  memset(tether->password, 0, sizeof(tether->password));
  tether->~TetheringState();
  psram::release(tether);
  tether = nullptr;
  WiFi.mode(station ? WIFI_STA : WIFI_OFF);
#endif
}

bool tetheringActive() {
  InterfaceGuard guard;
  return tether != nullptr;
}

bool applyTetherRouting() {
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  return applyTetherRoutingLocked();
#else
  return false;
#endif
}

bool tetherRoutingActive() {
  InterfaceGuard guard;
  return tether != nullptr && routingEnabled;
}

void setTetherProxyRoutingActive(bool active) {
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  if (!active) {
    routingEnabled = false;
    return;
  }
  if (proxyRoute(configuredTetherRoute())) {
    routingEnabled = tether != nullptr && WiFi.status() == WL_CONNECTED;
  }
#else
  (void)active;
#endif
}

bool localAddress(char *out, size_t outSize) {
  if (out == nullptr || outSize < 16U) {
    return false;
  }
  out[0] = '\0';
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  if (WiFi.status() == WL_CONNECTED) {
    return copyAddress(out, outSize, WiFi.localIP());
  }
  if (tether != nullptr) {
    return copyAddress(out, outSize, WiFi.softAPIP());
  }
#endif
  return false;
}

InterfaceSnapshot interfaceSnapshot() {
  InterfaceSnapshot snapshot;
#if ENABLE_NETWORK_APPS
  InterfaceGuard guard;
  snapshot.stationConnected = WiFi.status() == WL_CONNECTED;
  snapshot.tethering = tether != nullptr;
  snapshot.routing = tether != nullptr && routingEnabled;
  if (snapshot.stationConnected) {
    copyAddress(snapshot.stationAddress, sizeof(snapshot.stationAddress),
                WiFi.localIP());
  }
  if (snapshot.tethering) {
    copyAddress(snapshot.tetherAddress, sizeof(snapshot.tetherAddress),
                WiFi.softAPIP());
    memcpy(snapshot.tetherSsid, tether->ssid, sizeof(snapshot.tetherSsid));
    memcpy(snapshot.tetherPassword, tether->password,
           sizeof(snapshot.tetherPassword));
    const TetherRoute route = configuredTetherRoute();
    const char *activeStatus = route == TetherRoute::Socks5
                                   ? "socks5"
                               : route == TetherRoute::HttpConnect
                                   ? "http proxy"
                               : route == TetherRoute::HttpsConnect
                                   ? "https proxy"
                                   : "direct";
    const char *status = routingEnabled ? activeStatus
                                        : configuredRouteStatus();
    strncpy(snapshot.routeStatus, status, sizeof(snapshot.routeStatus) - 1U);
  }
#endif
  return snapshot;
}

} // namespace net
