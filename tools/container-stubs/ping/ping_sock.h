#pragma once

#include <cstdint>
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_ping_handle_t;

typedef enum {
  ESP_PING_PROF_SEQNO,
  ESP_PING_PROF_TTL,
  ESP_PING_PROF_TIMEGAP,
  ESP_PING_PROF_SIZE,
  ESP_PING_PROF_IPADDR,
  ESP_PING_PROF_REQUEST,
  ESP_PING_PROF_REPLY,
  ESP_PING_PROF_DURATION,
} esp_ping_profile_t;

typedef struct {
  uint32_t count;
  uint32_t interval_ms;
  uint32_t timeout_ms;
  uint32_t data_size;
  int tos;
  int ttl;
  ip_addr_t target_addr;
  ip_addr_t target_addr6;
  uint32_t interface;
} esp_ping_config_t;

#define ESP_PING_DEFAULT_CONFIG() \
  { 4, 1000, 1000, 64, 0, 0, 0, 0, 0 }

typedef struct {
  void (*on_ping_success)(esp_ping_handle_t, void *);
  void (*on_ping_timeout)(esp_ping_handle_t, void *);
  void (*on_ping_end)(esp_ping_handle_t, void *);
  void *cb_args;
} esp_ping_callbacks_t;

typedef int esp_err_t;

#ifndef ESP_OK
static const esp_err_t ESP_OK = 0;
#endif

esp_err_t esp_ping_new_session(const esp_ping_config_t *config,
                               const esp_ping_callbacks_t *cbs,
                               esp_ping_handle_t *handle_out);

void esp_ping_delete_session(esp_ping_handle_t handle);

esp_err_t esp_ping_start(esp_ping_handle_t handle);

esp_err_t esp_ping_stop(esp_ping_handle_t handle);

esp_err_t esp_ping_get_profile(esp_ping_handle_t handle,
                               esp_ping_profile_t profile,
                               void *data, uint32_t size);

#ifdef __cplusplus
}
#endif
