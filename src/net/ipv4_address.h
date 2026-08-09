#ifndef POCKETINK_NET_IPV4_ADDRESS_H
#define POCKETINK_NET_IPV4_ADDRESS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace net {

inline bool parseIpv4Address(const char *text, uint8_t out[4]) {
  if (text == nullptr || out == nullptr) {
    return false;
  }

  uint8_t parsed[4] = {};
  const char *cursor = text;
  for (size_t part = 0; part < 4U; part++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') {
      return false;
    }

    unsigned value = 0;
    size_t digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
      if (digits == 3U) {
        return false;
      }
      value = value * 10U + static_cast<unsigned>(*cursor - '0');
      if (value > 255U) {
        return false;
      }
      cursor++;
      digits++;
    }
    parsed[part] = static_cast<uint8_t>(value);

    if (part < 3U) {
      if (*cursor != '.') {
        return false;
      }
      cursor++;
    } else if (*cursor != '\0') {
      return false;
    }
  }

  for (size_t i = 0; i < 4U; i++) {
    out[i] = parsed[i];
  }
  return true;
}

inline bool formatIpv4Address(const uint8_t address[4], char *out,
                              size_t outSize) {
  if (address == nullptr || out == nullptr || outSize == 0U) {
    return false;
  }
  const int written = snprintf(out, outSize, "%u.%u.%u.%u", address[0],
                               address[1], address[2], address[3]);
  if (written <= 0 || static_cast<size_t>(written) >= outSize) {
    out[0] = '\0';
    return false;
  }
  return true;
}

} // namespace net

#endif
