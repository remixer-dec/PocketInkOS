#pragma once

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

struct eth_addr {
  uint8_t addr[6];
};

inline int etharp_find_addr(netif *, const ip4_addr_t *, eth_addr **,
                            const ip4_addr_t **) {
  return -1;
}
