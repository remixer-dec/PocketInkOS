#ifndef SHARED_I2C_BUS_H
#define SHARED_I2C_BUS_H

#include <driver/i2c_master.h>

/// Initializes the board I2C bus shared by touch, RTC, and sensors.
bool sharedI2cBegin();
/// Adds a device without racing traffic from another task.
esp_err_t sharedI2cAddDevice(const i2c_device_config_t *config,
                             i2c_master_dev_handle_t *device);
/// Runs one write on the shared bus and retries after clearing a stuck bus.
esp_err_t sharedI2cTransmit(i2c_master_dev_handle_t device,
                            const uint8_t *data, size_t size, int timeoutMs);
/// Runs one read on the shared bus and retries after clearing a stuck bus.
esp_err_t sharedI2cReceive(i2c_master_dev_handle_t device, uint8_t *data,
                           size_t size, int timeoutMs);
/// Runs one combined write/read and retries after clearing a stuck bus.
esp_err_t sharedI2cTransmitReceive(i2c_master_dev_handle_t device,
                                   const uint8_t *writeData, size_t writeSize,
                                   uint8_t *readData, size_t readSize,
                                   int timeoutMs);
/// Returns the initialized shared bus, or nullptr before successful setup.
i2c_master_bus_handle_t sharedI2cBusHandle();

#endif
