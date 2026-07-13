#pragma once
enum esp_log_level_t {
  ESP_LOG_NONE,
  ESP_LOG_ERROR,
  ESP_LOG_WARN,
  ESP_LOG_INFO,
  ESP_LOG_DEBUG,
  ESP_LOG_VERBOSE,
};
inline esp_log_level_t esp_log_level_get(const char *) { return ESP_LOG_INFO; }
inline void esp_log_level_set(const char *, esp_log_level_t) {}
#define ESP_LOGI(...)
#define ESP_LOGW(...)
#define ESP_LOGE(...)
#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) do { (void)(x); } while(0)
