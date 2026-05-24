#pragma once
#include "Arduino.h"
#define GPIO_INTR_DISABLE 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { int intr_type; int mode; unsigned long long pin_bit_mask; int pull_down_en; int pull_up_en; } gpio_config_t;
inline esp_err_t gpio_config(const gpio_config_t *) { return ESP_OK; }
inline void gpio_set_level(gpio_num_t, int) {}
inline int gpio_get_level(gpio_num_t) { return 0; }
