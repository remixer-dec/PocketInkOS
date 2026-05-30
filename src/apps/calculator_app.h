#ifndef CALCULATOR_APP_H
#define CALCULATOR_APP_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>

class CalculatorApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;

private:
  char expression[32] = {};
  char result[24] = {};
  unsigned long lastDelAt = 0;
  bool showingResult = false;
  bool error = false;

  void append(char c);
  bool canAppendDot() const;
  void backspace();
  void clear();
  void calculate();
  bool parseExpression(const char *text, double &value) const;
  void formatResult(double value);
};

#endif
