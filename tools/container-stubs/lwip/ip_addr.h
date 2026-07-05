#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ip_addr {
  uint32_t addr;
} ip_addr_t;

typedef struct ip4_addr {
  uint32_t addr;
} ip4_addr_t;

const char *ipaddr_ntoa(const ip_addr_t *addr);

#ifdef __cplusplus
}
#endif

#include <cstdint>
#define IP_ADDR4(ipaddr, a, b, c, d) ((ipaddr)->addr = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d))
#define IP4_ADDR(ipaddr, a, b, c, d) ((ipaddr)->addr = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d))
