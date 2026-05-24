#include "keyboard_component.h"

static const int KEY_W = 18;
static const int KEY_H = 22;
static const int KEY_GAP = 2;

static void drawKey(Adafruit_GFX &gfx, int x, int y, int w, int h,
                    const char *label) {
  gfx.drawRect(x, y, w, h, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  int textX = x + (w - (int)strlen(label) * 6) / 2;
  gfx.setCursor(textX, y + 7);
  gfx.print(label);
}

void KeyboardComponent::draw(Adafruit_GFX &gfx, const String &text) {
  gfx.drawRect(4, 4, 192, 34, 1);
  gfx.setTextSize(2);
  gfx.setTextColor(1);
  gfx.setCursor(10, 14);
  String shown = text;
  if (shown.length() > 15) {
    shown = shown.substring(shown.length() - 15);
  }
  gfx.print(shown);

  const char *row1 = "QWERTYUIOP";
  const char *row2 = "ASDFGHJKL";
  const char *row3 = "ZXCVBNM";
  for (int i = 0; i < 10; i++) {
    char s[2] = {row1[i], 0};
    drawKey(gfx, 1 + i * (KEY_W + KEY_GAP), 52, KEY_W, KEY_H, s);
  }
  for (int i = 0; i < 9; i++) {
    char s[2] = {row2[i], 0};
    drawKey(gfx, 11 + i * (KEY_W + KEY_GAP), 80, KEY_W, KEY_H, s);
  }
  for (int i = 0; i < 7; i++) {
    char s[2] = {row3[i], 0};
    drawKey(gfx, 31 + i * (KEY_W + KEY_GAP), 108, KEY_W, KEY_H, s);
  }
  drawKey(gfx, 171, 108, 27, KEY_H, "<");
  drawKey(gfx, 20, 142, 42, 28, "CLR");
  drawKey(gfx, 70, 142, 60, 28, "SPACE");
  drawKey(gfx, 138, 142, 42, 28, "DEL");

  gfx.setTextSize(1);
  gfx.setCursor(27, 186);
  gfx.print("BOOT: apps   PWR: home");
}

KeyboardEvent KeyboardComponent::hitRow(const TouchPoint &point,
                                        const char *keys, int count, int x,
                                        int y) const {
  if (point.y < y || point.y >= y + KEY_H) {
    return {KEY_NONE, 0};
  }
  for (int i = 0; i < count; i++) {
    int keyX = x + i * (KEY_W + KEY_GAP);
    if (point.x >= keyX && point.x < keyX + KEY_W) {
      return {KEY_CHAR, keys[i]};
    }
  }
  return {KEY_NONE, 0};
}

KeyboardEvent KeyboardComponent::hitTest(const TouchPoint &point) const {
  KeyboardEvent event = hitRow(point, "QWERTYUIOP", 10, 1, 52);
  if (event.action != KEY_NONE) return event;
  event = hitRow(point, "ASDFGHJKL", 9, 11, 80);
  if (event.action != KEY_NONE) return event;
  event = hitRow(point, "ZXCVBNM", 7, 31, 108);
  if (event.action != KEY_NONE) return event;

  if (point.y >= 108 && point.y < 130 && point.x >= 171) {
    return {KEY_BACKSPACE, 0};
  }
  if (point.y >= 142 && point.y < 170) {
    if (point.x >= 20 && point.x < 62) return {KEY_CLEAR, 0};
    if (point.x >= 70 && point.x < 130) return {KEY_SPACE, ' '};
    if (point.x >= 138 && point.x < 180) return {KEY_BACKSPACE, 0};
  }
  return {KEY_NONE, 0};
}
