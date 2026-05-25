#include "t9_keyboard_component.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

static const unsigned long T9_WAIT_MS = 3000;
static const int INPUT_X = 4;
static const int INPUT_Y = 4;
static const int INPUT_W = 192;
static const int INPUT_H = 20;
static const int GRID_X = 4;
static const int GRID_Y = 28;
static const int KEY_W = 62;
static const int KEY_H = 40;
static const int KEY_GAP = 4;

static const char *lettersForKey(int key) {
  switch (key) {
  case 1:
    return ".,?!";
  case 2:
    return "ABC";
  case 3:
    return "DEF";
  case 4:
    return "GHI";
  case 5:
    return "JKL";
  case 6:
    return "MNO";
  case 7:
    return "PQRS";
  case 8:
    return "TUV";
  case 9:
    return "WXYZ";
  case 0:
    return " ";
  default:
    return "";
  }
}

void T9KeyboardComponent::drawInput(Adafruit_GFX &gfx, const String &text,
                                    int maxLength) {
  gfx.fillRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, 0);
  gfx.drawRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  gfx.setCursor(INPUT_X + 5, INPUT_Y + 6);
  String shown = text;
  if (shown.length() > 30) {
    shown = shown.substring(shown.length() - 30);
  }
  gfx.print(shown);
  if (maxLength > 0) {
    char label[10];
    snprintf(label, sizeof(label), "Max:%d", maxLength);
    gfx.setCursor(INPUT_X + INPUT_W - (int)strlen(label) * 6 - 4, INPUT_Y + 6);
    gfx.print(label);
  }
}

void T9KeyboardComponent::drawKey(Adafruit_GFX &gfx, int key, int x, int y,
                                  int w, int h, bool disabled) {
  const bool active = key == pendingKey && isPendingActive();
  if (active && !disabled) {
    gfx.fillRect(x, y, w, h, 1);
    gfx.setTextColor(0);
  } else {
    gfx.drawRect(x, y, w, h, 1);
    gfx.setTextColor(1);
  }
  if (disabled) {
    gfx.drawLine(x + 4, y + h - 5, x + w - 5, y + 4, 1);
  }

  char digit[2] = {(key == 10) ? '*' : (key == 11) ? '#' : char('0' + key),
                   0};
  gfx.setTextSize(2);
  gfx.setCursor(x + 8, y + 6);
  gfx.print(digit);

  const char *label = "";
  if (key >= 0 && key <= 9) {
    label = lettersForKey(key);
  } else if (key == 10) {
    label = "BACK";
  } else if (key == 11) {
    label = "OK";
  }
  gfx.setTextSize(1);
  gfx.setCursor(x + 8, y + 28);
  gfx.print(label);

  if (active && !disabled) {
    gfx.setTextSize(2);
    char selected[2] = {currentChar(), 0};
    gfx.setCursor(x + w - 24, y + 10);
    gfx.print(selected);
  }
  gfx.setTextColor(1);
}

void T9KeyboardComponent::draw(Adafruit_GFX &gfx, const String &text) {
  draw(gfx, text, 0);
}

void T9KeyboardComponent::draw(Adafruit_GFX &gfx, const String &text,
                               int maxLength) {
  drawInput(gfx, text, maxLength);

  const int keys[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11};
  bool maxReached = maxLength > 0 && (int)text.length() >= maxLength;
  bool pendingActive = isPendingActive();
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int x = GRID_X + col * (KEY_W + KEY_GAP);
      int y = GRID_Y + row * (KEY_H + KEY_GAP);
      int key = keys[row * 3 + col];
      bool disabled = maxReached && key != 10 && key != 11 &&
                      !(pendingActive && key == pendingKey);
      drawKey(gfx, key, x, y, KEY_W, KEY_H, disabled);
    }
  }
}

char T9KeyboardComponent::currentChar() const {
  if (pendingKey < 0) {
    return 0;
  }
  const char *letters = lettersForKey(pendingKey);
  const int len = strlen(letters);
  if (len == 0) {
    return 0;
  }
  return letters[pendingIndex % len];
}

void T9KeyboardComponent::clearPending() {
  pendingKey = -1;
  pendingIndex = 0;
  pendingAt = 0;
}

bool T9KeyboardComponent::isPendingActive() const {
  return pendingKey >= 0 && millis() - pendingAt < T9_WAIT_MS;
}

bool T9KeyboardComponent::expirePending() {
  if (pendingKey < 0 || isPendingActive()) {
    return false;
  }
  clearPending();
  return true;
}

bool T9KeyboardComponent::update() { return expirePending(); }

KeyboardEvent T9KeyboardComponent::hitTest(const TouchPoint &point,
                                           String &text) {
  return hitTest(point, text, 0);
}

KeyboardEvent T9KeyboardComponent::hitTest(const TouchPoint &point,
                                           String &text, int maxLength) {
  if (point.x < GRID_X || point.x >= GRID_X + 3 * KEY_W + 2 * KEY_GAP ||
      point.y < GRID_Y || point.y >= GRID_Y + 4 * KEY_H + 3 * KEY_GAP) {
    return {KEY_NONE, 0};
  }

  int col = (point.x - GRID_X) / (KEY_W + KEY_GAP);
  int row = (point.y - GRID_Y) / (KEY_H + KEY_GAP);
  int keyX = GRID_X + col * (KEY_W + KEY_GAP);
  int keyY = GRID_Y + row * (KEY_H + KEY_GAP);
  if (col < 0 || col > 2 || row < 0 || row > 3 ||
      point.x >= keyX + KEY_W || point.y >= keyY + KEY_H) {
    return {KEY_NONE, 0};
  }

  const int keys[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11};
  int key = keys[row * 3 + col];
  if (key == 10) {
    if (text.length() > 0) {
      text.remove(text.length() - 1);
    }
    clearPending();
    return {KEY_BACKSPACE, 0};
  }
  if (key == 11) {
    clearPending();
    return {KEY_OK, 0};
  }

  const bool sameKey = pendingKey == key && isPendingActive();
  int limit = maxLength > 0 ? maxLength : 64;
  if ((int)text.length() >= limit && !sameKey) {
    return {KEY_NONE, 0};
  }

  if (sameKey) {
    pendingIndex = (pendingIndex + 1) % strlen(lettersForKey(key));
    if (text.length() > 0) {
      text.remove(text.length() - 1);
    }
  } else {
    pendingKey = key;
    pendingIndex = 0;
  }

  pendingAt = millis();
  char value = currentChar();
  if (value != 0 && (int)text.length() < limit) {
    text += value;
  }
  return {KEY_CHAR, value};
}
