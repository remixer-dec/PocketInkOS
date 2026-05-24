#pragma once
#include "Arduino.h"
#ifndef ESP_OK
using esp_err_t = int;
#define ESP_OK 0
#endif
#define ESP_ERR_INVALID_STATE 259
#define I2C_NUM_0 0
#define I2C_NUM_1 1
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef int i2c_port_t;
struct i2c_master_bus_config_t {
  int clk_source;
  i2c_port_t i2c_port;
  gpio_num_t scl_io_num;
  gpio_num_t sda_io_num;
  int glitch_ignore_cnt;
  struct {
    bool enable_internal_pullup;
  } flags;
};
struct i2c_device_config_t {
  int dev_addr_length;
  int device_address;
  int scl_speed_hz;
};
inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *,
                                    i2c_master_bus_handle_t *bus) {
  if (bus) *bus = reinterpret_cast<void *>(1);
  return ESP_OK;
}
inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,
                                           const i2c_device_config_t *,
                                           i2c_master_dev_handle_t *dev) {
  if (dev) *dev = reinterpret_cast<void *>(1);
  return ESP_OK;
}
inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,
                                             const uint8_t *, size_t,
                                             uint8_t *, size_t, int) {
  return ESP_OK;
}
