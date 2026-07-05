#include "ui/components/t9_keyboard_component.h"

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
static const int VISIBLE_INPUT_CHARS = 30;
static const int DEFAULT_TEXT_LIMIT = 64;
static const int T9_KEYS[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11};

static const char *sequenceForKey(int key) {
  switch (key) {
  case 1:
    return ".,?!/-_1";
  case 2:
    return "abc2";
  case 3:
    return "def3";
  case 4:
    return "ghi4";
  case 5:
    return "jkl5";
  case 6:
    return "mno6";
  case 7:
    return "pqrs7";
  case 8:
    return "tuv8";
  case 9:
    return "wxyz9";
  case 0:
    return " _+0";
  default:
    return "";
  }
}

static char applyCaps(char value, bool caps) {
  if (value >= 'a' && value <= 'z' && caps) {
    return value - 'a' + 'A';
  }
  if (value >= 'A' && value <= 'Z' && !caps) {
    return value - 'A' + 'a';
  }
  return value;
}

static void buildLabel(char *out, size_t outSize, int key, bool caps) {
  const char *sequence = sequenceForKey(key);
  size_t write = 0;
  for (size_t read = 0; sequence[read] != '\0' && write + 1 < outSize;
       read++) {
    if (sequence[read] >= '0' && sequence[read] <= '9') {
      continue;
    }
    out[write++] = applyCaps(sequence[read], caps);
  }
  out[write] = '\0';
}

static void buildNumberLabel(char *out, size_t outSize, int key) {
  if (outSize == 0) {
    return;
  }
  if (key >= 0 && key <= 9) {
    snprintf(out, outSize, "%d", key);
  } else if (key == 10) {
    strncpy(out, "BACK", outSize - 1);
    out[outSize - 1] = '\0';
  } else if (key == 11) {
    strncpy(out, "OK", outSize - 1);
    out[outSize - 1] = '\0';
  } else {
    out[0] = '\0';
  }
}

static void drawMaxLengthLabel(Adafruit_GFX &gfx, int maxLength) {
  if (maxLength <= 0) {
    return;
  }

  char label[10];
  snprintf(label, sizeof(label), "Max:%d", maxLength);
  gfx.setCursor(INPUT_X + INPUT_W - (int)strlen(label) * 6 - 4, INPUT_Y + 6);
  gfx.print(label);
}

void T9KeyboardComponent::drawInput(Adafruit_GFX &gfx, const char *text,
                                    int maxLength) {
  gfx.fillRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, 0);
  gfx.drawRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  const char *shown = text != nullptr ? text : "";
  const size_t length = strlen(shown);
  if (length > VISIBLE_INPUT_CHARS) {
    shown += length - VISIBLE_INPUT_CHARS;
  }
  gfx.setCursor(INPUT_X + 5, INPUT_Y + 6);
  gfx.print(shown);
  drawMaxLengthLabel(gfx, maxLength);
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

  char label[12] = "";
  if (layout == Layout::Numbers) {
    buildNumberLabel(label, sizeof(label), key);
  } else if (key >= 0 && key <= 9) {
    buildLabel(label, sizeof(label), key, caps);
  } else if (key == 10) {
    strncpy(label, "BACK", sizeof(label) - 1);
  } else if (key == 11) {
    strncpy(label, "OK", sizeof(label) - 1);
  }
  gfx.setTextSize(1);
  gfx.setCursor(x + 8, y + 28);
  gfx.print(label);

  if (active && !disabled && layout == Layout::Text) {
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
  drawInput(gfx, text.c_str(), maxLength);
  drawKeyboard(gfx, static_cast<int>(text.length()), maxLength);
}

void T9KeyboardComponent::draw(Adafruit_GFX &gfx, const char *text,
                               int maxLength) {
  drawInput(gfx, text, maxLength);
  drawKeyboard(gfx, text != nullptr ? static_cast<int>(strlen(text)) : 0,
               maxLength);
}

void T9KeyboardComponent::drawKeyboard(Adafruit_GFX &gfx, int currentLength,
                                       int maxLength) {
  const bool maxReached = maxLength > 0 && currentLength >= maxLength;
  const bool pendingActive = isPendingActive();
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      const int x = GRID_X + col * (KEY_W + KEY_GAP);
      const int y = GRID_Y + row * (KEY_H + KEY_GAP);
      const int key = T9_KEYS[row * 3 + col];
      const bool disabled = maxReached && key != 10 && key != 11 &&
                            !(pendingActive && key == pendingKey);
      drawKey(gfx, key, x, y, KEY_W, KEY_H, disabled);
    }
  }
}

void T9KeyboardComponent::setLayout(Layout nextLayout) {
  if (layout != nextLayout) {
    clearPending();
  }
  layout = nextLayout;
}

char T9KeyboardComponent::currentChar() const {
  if (pendingKey < 0) {
    return 0;
  }
  const char *sequence = sequenceForKey(pendingKey);
  const int len = strlen(sequence);
  if (len == 0) {
    return 0;
  }
  return applyCaps(sequence[pendingIndex % len], caps);
}

void T9KeyboardComponent::clearPending() {
  pendingKey = -1;
  pendingIndex = 0;
  pendingAt = 0;
}

void T9KeyboardComponent::toggleCaps() { caps = !caps; }

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
  int key = 0;
  if (!keyAt(point, key)) {
    return {KEY_NONE, 0};
  }

  TextBuffer buffer = stringBuffer(text, maxLength);
  return handleKey(key, buffer);
}

KeyboardEvent T9KeyboardComponent::hitTest(const TouchPoint &point, char *text,
                                           int capacity) {
  if (text == nullptr || capacity <= 1) {
    return {KEY_NONE, 0};
  }

  int key = 0;
  if (!keyAt(point, key)) {
    return {KEY_NONE, 0};
  }

  TextBuffer buffer = charBuffer(text, capacity);
  return handleKey(key, buffer);
}

bool T9KeyboardComponent::keyAt(const TouchPoint &point, int &key) const {
  if (point.x < GRID_X || point.x >= GRID_X + 3 * KEY_W + 2 * KEY_GAP ||
      point.y < GRID_Y || point.y >= GRID_Y + 4 * KEY_H + 3 * KEY_GAP) {
    return false;
  }

  const int col = (point.x - GRID_X) / (KEY_W + KEY_GAP);
  const int row = (point.y - GRID_Y) / (KEY_H + KEY_GAP);
  const int keyX = GRID_X + col * (KEY_W + KEY_GAP);
  const int keyY = GRID_Y + row * (KEY_H + KEY_GAP);
  if (col < 0 || col > 2 || row < 0 || row > 3 ||
      point.x >= keyX + KEY_W || point.y >= keyY + KEY_H) {
    return false;
  }

  key = T9_KEYS[row * 3 + col];
  return true;
}

KeyboardEvent T9KeyboardComponent::handleKey(int key, TextBuffer &text) {
  if (key == 10) {
    text.backspace();
    clearPending();
    return {KEY_BACKSPACE, 0};
  }
  if (key == 11) {
    clearPending();
    return {KEY_OK, 0};
  }

  if (layout == Layout::Numbers) {
    const char value = static_cast<char>('0' + key);
    if (!text.append(value)) {
      return {KEY_NONE, 0};
    }
    clearPending();
    return {KEY_CHAR, value};
  }

  const bool sameKey = pendingKey == key && isPendingActive();
  if (text.length >= text.limit && !sameKey) {
    return {KEY_NONE, 0};
  }

  if (sameKey) {
    pendingIndex = (pendingIndex + 1) % strlen(sequenceForKey(key));
    text.backspace();
  } else {
    pendingKey = key;
    pendingIndex = 0;
  }

  pendingAt = millis();
  char value = currentChar();
  if (value != 0) {
    text.append(value);
  }
  return {KEY_CHAR, value};
}

void T9KeyboardComponent::TextBuffer::backspace() {
  if (length <= 0) {
    return;
  }

  if (stringText != nullptr) {
    stringText->remove(length - 1);
  } else if (charText != nullptr) {
    charText[length - 1] = '\0';
  }
  length--;
}

bool T9KeyboardComponent::TextBuffer::append(char value) {
  if (length >= limit) {
    return false;
  }

  if (stringText != nullptr) {
    *stringText += value;
  } else if (charText != nullptr) {
    charText[length] = value;
    charText[length + 1] = '\0';
  } else {
    return false;
  }
  length++;
  return true;
}

T9KeyboardComponent::TextBuffer
T9KeyboardComponent::stringBuffer(String &text, int maxLength) {
  return {&text, nullptr, static_cast<int>(text.length()),
          maxLength > 0 ? maxLength : DEFAULT_TEXT_LIMIT};
}

T9KeyboardComponent::TextBuffer T9KeyboardComponent::charBuffer(char *text,
                                                                int capacity) {
  return {nullptr, text, static_cast<int>(strlen(text)), capacity - 1};
}
