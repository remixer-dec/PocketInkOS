#include "sys/debug_logging.h"

#include <esp_log.h>

namespace {

bool enabled = true;
esp_log_level_t previousEspLogLevel = ESP_LOG_INFO;

} // namespace

void debugLoggingSetEnabled(bool nextEnabled) {
  if (enabled == nextEnabled) {
    return;
  }
  if (!nextEnabled) {
    previousEspLogLevel = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);
  } else {
    esp_log_level_set("*", previousEspLogLevel);
  }
  enabled = nextEnabled;
}

bool debugLoggingEnabled() { return enabled; }
