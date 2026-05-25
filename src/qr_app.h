#ifndef QR_APP_H
#define QR_APP_H

#include "t9_keyboard_component.h"
#include "touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class QrApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;
  void setText(const char *text);

private:
  enum Mode { MODE_TEXT, MODE_HTTP, MODE_HTTPS };
  static const int QR_SIZE = 29;
  static const int MAX_PAYLOAD = 24;

  Mode mode = MODE_TEXT;
  String inputText;
  bool keyboardOpen = false;
  bool hasQr = false;
  bool modules[QR_SIZE][QR_SIZE] = {};

  void openKeyboard();
  void submit();
  void drawMenu(Adafruit_GFX &gfx);
  void drawQr(Adafruit_GFX &gfx);
  void buildPayload(char *out, int outSize) const;
  bool encodeQr(const char *text);
  T9KeyboardComponent inputKeyboard;
};

#endif
