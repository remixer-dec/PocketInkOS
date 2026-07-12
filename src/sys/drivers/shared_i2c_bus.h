#ifndef SHARED_I2C_BUS_H
#define SHARED_I2C_BUS_H

#include <driver/i2c_master.h>

/// Initializes the board I2C bus shared by touch, RTC, and sensors.
bool sharedI2cBegin();
/// Returns the initialized shared bus, or nullptr before successful setup.
i2c_master_bus_handle_t sharedI2cBusHandle();

#endif
