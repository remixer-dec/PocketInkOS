#pragma once

#include "lwip/err.h"

#include <cstdint>

enum pbuf_layer { PBUF_RAW, PBUF_LINK };
enum pbuf_type { PBUF_RAM };

struct pbuf {
  pbuf *next = nullptr;
  void *payload = nullptr;
  uint16_t tot_len = 0;
  uint16_t len = 0;
};

pbuf *pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type);
err_t pbuf_copy(pbuf *destination, const pbuf *source);
uint16_t pbuf_copy_partial(const pbuf *source, void *destination,
                           uint16_t length, uint16_t offset);
uint8_t pbuf_free(pbuf *packet);
