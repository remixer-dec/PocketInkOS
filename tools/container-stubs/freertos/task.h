#pragma once
#include <stdint.h>
using BaseType_t = int;
using UBaseType_t = unsigned int;
using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);
#define pdPASS 1
#define portMAX_DELAY 0xffffffffUL
inline void vTaskDelay(int) {}
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}
inline void vTaskSuspend(TaskHandle_t) {}
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle, BaseType_t) { if (handle) *handle = (TaskHandle_t)1; return pdPASS; }
