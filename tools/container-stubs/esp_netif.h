#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_ip6_addr {
    uint32_t addr[4];
    uint8_t zone;
} esp_ip6_addr_t;

typedef void *esp_netif_t;

#define ESP_OK 0

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
int esp_netif_get_ip6_global(esp_netif_t *netif, esp_ip6_addr_t *ip6);
int esp_netif_get_ip6_linklocal(esp_netif_t *netif, esp_ip6_addr_t *ip6);

#ifdef __cplusplus
}
#endif
