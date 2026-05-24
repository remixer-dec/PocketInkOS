#include "keyboard_component.h"

static const int KEY_H = 30;
static const int KEY_GAP = 2;
static const int ROW1_KEY_W = 18;
static const int ROW2_KEY_W = 20;
static const int ROW3_KEY_W = 22;

static void drawKey(Adafruit_GFX &gfx, int x, int y, int w, int h, const char *label) {
  gfx.drawRect(x, y, w, h, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  int textX = x + (w - (int)strlen(label) * 6) / 2;
  int textY = y + (h - 8) / 2;
  gfx.setCursor(textX, textY);
  gfx.print(label);
}

void KeyboardComponent::drawInput(Adafruit_GFX &gfx, const String &text) {
  gfx.fillRect(4, 4, 192, 34, 0);
  gfx.drawRect(4, 4, 192, 34, 1);
  gfx.setTextSize(2);
  gfx.setTextColor(1);
  gfx.setCursor(10, 14);
  String shown = text;
  if (shown.length() > 15) {
    shown = shown.substring(shown.length() - 15);
  }
  gfx.print(shown);
}

void KeyboardComponent::draw(Adafruit_GFX &gfx, const String &text) {
  drawInput(gfx, text);

  const char *row1 = "QWERTYUIOP";
  const char *row2 = "ASDFGHJKL";
  const char *row3 = "ZXCVBNM";
  for (int i = 0; i < 10; i++) {
    char s[2] = {row1[i], 0};
    drawKey(gfx, 1 + i * (ROW1_KEY_W + KEY_GAP), 46, ROW1_KEY_W, KEY_H, s);
  }
  for (int i = 0; i < 9; i++) {
    char s[2] = {row2[i], 0};
    drawKey(gfx, 10 + i * (ROW2_KEY_W + KEY_GAP), 80, ROW2_KEY_W, KEY_H, s);
  }
  for (int i = 0; i < 7; i++) {
    char s[2] = {row3[i], 0};
    drawKey(gfx, 1 + i * (ROW3_KEY_W + KEY_GAP), 114, ROW3_KEY_W, KEY_H, s);
  }
  drawKey(gfx, 171, 114, 28, KEY_H, "<");
  drawKey(gfx, 1, 154, 46, 34, "CLR");
  drawKey(gfx, 53, 154, 92, 34, "SPACE");
  drawKey(gfx, 151, 154, 48, 34, "DEL");
}

KeyboardEvent KeyboardComponent::hitRow(const TouchPoint &point,
                                        const char *keys, int count, int x,
                                        int y, int keyW) const {
  if (point.y < y || point.y >= y + KEY_H) {
    return {KEY_NONE, 0};
  }
  for (int i = 0; i < count; i++) {
    int keyX = x + i * (keyW + KEY_GAP);
    if (point.x >= keyX && point.x < keyX + keyW) {
      return {KEY_CHAR, keys[i]};
    }
  }
  return {KEY_NONE, 0};
}

KeyboardEvent KeyboardComponent::hitTest(const TouchPoint &point) const {
  KeyboardEvent event = hitRow(point, "QWERTYUIOP", 10, 1, 46, ROW1_KEY_W);
  if (event.action != KEY_NONE)
    return event;
  event = hitRow(point, "ASDFGHJKL", 9, 10, 80, ROW2_KEY_W);
  if (event.action != KEY_NONE)
    return event;
  event = hitRow(point, "ZXCVBNM", 7, 1, 114, ROW3_KEY_W);
  if (event.action != KEY_NONE)
    return event;

  if (point.y >= 114 && point.y < 144 && point.x >= 171) {
    return {KEY_BACKSPACE, 0};
  }
  if (point.y >= 154 && point.y < 188) {
    if (point.x >= 1 && point.x < 47)
      return {KEY_CLEAR, 0};
    if (point.x >= 53 && point.x < 145)
      return {KEY_SPACE, ' '};
    if (point.x >= 151 && point.x < 199)
      return {KEY_BACKSPACE, 0};
  }
  return {KEY_NONE, 0};
}
