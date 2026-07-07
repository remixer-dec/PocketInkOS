#include "shell/shell_dialog_layout.h"
#include "shell/shell_home_layout.h"
#include "sys/global.h"
#include "ui/icon_ascii_font.h"
#include "ui/ui_helpers.h"
#include <stdio.h>

namespace {

static const UiRect QUIT_YES_BUTTON = {44, 112, 48, 24};
static const UiRect QUIT_NO_BUTTON = {108, 112, 48, 24};
static const UiRect POWER_PREV_PAGE_BUTTON = {14, 56, 54, 44};
static const UiRect POWER_NEXT_PAGE_BUTTON = {132, 56, 54, 44};
static const UiRect POWER_LEFT_BUTTON = {20, 100, 46, 44};
static const UiRect POWER_MIDDLE_BUTTON = {77, 100, 46, 44};
static const UiRect POWER_RIGHT_BUTTON = {134, 100, 46, 44};
static const int16_t DIALOG_BACKDROP_TOP_Y = 20;
static const int16_t DIALOG_HATCH_STEP = 6;

void drawCenteredText(AppDisplay &display, const char *text, int16_t y) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  display.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  display.setCursor((EPD_WIDTH - textW) / 2 - textX, y);
  display.print(text);
}

void drawDialogBackdropHatch(AppDisplay &display) {
  for (int16_t x = -EPD_HEIGHT; x < EPD_WIDTH; x += DIALOG_HATCH_STEP) {
    const int16_t diagonal = x + EPD_HEIGHT - 1;
    int16_t x0 = x;
    int16_t y0 = EPD_HEIGHT - 1;
    int16_t x1 = diagonal - DIALOG_BACKDROP_TOP_Y;
    int16_t y1 = DIALOG_BACKDROP_TOP_Y;
    if (x0 < 0) {
      y0 = diagonal;
      x0 = 0;
    }
    if (x1 >= EPD_WIDTH) {
      x1 = EPD_WIDTH - 1;
      y1 = diagonal - x1;
    }
    if (y0 >= DIALOG_BACKDROP_TOP_Y && y1 >= DIALOG_BACKDROP_TOP_Y &&
        y0 < EPD_HEIGHT && y1 < EPD_HEIGHT) {
      display.drawLine(x0, y0, x1, y1, 1);
    }
  }
}

void drawIconGlyph(AppDisplay &display, const UiRect &rect, char icon,
                   bool selected = false, int16_t yOffset = 0) {
  char text[2] = {icon, '\0'};
  display.setFont(&iconASCII12pt7b);
  display.setTextSize(1);
  display.setTextColor(selected ? 0 : 1);

  int16_t iconX;
  int16_t iconY;
  uint16_t iconW;
  uint16_t iconH;
  display.getTextBounds(text, 0, 0, &iconX, &iconY, &iconW, &iconH);
  display.setCursor(rect.x + (rect.w - static_cast<int16_t>(iconW)) / 2 -
                        iconX,
                    rect.y + (rect.h - static_cast<int16_t>(iconH)) / 2 -
                        iconY + 1 + yOffset);
  display.print(text);
  display.setFont();
  display.setTextSize(1);
  display.setTextColor(1);
}

void drawIconButton(AppDisplay &display, const UiRect &rect, char icon,
                    bool selected = false) {
  char iconText[2] = {icon, '\0'};
  TouchButtonContent content;
  content.icon = iconText;
  content.iconPlacement = TouchButtonIconPlacement::Only;
  TouchButton(rect, content).draw(display, selected);
}

void drawIconStateButton(AppDisplay &display, const UiRect &rect, char icon,
                         const char *state, bool selected = false) {
  char iconText[2] = {icon, '\0'};
  TouchButtonContent content;
  content.text = state;
  content.icon = iconText;
  content.iconPlacement = TouchButtonIconPlacement::AboveText;
  content.iconOffsetY = -1;
  content.textOffsetY = 1;
  TouchButton(rect, content).draw(display, selected);
}

void drawPowerDialogFrame(AppDisplay &display, const char *title) {
  display.fillRect(14, 56, 172, 94, 0);
  display.drawRect(14, 56, 172, 94, 1);
  display.drawLine(14, 150, 185, 150, 1);
  display.drawLine(14, 151, 185, 151, 1);
  display.drawLine(14, 152, 185, 152, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  drawCenteredText(display, title, 76);
  drawIconGlyph(display, POWER_PREV_PAGE_BUTTON, '<');
  drawIconGlyph(display, POWER_NEXT_PAGE_BUTTON, '>');
}

const char *powerPageTitle(PowerDialogPage page) {
  switch (page) {
  case PowerDialogPage::Power:
    return "Power";
  case PowerDialogPage::Device:
    return "Device";
  case PowerDialogPage::Volume:
    return "Volume";
  }
  return "Power";
}

void drawCenteredIcon(AppDisplay &display, char icon, int16_t y,
                      uint8_t textSize = 1) {
  char text[2] = {icon, '\0'};
  display.setFont(&iconASCII12pt7b);
  display.setTextSize(textSize);
  display.setTextColor(1);

  int16_t iconX;
  int16_t iconY;
  uint16_t iconW;
  uint16_t iconH;
  display.getTextBounds(text, 0, 0, &iconX, &iconY, &iconW, &iconH);
  display.setCursor((EPD_WIDTH - static_cast<int16_t>(iconW)) / 2 - iconX, y);
  display.print(text);
  display.setFont();
  display.setTextSize(1);
}

} // namespace

void drawQuitDialog(AppDisplay &display, const StatusBarSnapshot &status) {
  drawStatusBar(display, status);

  drawDialogBackdropHatch(display);

  display.fillRect(24, 62, 152, 86, 0);
  display.drawRect(24, 62, 152, 86, 1);
  display.drawLine(24, 148, 175, 148, 1);
  display.drawLine(24, 149, 175, 149, 1);
  display.drawLine(24, 150, 175, 150, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  drawCenteredText(display, "Do you really", 80);
  drawCenteredText(display, "want to quit?", 96);
  uiDrawButton(display, QUIT_YES_BUTTON, "YES");
  uiDrawButton(display, QUIT_NO_BUTTON, "NO");
}

bool quitDialogHitYes(const TouchPoint &point) {
  return uiContains(QUIT_YES_BUTTON, point);
}

bool quitDialogHitNo(const TouchPoint &point) {
  return uiContains(QUIT_NO_BUTTON, point);
}

void drawPowerDialog(AppDisplay &display, const StatusBarSnapshot &status,
                     const PowerDialogSnapshot &snapshot,
                     PowerDialogAction pressed) {
  drawStatusBar(display, status);
  drawDialogBackdropHatch(display);

  drawPowerDialogFrame(display, powerPageTitle(snapshot.page));
  if (snapshot.page == PowerDialogPage::Power) {
    drawIconButton(display, POWER_LEFT_BUTTON, 'Z',
                   pressed == PowerDialogAction::Reboot);
    drawIconButton(display, POWER_MIDDLE_BUTTON, 'j',
                   pressed == PowerDialogAction::PowerOff);
    drawIconButton(display, POWER_RIGHT_BUTTON, 'S',
                   pressed == PowerDialogAction::DeepSleep);
    return;
  }

  if (snapshot.page == PowerDialogPage::Device) {
    const char wifiIcon = snapshot.wifiConnected ? 'g' : 'f';
    const char *wifiState =
        snapshot.wifiConnected ? "OK" : (snapshot.wifiOn ? "ON" : "OFF");
    drawIconStateButton(display, POWER_LEFT_BUTTON, wifiIcon, wifiState,
                        pressed == PowerDialogAction::WifiToggle);
    drawIconStateButton(display, POWER_MIDDLE_BUTTON, '0',
                        snapshot.bluetoothOn ? "ON" : "OFF",
                        pressed == PowerDialogAction::BluetoothToggle);
    char cpuText[8];
    snprintf(cpuText, sizeof(cpuText), "%u",
             static_cast<unsigned>(snapshot.cpuMhz));
    drawIconStateButton(display, POWER_RIGHT_BUTTON, ':', cpuText,
                        pressed == PowerDialogAction::CpuCycle);
    return;
  }

  char volumeText[12];
  snprintf(volumeText, sizeof(volumeText), "%u%%",
           static_cast<unsigned>(snapshot.volume));
  drawCenteredText(display, snapshot.muted ? "MUTED" : volumeText, 90);
  drawIconButton(display, POWER_LEFT_BUTTON, 'b',
                 pressed == PowerDialogAction::VolumeDown);
  drawIconButton(display, POWER_MIDDLE_BUTTON, snapshot.muted ? '"' : '!',
                 pressed == PowerDialogAction::VolumeMute);
  drawIconButton(display, POWER_RIGHT_BUTTON, 'X',
                 pressed == PowerDialogAction::VolumeUp);
}

PowerDialogAction powerDialogHitAction(const TouchPoint &point,
                                       PowerDialogPage page) {
  if (uiContains(POWER_PREV_PAGE_BUTTON, point)) {
    return PowerDialogAction::PreviousPage;
  }
  if (uiContains(POWER_NEXT_PAGE_BUTTON, point)) {
    return PowerDialogAction::NextPage;
  }

  if (page == PowerDialogPage::Power && uiContains(POWER_LEFT_BUTTON, point)) {
    return PowerDialogAction::Reboot;
  }
  if (page == PowerDialogPage::Power && uiContains(POWER_MIDDLE_BUTTON, point)) {
    return PowerDialogAction::PowerOff;
  }
  if (page == PowerDialogPage::Power && uiContains(POWER_RIGHT_BUTTON, point)) {
    return PowerDialogAction::DeepSleep;
  }
  if (page == PowerDialogPage::Device && uiContains(POWER_LEFT_BUTTON, point)) {
    return PowerDialogAction::WifiToggle;
  }
  if (page == PowerDialogPage::Device && uiContains(POWER_MIDDLE_BUTTON, point)) {
    return PowerDialogAction::BluetoothToggle;
  }
  if (page == PowerDialogPage::Device && uiContains(POWER_RIGHT_BUTTON, point)) {
    return PowerDialogAction::CpuCycle;
  }
  if (page == PowerDialogPage::Volume && uiContains(POWER_LEFT_BUTTON, point)) {
    return PowerDialogAction::VolumeDown;
  }
  if (page == PowerDialogPage::Volume && uiContains(POWER_MIDDLE_BUTTON, point)) {
    return PowerDialogAction::VolumeMute;
  }
  if (page == PowerDialogPage::Volume && uiContains(POWER_RIGHT_BUTTON, point)) {
    return PowerDialogAction::VolumeUp;
  }
  return PowerDialogAction::None;
}

void drawPowerOffScreen(AppDisplay &display) {
  display.fillScreen(0);
}

void drawLowBatteryScreen(AppDisplay &display) {
  display.fillScreen(0);
  drawCenteredIcon(display, 'E', 114, 2);
}

void drawDeepSleepScreen(AppDisplay &display) {
  display.fillScreen(0);
  drawCenteredIcon(display, shellLayoutConfig().lockscreenTopIcon, 114, 2);
}

void drawDeepSleepClockScreen(AppDisplay &display, const char *timeText,
                              const char *dateText) {
  const ShellLayoutConfig &config = shellLayoutConfig();
  display.fillScreen(0);
  if (config.lockscreenTopIcon != '\0') {
    drawCenteredIcon(display, config.lockscreenTopIcon, 42, 1);
  }

  display.setFont();
  display.setTextColor(1);
  display.setTextSize(4);
  drawCenteredText(display, timeText, 104);

  if (config.lockscreenDate) {
    display.setTextSize(1);
    drawCenteredText(display, dateText, 140);
  }
  display.setTextSize(1);
}
