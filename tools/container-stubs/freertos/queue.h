#pragma once
#include "freertos/task.h"

using QueueHandle_t = void *;

inline QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t) {
  return (QueueHandle_t)1;
}

inline BaseType_t xQueueReset(QueueHandle_t) { return pdPASS; }
inline BaseType_t xQueueSend(QueueHandle_t, const void *, uint32_t) {
  return pdPASS;
}
inline BaseType_t xQueueReceive(QueueHandle_t, void *, uint32_t) {
  return 0;
}
