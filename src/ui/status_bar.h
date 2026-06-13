#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "sys/app_display.h"
#include "sys/battery_monitor.h"
#include <stdint.h>

struct StatusBarSnapshot {
  const char *dateText;
  const char *timeText;
  const BatterySnapshot *battery;
  char wifiIcon;
};

void drawStatusBar(AppDisplay &display, const StatusBarSnapshot &status);

#endif
