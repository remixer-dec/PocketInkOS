#pragma once
#include <stdint.h>
using BaseType_t = int;
using UBaseType_t = unsigned int;
using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);
#define pdPASS 1
inline void vTaskDelay(int) {}
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle, BaseType_t) { if (handle) *handle = (TaskHandle_t)1; return pdPASS; }
