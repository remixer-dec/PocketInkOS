#ifndef QR_APP_H
#define QR_APP_H

#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"
#include "ui/components/t9_keyboard_component.h"
#include "ui/ui_helpers.h"
#include "ui/system_qr.h"
#include "sys/services/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

class QrApp {
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
  size_t saveContext(uint8_t *buffer, size_t capacity) const;
  void restoreContext(const uint8_t *buffer, size_t length);

private:
  enum Mode { MODE_TEXT, MODE_HTTP, MODE_HTTPS };
  enum KeyboardMode { KEYBOARD_T9, KEYBOARD_QWERTY_ZOOM };
  static const int MAX_PAYLOAD = SystemQrCode::maxPayload;

  Mode mode = MODE_TEXT;
  String inputText;
  bool keyboardOpen = false;
  KeyboardMode keyboardMode = KEYBOARD_T9;
  bool hasQr = false;
  SystemQrCode qr;
  UiPressFeedback press;

  void openKeyboard();
  void drawKeyboard(Adafruit_GFX &gfx);
  bool handleKeyboardTouch(const TouchPoint &point);
  void submit();
  void drawMenu(Adafruit_GFX &gfx);
  void drawQr(Adafruit_GFX &gfx);
  void buildPayload(char *out, int outSize) const;
  T9KeyboardComponent inputKeyboard;
  QwertyZoomKeyboardComponent zoomKeyboard;
};

#endif
