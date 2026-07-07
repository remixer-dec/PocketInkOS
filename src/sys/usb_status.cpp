#include "sys/usb_status.h"

#if __has_include(<HWCDC.h>)
#include <HWCDC.h>
#define POCKETINK_HAS_HWCDC 1
#else
#define POCKETINK_HAS_HWCDC 0
#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
bool usb_is_enumerated_or_cdc_connected() __attribute__((weak));
#endif

bool usbConnected() {
#if POCKETINK_HAS_HWCDC
  // isPlugged() tracks the USB Serial/JTAG host connection and does not require
  // a serial monitor to have DTR/CDC open.
  if (HWCDC::isPlugged()) {
    return true;
  }
  if (HWCDC::isConnected()) {
    return true;
  }
#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
  if (usb_is_enumerated_or_cdc_connected != nullptr) {
    return usb_is_enumerated_or_cdc_connected();
  }
#endif
  return false;
}
