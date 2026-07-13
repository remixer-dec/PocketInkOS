#include "sys/services/system_stage_service.h"

#include "shell/shell_home_layout.h"
#include "sys/audio/audio_power.h"
#include "sys/services/sd_storage.h"
#include "sys/system_config.h"

void SystemStageService::initialize() {
  if (started) {
    return;
  }
  sdStorageBegin();
  configBegin();
  shellLayoutConfigReload();
  audioPowerBegin();
  started = true;
}
