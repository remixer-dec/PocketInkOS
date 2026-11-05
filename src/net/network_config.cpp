#include "net/network_config.h"

#include "net/http_connect_protocol.h"
#include "sys/system_config.h"

#include <cstdlib>
#include <cstring>

namespace net {

bool validProxyHost(const char *host) {
  if (host == nullptr) {
    return false;
  }
  const size_t length = strnlen(host, 96);
  if (length == 0 || length >= 96 || host[0] == '.' ||
      host[length - 1U] == '.') {
    return false;
  }
  size_t labelLength = 0;
  for (size_t i = 0; i < length; i++) {
    const char c = host[i];
    if (c == '.') {
      if (labelLength == 0 || host[i - 1U] == '-') {
        return false;
      }
      labelLength = 0;
      continue;
    }
    const bool allowed = (c >= 'a' && c <= 'z') ||
                         (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '-';
    if (!allowed || labelLength >= 63 ||
        (labelLength == 0 && c == '-')) {
      return false;
    }
    labelLength++;
  }
  return labelLength > 0 && host[length - 1U] != '-';
}

bool validDohEndpoint(const char *url) {
  if (url == nullptr) {
    return false;
  }
  const size_t length = strnlen(url, 96U);
  if (length < 10U || length >= 96U || strncmp(url, "https://", 8U) != 0) {
    return false;
  }
  const char *authority = url + 8U;
  const char *path = strchr(authority, '/');
  if (path == nullptr || path == authority ||
      strchr(authority, '@') != nullptr || strchr(path, '#') != nullptr) {
    return false;
  }
  const size_t authorityLength = static_cast<size_t>(path - authority);
  const char *colon = static_cast<const char *>(
      memchr(authority, ':', authorityLength));
  size_t hostLength = authorityLength;
  if (colon != nullptr) {
    hostLength = static_cast<size_t>(colon - authority);
    if (hostLength == 0 || colon + 1 == path) {
      return false;
    }
    unsigned port = 0;
    for (const char *at = colon + 1; at < path; at++) {
      if (*at < '0' || *at > '9') {
        return false;
      }
      const unsigned digit = static_cast<unsigned>(*at - '0');
      if (port > (65535U - digit) / 10U) {
        return false;
      }
      port = port * 10U + digit;
    }
    if (port == 0) {
      return false;
    }
  }
  char host[96] = {};
  if (hostLength >= sizeof(host)) {
    return false;
  }
  memcpy(host, authority, hostLength);
  if (!validProxyHost(host)) {
    return false;
  }
  for (const unsigned char *at =
           reinterpret_cast<const unsigned char *>(path);
       *at != 0; at++) {
    if (*at < 0x21U || *at == 0x7fU || *at == '\\') {
      return false;
    }
  }
  return true;
}

bool configValue(const char *key, const char *fallback, char *out,
                 size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  if (key != nullptr && configGet(key, out, outSize)) {
    return true;
  }
  const char *value = fallback != nullptr ? fallback : "";
  const size_t length = strnlen(value, outSize);
  if (length >= outSize) {
    out[0] = '\0';
    return false;
  }
  memcpy(out, value, length + 1U);
  return true;
}

uint16_t configPort(const char *key, uint16_t fallback) {
  char value[8] = {};
  if (!configValue(key, "", value, sizeof(value)) || value[0] == '\0') {
    return fallback;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  return end != value && end != nullptr && *end == '\0' && parsed > 0 &&
                 parsed <= 65535UL
             ? static_cast<uint16_t>(parsed)
             : fallback;
}

TetherRoute configuredTetherRoute() {
  char value[20] = {};
  configValue("network.tether.route", "direct", value, sizeof(value));
  if (strcmp(value, "socks5") == 0) {
    return TetherRoute::Socks5;
  }
  if (strcmp(value, "http-connect") == 0) {
    return TetherRoute::HttpConnect;
  }
  if (strcmp(value, "https-connect") == 0) {
    return TetherRoute::HttpsConnect;
  }
  return TetherRoute::Direct;
}

DnsMode configuredDnsMode() {
  char value[16] = {};
  configValue("network.dns.mode", "default", value, sizeof(value));
  if (strcmp(value, "cloudflare") == 0) {
    return DnsMode::Cloudflare;
  }
  if (strcmp(value, "doh") == 0) {
    return DnsMode::Doh;
  }
  return DnsMode::Default;
}

bool configuredProxyReady() {
  const TetherRoute route = configuredTetherRoute();
  if (route == TetherRoute::Direct) {
    return true;
  }
  char host[96] = {};
  char username[65] = {};
  char password[97] = {};
  configValue("network.proxy.host", "", host, sizeof(host));
  configValue("network.proxy.username", "", username, sizeof(username));
  configValue("network.proxy.password", "", password, sizeof(password));
  const bool usernameSet = username[0] != '\0';
  const bool passwordSet = password[0] != '\0';
  if (!validProxyHost(host) || configPort("network.proxy.port", 0) == 0 ||
      usernameSet != passwordSet) {
    return false;
  }
  return route == TetherRoute::Socks5 ||
         http_connect::validCredentials(username, password);
}

const char *configuredRouteStatus() {
  const TetherRoute route = configuredTetherRoute();
  if (!proxyRoute(route)) {
    return "direct";
  }
  if (configuredDnsMode() == DnsMode::Doh) {
    return "DoH incompatible";
  }
  if (!configuredProxyReady()) {
    return "proxy incomplete";
  }
  return route == TetherRoute::Socks5        ? "socks5 starting"
         : route == TetherRoute::HttpConnect ? "http proxy starting"
                                             : "https proxy starting";
}

} // namespace net
