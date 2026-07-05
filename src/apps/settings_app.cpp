#include "apps/settings_app.h"
#include "sys/global.h"
#include "sys/sd_storage.h"
#include "ui/ui_helpers.h"
#include <SD_MMC.h>

namespace {

static const UiRect RESET_BUTTON = {10, 40, 180, 36};
static const UiRect DONE_BACK_BUTTON = {10, 160, 180, 30};
static const UiRect CONFIRM_BACKDROP = {10, 40, 180, 80};

static const char *kConfigPath = "/cfg/system.cfg";

} // namespace

void SettingsApp::reset() {
  confirmOpen = false;
  done = false;
}

void SettingsApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);

  gfx.setCursor(4, 8);
  gfx.print("Settings");

  if (done) {
    const char *msg = "Settings reset.";
    int16_t tx, ty;
    uint16_t tw, th;
    gfx.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
    gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(tw)) / 2 - tx, 80);
    gfx.print(msg);
  }

  uiDrawButton(gfx, RESET_BUTTON, "Reset Settings");

  if (confirmOpen) {
    for (int16_t y = CONFIRM_BACKDROP.y; y < CONFIRM_BACKDROP.y + CONFIRM_BACKDROP.h; y += 2) {
      for (int16_t x = CONFIRM_BACKDROP.x; x < CONFIRM_BACKDROP.x + CONFIRM_BACKDROP.w; x += 4) {
        if ((x + y) % 8 < 4) {
          gfx.drawPixel(x, y, 1);
        }
      }
    }
    gfx.fillRect(CONFIRM_BACKDROP.x, CONFIRM_BACKDROP.y,
                 CONFIRM_BACKDROP.w, CONFIRM_BACKDROP.h, 0);
    gfx.drawRect(CONFIRM_BACKDROP.x + 1, CONFIRM_BACKDROP.y + 1,
                 CONFIRM_BACKDROP.w - 2, CONFIRM_BACKDROP.h - 2, 1);
    gfx.drawRect(CONFIRM_BACKDROP.x, CONFIRM_BACKDROP.y,
                 CONFIRM_BACKDROP.w, CONFIRM_BACKDROP.h, 1);

    const char *msg = "Delete all settings?";
    int16_t tx, ty;
    uint16_t tw, th;
    gfx.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
    gfx.setCursor(CONFIRM_BACKDROP.x + (CONFIRM_BACKDROP.w - static_cast<int16_t>(tw)) / 2 - tx,
                  CONFIRM_BACKDROP.y + 24);
    gfx.print(msg);

    static const UiRect YES_BTN = {CONFIRM_BACKDROP.x + 10,
                                   CONFIRM_BACKDROP.y + CONFIRM_BACKDROP.h - 36,
                                   74, 24};
    static const UiRect NO_BTN = {CONFIRM_BACKDROP.x + CONFIRM_BACKDROP.w - 84,
                                  CONFIRM_BACKDROP.y + CONFIRM_BACKDROP.h - 36,
                                  74, 24};
    uiDrawButton(gfx, YES_BTN, "Yes");
    uiDrawButton(gfx, NO_BTN, "No");
  }
}

bool SettingsApp::handleTouch(const TouchPoint &point) {
  if (confirmOpen) {
    static const UiRect YES_BTN = {CONFIRM_BACKDROP.x + 10,
                                   CONFIRM_BACKDROP.y + CONFIRM_BACKDROP.h - 36,
                                   74, 24};
    static const UiRect NO_BTN = {CONFIRM_BACKDROP.x + CONFIRM_BACKDROP.w - 84,
                                  CONFIRM_BACKDROP.y + CONFIRM_BACKDROP.h - 36,
                                  74, 24};
    if (uiContains(YES_BTN, point)) {
      confirmOpen = false;
      resetSystemConfig();
      done = true;
      return true;
    }
    if (uiContains(NO_BTN, point) || !uiContains(CONFIRM_BACKDROP, point)) {
      confirmOpen = false;
      return true;
    }
    return true;
  }

  if (uiContains(RESET_BUTTON, point)) {
    confirmOpen = true;
    return true;
  }
  return true;
}

bool SettingsApp::hasActiveSession() const {
  return false;
}

void SettingsApp::resetSystemConfig() {
  if (sdStorageMounted()) {
    SD_MMC.remove(kConfigPath);
  }
}
