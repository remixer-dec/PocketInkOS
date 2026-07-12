#include "sys/drivers/shared_i2c_bus.h"

#include "sys/global.h"

namespace {

i2c_master_bus_handle_t sharedBus = nullptr;

} // namespace

bool sharedI2cBegin() {
  if (sharedBus != nullptr) {
    return true;
  }

  i2c_master_bus_config_t config = {};
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.i2c_port = I2C_NUM_0;
  config.scl_io_num = static_cast<gpio_num_t>(ESP32_I2C_SCL);
  config.sda_io_num = static_cast<gpio_num_t>(ESP32_I2C_SDA);
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&config, &sharedBus) == ESP_OK;
}

i2c_master_bus_handle_t sharedI2cBusHandle() { return sharedBus; }
