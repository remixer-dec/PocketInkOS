#pragma once
#define ESP_LOGI(...)
#define ESP_LOGW(...)
#define ESP_LOGE(...)
#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) do { (void)(x); } while(0)
