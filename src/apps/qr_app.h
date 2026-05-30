#ifndef QR_APP_H
#define QR_APP_H

#include "ui/components/menu_button_consumer.h"
#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"
#include "ui/t9_keyboard/t9_keyboard_component.h"
#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class QrApp : public MenuButtonConsumer {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handleMenuDoubleButton();
  bool handleMenuLongButton();
  bool hasActiveSession() const;
  void setText(const char *text);

private:
  enum Mode { MODE_TEXT, MODE_HTTP, MODE_HTTPS };
  enum KeyboardMode { KEYBOARD_T9, KEYBOARD_QWERTY_ZOOM };
  static const int QR_SIZE = 33;
  static const int MAX_PAYLOAD = 46;

  Mode mode = MODE_TEXT;
  String inputText;
  bool keyboardOpen = false;
  KeyboardMode keyboardMode = KEYBOARD_T9;
  bool hasQr = false;
  bool modules[QR_SIZE][QR_SIZE] = {};

  void openKeyboard();
  void drawKeyboard(Adafruit_GFX &gfx);
  bool handleKeyboardTouch(const TouchPoint &point);
  void submit();
  void drawMenu(Adafruit_GFX &gfx);
  void drawQr(Adafruit_GFX &gfx);
  void buildPayload(char *out, int outSize) const;
  bool encodeQr(const char *text);
  T9KeyboardComponent inputKeyboard;
  QwertyZoomKeyboardComponent zoomKeyboard;
};

#endif
