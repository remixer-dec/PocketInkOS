#pragma once
#include "Arduino.h"
#include <cstddef>

#ifndef ESP_OK
using esp_err_t = int;
#define ESP_OK 0
#endif

#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2

using i2s_port_t = int;
using i2s_chan_handle_t = void *;

struct i2s_chan_config_t {
  int id;
  int role;
};

struct i2s_std_clk_config_t {
  uint32_t sample_rate_hz;
};

struct i2s_std_slot_config_t {
  int data_bit_width;
  int slot_mode;
};

struct i2s_std_gpio_config_t {
  gpio_num_t mclk;
  gpio_num_t bclk;
  gpio_num_t ws;
  gpio_num_t dout;
  gpio_num_t din;
};

struct i2s_std_config_t {
  i2s_std_clk_config_t clk_cfg;
  i2s_std_slot_config_t slot_cfg;
  i2s_std_gpio_config_t gpio_cfg;
};

#define I2S_CHANNEL_DEFAULT_CONFIG(id_value, role_value) { id_value, role_value }
#define I2S_STD_CLK_DEFAULT_CONFIG(rate_value) { rate_value }
#define I2S_STD_MSB_SLOT_DEFAULT_CONFIG(width_value, mode_value) { width_value, mode_value }

inline esp_err_t i2s_new_channel(const i2s_chan_config_t *,
                                 i2s_chan_handle_t *tx,
                                 i2s_chan_handle_t *rx) {
  if (tx) *tx = reinterpret_cast<void *>(1);
  if (rx) *rx = reinterpret_cast<void *>(1);
  return ESP_OK;
}
inline esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,
                                           const i2s_std_config_t *) {
  return ESP_OK;
}
inline esp_err_t i2s_channel_enable(i2s_chan_handle_t) { return ESP_OK; }
inline esp_err_t i2s_channel_disable(i2s_chan_handle_t) { return ESP_OK; }
inline esp_err_t i2s_del_channel(i2s_chan_handle_t) { return ESP_OK; }
inline esp_err_t i2s_channel_read(i2s_chan_handle_t, void *dest, size_t size,
                                  size_t *bytes_read, unsigned long) {
  if (dest) {
    std::memset(dest, 0, size);
  }
  if (bytes_read) {
    *bytes_read = size;
  }
  return ESP_OK;
}
