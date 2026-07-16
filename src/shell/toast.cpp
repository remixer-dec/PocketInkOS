#include "shell/toast.h"

#include "sys/app_display.h"
#include "sys/global.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cstring>

namespace {

constexpr UiRect kToastRect = {24, 162, 152, 24};

struct ToastState {
  uint32_t shownAt = 0;
  uint32_t durationMs = 0;
  char message[24] = {};
  bool visible = false;
  bool changed = false;
};

ToastState toast;

void clearToast() {
  toast.message[0] = '\0';
  toast.shownAt = 0;
  toast.durationMs = 0;
  toast.visible = false;
  toast.changed = true;
}

void drawCenteredText(AppDisplay &display, const char *text, int16_t y) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  display.setCursor((EPD_WIDTH - static_cast<int16_t>(textW)) / 2 - textX,
                    y);
  display.print(text);
}

} // namespace

void showShellToast(const char *message, uint32_t durationMs) {
  if (message == nullptr || message[0] == '\0' || durationMs == 0) {
    clearToast();
    return;
  }
  strncpy(toast.message, message, sizeof(toast.message) - 1);
  toast.message[sizeof(toast.message) - 1] = '\0';
  toast.shownAt = millis();
  toast.durationMs = durationMs;
  toast.visible = true;
  toast.changed = true;
}

bool updateShellToast() {
  if (toast.visible && millis() - toast.shownAt >= toast.durationMs) {
    clearToast();
  }
  const bool changed = toast.changed;
  toast.changed = false;
  return changed;
}

void drawShellToast(AppDisplay &display) {
  if (!toast.visible) {
    return;
  }
  display.fillRect(kToastRect.x, kToastRect.y, kToastRect.w, kToastRect.h, 0);
  display.drawRect(kToastRect.x, kToastRect.y, kToastRect.w, kToastRect.h, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  drawCenteredText(display, toast.message, kToastRect.y + 7);
}
