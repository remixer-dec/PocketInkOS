#include "apps/calculator_app.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>

static const int BUTTON_W = 46;
static const int BUTTON_H = 27;
static const int BUTTON_X = 4;
static const int BUTTON_Y = 48;
static const int BUTTON_GAP = 4;
static const char *BUTTONS[5][4] = {{"DEL", "%", "^", "/"},
                                    {"7", "8", "9", "*"},
                                    {"4", "5", "6", "-"},
                                    {"1", "2", "3", "+"},
                                    {"0", ".", "(", ")"}};
static const UiRect DISPLAY_RECT = {6, 16, 146, 28};
static const UiRect EQUALS_BUTTON = {158, 16, 36, 28};

class ExpressionParser {
public:
  explicit ExpressionParser(const char *source) : text(source) {}

  bool parse(double &value) {
    pos = 0;
    ok = true;
    value = parseSum();
    skipSpaces();
    return ok && text[pos] == '\0' && std::isfinite(value);
  }

private:
  const char *text;
  int pos = 0;
  bool ok = true;

  void skipSpaces() {
    while (text[pos] == ' ') {
      pos++;
    }
  }

  bool match(char c) {
    skipSpaces();
    if (text[pos] != c) {
      return false;
    }
    pos++;
    return true;
  }

  double parseSum() {
    double value = parseProduct();
    while (ok) {
      if (match('+')) {
        value += parseProduct();
      } else if (match('-')) {
        value -= parseProduct();
      } else {
        break;
      }
    }
    return value;
  }

  double parseProduct() {
    double value = parseUnary();
    while (ok) {
      if (match('*')) {
        value *= parseUnary();
      } else if (match('/')) {
        double rhs = parseUnary();
        if (rhs == 0.0) {
          ok = false;
          return 0.0;
        }
        value /= rhs;
      } else if (match('%')) {
        double rhs = parseUnary();
        if (rhs == 0.0) {
          ok = false;
          return 0.0;
        }
        value = fmod(value, rhs);
      } else {
        break;
      }
    }
    return value;
  }

  double parseUnary() {
    if (match('+')) {
      return parseUnary();
    }
    if (match('-')) {
      return -parseUnary();
    }
    return parsePrimary();
  }

  double parsePrimary() {
    skipSpaces();
    if (match('(')) {
      double value = parseSum();
      if (!match(')')) {
        ok = false;
      }
      return value;
    }

    char *end = nullptr;
    double value = strtod(text + pos, &end);
    if (end == text + pos) {
      ok = false;
      return 0.0;
    }
    pos = end - text;
    while (match('^')) {
      value *= value;
    }
    return value;
  }
};

void CalculatorApp::reset() { clear(); }

bool CalculatorApp::hasActiveSession() const { return expression[0] != '\0'; }

void CalculatorApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  gfx.getTextBounds("CALC", 0, 0, &titleX, &titleY, &titleW, &titleH);
  gfx.setCursor((200 - static_cast<int>(titleW)) / 2 - titleX, 4);
  gfx.print("CALC");

  gfx.drawRect(DISPLAY_RECT.x, DISPLAY_RECT.y, DISPLAY_RECT.w, DISPLAY_RECT.h,
               1);
  gfx.setTextSize(1);
  gfx.setCursor(DISPLAY_RECT.x + 6, DISPLAY_RECT.y + 10);
  gfx.print(showingResult ? result : expression);
  uiDrawButton(gfx, EQUALS_BUTTON, "=");

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 4; col++) {
      UiRect rect = {static_cast<int16_t>(BUTTON_X + col * (BUTTON_W + BUTTON_GAP)),
                     static_cast<int16_t>(BUTTON_Y + row * (BUTTON_H + BUTTON_GAP)),
                     BUTTON_W, BUTTON_H};
      uiDrawButton(gfx, rect, BUTTONS[row][col]);
    }
  }
}

bool CalculatorApp::handleTouch(const TouchPoint &point) {
  if (uiContains(EQUALS_BUTTON, point)) {
    calculate();
    return true;
  }
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 4; col++) {
      UiRect rect = {static_cast<int16_t>(BUTTON_X + col * (BUTTON_W + BUTTON_GAP)),
                     static_cast<int16_t>(BUTTON_Y + row * (BUTTON_H + BUTTON_GAP)),
                     BUTTON_W, BUTTON_H};
      if (!uiContains(rect, point)) {
        continue;
      }
      const char *label = BUTTONS[row][col];
      if (strcmp(label, "DEL") == 0) {
        backspace();
      } else {
        append(label[0]);
      }
      return true;
    }
  }
  return false;
}

void CalculatorApp::append(char c) {
  if (c == '.' && !canAppendDot()) {
    return;
  }
  if (showingResult && (c >= '0' && c <= '9')) {
    clear();
  } else if (showingResult) {
    strncpy(expression, result, sizeof(expression) - 1);
    showingResult = false;
  }
  size_t len = strlen(expression);
  if (len + 1 >= sizeof(expression)) {
    return;
  }
  expression[len] = c;
  expression[len + 1] = '\0';
  showingResult = false;
  error = false;
}

void CalculatorApp::backspace() {
  unsigned long now = millis();
  if (now - lastDelAt < 700) {
    clear();
    lastDelAt = 0;
    return;
  }
  lastDelAt = now;
  if (showingResult) {
    clear();
    return;
  }
  size_t len = strlen(expression);
  if (len > 0) {
    expression[len - 1] = '\0';
  }
}

void CalculatorApp::clear() {
  expression[0] = '\0';
  result[0] = '\0';
  lastDelAt = 0;
  showingResult = false;
  error = false;
}

void CalculatorApp::calculate() {
  double value = 0.0;
  if (!parseExpression(expression, value)) {
    strncpy(result, "ERR", sizeof(result) - 1);
    result[sizeof(result) - 1] = '\0';
    showingResult = true;
    error = true;
    return;
  }
  formatResult(value);
  showingResult = true;
  error = false;
}

bool CalculatorApp::parseExpression(const char *text, double &value) const {
  if (!text || text[0] == '\0') {
    return false;
  }
  ExpressionParser parser(text);
  return parser.parse(value);
}

bool CalculatorApp::canAppendDot() const {
  const char *source = showingResult ? result : expression;
  for (int i = strlen(source) - 1; i >= 0; --i) {
    char c = source[i];
    if (c == '.') {
      return false;
    }
    if ((c < '0' || c > '9')) {
      return true;
    }
  }
  return true;
}

void CalculatorApp::formatResult(double value) {
  if (fabs(value) > 999999999.0) {
    snprintf(result, sizeof(result), "%.6g", value);
    return;
  }
  snprintf(result, sizeof(result), "%.8f", value);
  char *dot = strchr(result, '.');
  if (!dot) {
    return;
  }
  char *end = result + strlen(result) - 1;
  while (end > dot && *end == '0') {
    *end-- = '\0';
  }
  if (*end == '.') {
    *end = '\0';
  }
}
