#pragma once

#include <Arduino.h>

#ifndef BitOrder
// Adafruit BusIO still names the Arduino SPI bit-order type. ESP32 Arduino 3.x
// exposes the SPI bit-order values but no longer provides that public typedef.
#define BitOrder uint8_t
#endif
