#include "keyboard_component.h"

#include <cstdio>
#include <cstring>

static const int KEY_H = 30;
static const int KEY_GAP = 2;
static const int ROW1_KEY_W = 18;
static const int ROW2_KEY_W = 20;
static const int ROW3_KEY_W = 22;

static void drawKey(Adafruit_GFX &gfx, int x, int y, int w, int h,
                    const char *label) {
  gfx.drawRect(x, y, w, h, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  int textX = x + (w - (int)strlen(label) * 6) / 2;
  int textY = y + (h - 8) / 2;
  gfx.setCursor(textX, textY);
  gfx.print(label);
}

void KeyboardComponent::drawInput(Adafruit_GFX &gfx, const String &text) {
  drawInput(gfx, text, 0);
}

void KeyboardComponent::drawInput(Adafruit_GFX &gfx, const String &text,
                                  int maxLength) {
  gfx.fillRect(4, 4, 192, 34, 0);
  gfx.drawRect(4, 4, 192, 34, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  gfx.setCursor(10, 17);
  String shown = text;
  if (shown.length() > 28) {
    shown = shown.substring(shown.length() - 28);
  }
  gfx.print(shown);
  if (maxLength > 0) {
    char label[10];
    snprintf(label, sizeof(label), "Max:%d", maxLength);
    gfx.setCursor(196 - (int)strlen(label) * 6 - 4, 8);
    gfx.print(label);
  }
}

void KeyboardComponent::draw(Adafruit_GFX &gfx, const String &text) {
  draw(gfx, text, 0);
}

void KeyboardComponent::draw(Adafruit_GFX &gfx, const String &text,
                             int maxLength) {
  inputLimit = maxLength > 0 && (int)text.length() >= maxLength;
  drawInput(gfx, text, maxLength);

  const char *row1 = caps ? "QWERTYUIOP" : "qwertyuiop";
  const char *row2 = caps ? "ASDFGHJKL" : "asdfghjkl";
  const char *row3 = caps ? "ZXCVBNM" : "zxcvbnm";
  for (int i = 0; i < 10; i++) {
    char s[2] = {row1[i], 0};
    int x = 1 + i * (ROW1_KEY_W + KEY_GAP);
    drawKey(gfx, x, 46, ROW1_KEY_W, KEY_H, s);
    if (inputLimit) gfx.drawLine(x + 3, 46 + KEY_H - 4, x + ROW1_KEY_W - 4, 49, 1);
  }
  for (int i = 0; i < 9; i++) {
    char s[2] = {row2[i], 0};
    int x = 4 + i * (ROW2_KEY_W + KEY_GAP);
    drawKey(gfx, x, 80, ROW2_KEY_W, KEY_H, s);
    if (inputLimit) gfx.drawLine(x + 3, 80 + KEY_H - 4, x + ROW2_KEY_W - 4, 83, 1);
  }
  for (int i = 0; i < 7; i++) {
    char s[2] = {row3[i], 0};
    int x = 1 + i * (ROW3_KEY_W + KEY_GAP);
    drawKey(gfx, x, 114, ROW3_KEY_W, KEY_H, s);
    if (inputLimit) gfx.drawLine(x + 3, 114 + KEY_H - 4, x + ROW3_KEY_W - 4, 117, 1);
  }
  drawKey(gfx, 171, 114, 28, KEY_H, "<");
  drawKey(gfx, 1, 154, 46, 34, caps ? "CAPS" : "caps");
  drawKey(gfx, 53, 154, 92, 34, "SPACE");
  if (inputLimit) gfx.drawLine(57, 184, 141, 158, 1);
  drawKey(gfx, 151, 154, 48, 34, "OK");
  inputLimit = 0;
}

void KeyboardComponent::toggleCaps() { caps = !caps; }

KeyboardEvent KeyboardComponent::hitRow(const TouchPoint &point,
                                        const char *keys, int count, int x,
                                        int y, int keyW) const {
  if (inputLimit) {
    return {KEY_NONE, 0};
  }
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

KeyboardEvent KeyboardComponent::hitTest(const TouchPoint &point) {
  return hitTest(point, 0, 0);
}

KeyboardEvent KeyboardComponent::hitTest(const TouchPoint &point,
                                         int currentLength, int maxLength) {
  inputLimit = maxLength > 0 && currentLength >= maxLength;
  KeyboardEvent event = hitRow(point, "QWERTYUIOP", 10, 1, 46, ROW1_KEY_W);
  if (event.action != KEY_NONE) {
    if (!caps) event.value = event.value - 'A' + 'a';
    inputLimit = 0;
    return event;
  }
  event = hitRow(point, "ASDFGHJKL", 9, 4, 80, ROW2_KEY_W);
  if (event.action != KEY_NONE) {
    if (!caps) event.value = event.value - 'A' + 'a';
    inputLimit = 0;
    return event;
  }
  event = hitRow(point, "ZXCVBNM", 7, 1, 114, ROW3_KEY_W);
  if (event.action != KEY_NONE) {
    if (!caps) event.value = event.value - 'A' + 'a';
    inputLimit = 0;
    return event;
  }

  if (point.y >= 114 && point.y < 144 && point.x >= 171) {
    inputLimit = 0;
    return {KEY_BACKSPACE, 0};
  }
  if (point.y >= 154 && point.y < 188) {
    if (point.x >= 1 && point.x < 47) {
      caps = !caps;
      inputLimit = 0;
      return {KEY_SHIFT, 0};
    }
    if (point.x >= 53 && point.x < 145) {
      if (inputLimit) {
        inputLimit = 0;
        return {KEY_NONE, 0};
      }
      inputLimit = 0;
      return {KEY_SPACE, ' '};
    }
    if (point.x >= 151 && point.x < 199) {
      inputLimit = 0;
      return {KEY_OK, 0};
    }
  }
  inputLimit = 0;
  return {KEY_NONE, 0};
}
