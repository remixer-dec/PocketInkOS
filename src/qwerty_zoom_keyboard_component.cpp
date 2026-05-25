#include "qwerty_zoom_keyboard_component.h"

#include <cstdio>
#include <cstring>

static const char *QWERTY_ZOOM_ROWS[] = {"QWERTYUIOP", "ASDFGHJKL",
                                         "ZXCVBNM"};
static const int QWERTY_ZOOM_ROW_COUNT = 3;
static const int QWERTY_ZOOM_VISIBLE_COLUMNS = 3;
static const int QWERTY_ZOOM_STEP = 3;
static const int QWERTY_ZOOM_MAX_COLUMN = 6;

static const int INPUT_X = 4;
static const int INPUT_Y = 4;
static const int INPUT_W = 192;
static const int INPUT_H = 20;
static const int GRID_X = 4;
static const int GRID_Y = 28;
static const int KEY_W = 62;
static const int KEY_H = 34;
static const int KEY_GAP = 4;
static const int CTRL_Y = 142;
static const int CTRL_H = 26;
static const int NAV_Y = 170;
static const int NAV_H = 26;

static void drawZoomKey(Adafruit_GFX &gfx, int x, int y, int w, int h,
                        const char *label, int textSize = 1) {
  gfx.drawRect(x, y, w, h, 1);
  gfx.setTextSize(textSize);
  gfx.setTextColor(1);
  int charW = 6 * textSize;
  int charH = 8 * textSize;
  int textX = x + (w - (int)strlen(label) * charW) / 2;
  int textY = y + (h - charH) / 2;
  gfx.setCursor(textX, textY);
  gfx.print(label);
}

void QwertyZoomKeyboardComponent::drawInput(Adafruit_GFX &gfx,
                                            const String &text,
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

void QwertyZoomKeyboardComponent::draw(Adafruit_GFX &gfx,
                                       const String &text) {
  draw(gfx, text, 0);
}

void QwertyZoomKeyboardComponent::draw(Adafruit_GFX &gfx, const String &text,
                                       int maxLength) {
  bool maxReached = maxLength > 0 && (int)text.length() >= maxLength;
  drawInput(gfx, text, maxLength);

  for (int row = 0; row < QWERTY_ZOOM_ROW_COUNT; row++) {
    for (int col = 0; col < QWERTY_ZOOM_VISIBLE_COLUMNS; col++) {
      char value = keyAt(row, col);
      if (value == 0) {
        continue;
      }
      if (!caps) {
        value = value - 'A' + 'a';
      }
      char label[2] = {value, 0};
      int x = GRID_X + col * (KEY_W + KEY_GAP);
      int y = GRID_Y + row * (KEY_H + KEY_GAP);
      drawZoomKey(gfx, x, y, KEY_W, KEY_H, label, 2);
      if (maxReached) {
        gfx.drawLine(x + 5, y + KEY_H - 6, x + KEY_W - 6, y + 5, 1);
      }
    }
  }

  drawZoomKey(gfx, 4, CTRL_Y, 62, CTRL_H, caps ? "CAPS" : "caps");
  drawZoomKey(gfx, 70, CTRL_Y, 62, CTRL_H, "<");
  drawZoomKey(gfx, 136, CTRL_Y, 60, CTRL_H, "OK");

  if (pageColumn > 0) {
    drawZoomKey(gfx, 4, NAV_Y, 62, NAV_H, "<-");
  }
  drawZoomKey(gfx, 70, NAV_Y, 62, NAV_H, "SPACE");
  if (pageColumn < QWERTY_ZOOM_MAX_COLUMN) {
    drawZoomKey(gfx, 136, NAV_Y, 60, NAV_H, "->");
  }
}

void QwertyZoomKeyboardComponent::movePage(int delta) {
  pageColumn += delta;
  if (pageColumn < 0) {
    pageColumn = 0;
  }
  if (pageColumn > QWERTY_ZOOM_MAX_COLUMN) {
    pageColumn = QWERTY_ZOOM_MAX_COLUMN;
  }
}

void QwertyZoomKeyboardComponent::toggleCaps() { caps = !caps; }

char QwertyZoomKeyboardComponent::keyAt(int row, int col) const {
  int keyColumn = pageColumn + col;
  const char *keys = QWERTY_ZOOM_ROWS[row];
  int count = strlen(keys);
  if (keyColumn >= count) {
    return 0;
  }
  return keys[keyColumn];
}

KeyboardEvent QwertyZoomKeyboardComponent::hitTest(const TouchPoint &point) {
  return hitTest(point, 0, 0);
}

KeyboardEvent QwertyZoomKeyboardComponent::hitTest(const TouchPoint &point,
                                                   int currentLength,
                                                   int maxLength) {
  inputDisabled = maxLength > 0 && currentLength >= maxLength;
  if (point.x >= GRID_X && point.x < GRID_X + 3 * KEY_W + 2 * KEY_GAP &&
      point.y >= GRID_Y && point.y < GRID_Y + 3 * KEY_H + 2 * KEY_GAP) {
    if (inputDisabled) {
      inputDisabled = false;
      return {KEY_NONE, 0};
    }
    int col = (point.x - GRID_X) / (KEY_W + KEY_GAP);
    int row = (point.y - GRID_Y) / (KEY_H + KEY_GAP);
    int keyX = GRID_X + col * (KEY_W + KEY_GAP);
    int keyY = GRID_Y + row * (KEY_H + KEY_GAP);
    if (col >= 0 && col < 3 && row >= 0 && row < 3 &&
        point.x < keyX + KEY_W && point.y < keyY + KEY_H) {
      char value = keyAt(row, col);
      if (value == 0) {
        return {KEY_NONE, 0};
      }
      if (!caps) {
        value = value - 'A' + 'a';
      }
      inputDisabled = false;
      return {KEY_CHAR, value};
    }
  }

  if (point.y >= CTRL_Y && point.y < CTRL_Y + CTRL_H) {
    if (point.x >= 4 && point.x < 66) {
      caps = !caps;
      inputDisabled = false;
      return {KEY_SHIFT, 0};
    }
    if (point.x >= 70 && point.x < 132) {
      inputDisabled = false;
      return {KEY_BACKSPACE, 0};
    }
    if (point.x >= 136 && point.x < 196) {
      inputDisabled = false;
      return {KEY_OK, 0};
    }
  }

  if (point.y >= NAV_Y && point.y < NAV_Y + NAV_H) {
    if (point.x >= 4 && point.x < 66 && pageColumn > 0) {
      movePage(-QWERTY_ZOOM_STEP);
      inputDisabled = false;
      return {KEY_NAV, 0};
    }
    if (point.x >= 70 && point.x < 132) {
      if (inputDisabled) {
        inputDisabled = false;
        return {KEY_NONE, 0};
      }
      inputDisabled = false;
      return {KEY_SPACE, ' '};
    }
    if (point.x >= 136 && point.x < 196 &&
        pageColumn < QWERTY_ZOOM_MAX_COLUMN) {
      movePage(QWERTY_ZOOM_STEP);
      inputDisabled = false;
      return {KEY_NAV, 0};
    }
  }

  inputDisabled = false;
  return {KEY_NONE, 0};
}
