#include "sys/usb_status.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
bool usb_is_enumerated_or_cdc_connected() __attribute__((weak));
#endif

bool usbDataConnected() {
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
  if (usb_is_enumerated_or_cdc_connected != nullptr) {
    return usb_is_enumerated_or_cdc_connected();
  }
#endif
  return false;
}
