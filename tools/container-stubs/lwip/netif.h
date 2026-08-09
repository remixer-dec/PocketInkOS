#pragma once

#include "lwip/err.h"
#include "lwip/ip_addr.h"

struct pbuf;
struct netif;

using netif_input_fn = err_t (*)(pbuf *, netif *);
using netif_output_fn = err_t (*)(netif *, pbuf *, const ip4_addr_t *);

struct netif {
  netif_input_fn input = nullptr;
  netif_output_fn output = nullptr;
};

inline netif *netif_default = nullptr;
