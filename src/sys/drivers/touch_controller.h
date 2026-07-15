#ifndef TOUCH_CONTROLLER_H
#define TOUCH_CONTROLLER_H

#include <driver/i2c_master.h>
#include <stdint.h>

/// Raw sample reported by the FT6336 touch controller.
struct TouchControllerSample {
  uint8_t count = 0;
  uint16_t x = 0;
  uint16_t y = 0;
};

/// Low-level FT6336 access on the board's shared I2C bus.
class TouchController {
public:
  /// Resets and attaches the controller.
  bool begin();
  /// Holds the controller in reset after all reads have stopped.
  void end();
  /// Reads one raw controller sample.
  bool read(TouchControllerSample &sample);

private:
  i2c_master_dev_handle_t device = nullptr;
};

#endif
