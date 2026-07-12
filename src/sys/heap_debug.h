/// Lightweight diagnostic checkpoint for printing internal-heap fragmentation,
/// allocation watermarks, and PSRAM capacity to both Arduino and ROM serial
/// outputs. It is intended for development measurements, not memory ownership.
#pragma once

#include <Arduino.h>

/// Prints a tagged snapshot of internal heap and PSRAM health to serial output.
void heapMark(const char *tag);
