/// Lazy accessors for network settings stored as ordinary system cfg records.
#ifndef POCKETINK_NET_NETWORK_CONFIG_H
#define POCKETINK_NET_NETWORK_CONFIG_H

#include <stddef.h>
#include <stdint.h>

namespace net {

enum class TetherRoute : uint8_t {
  Direct,
  Socks5,
  HttpConnect,
  HttpsConnect,
};
enum class DnsMode : uint8_t { Default, Cloudflare, Doh };

/// Reads a network cfg key, substituting the supplied flash-string default.
bool configValue(const char *key, const char *fallback, char *out,
                 size_t outSize);
/// Reads a port with range validation and a deterministic fallback.
uint16_t configPort(const char *key, uint16_t fallback);

/// Strict syntax checks used before hostname resolution or HTTP parsing.
bool validProxyHost(const char *host);
bool validDohEndpoint(const char *url);

TetherRoute configuredTetherRoute();
DnsMode configuredDnsMode();

/// Validates the selected proxy profile without opening a connection.
bool configuredProxyReady();
/// Returns a short user-facing route state. Proxy routes deliberately fail
/// closed until a transparent packet adapter is available.
const char *configuredRouteStatus();

} // namespace net

#endif
