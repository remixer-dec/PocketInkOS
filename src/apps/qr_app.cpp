#include "apps/qr_app.h"
#include "sys/rtc_context.h"
#include "ui/components/select_button_component.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cstring>

static const UiRect TEXT_BUTTON = {12, 52, 52, 26};
static const UiRect HTTP_BUTTON = {74, 52, 52, 26};
static const UiRect HTTPS_BUTTON = {136, 52, 52, 26};
static const UiRect INPUT_BUTTON = {50, 96, 100, 24};
static const int8_t TEXT_BUTTON_ID = 0;
static const int8_t HTTP_BUTTON_ID = 1;
static const int8_t HTTPS_BUTTON_ID = 2;
static const int8_t INPUT_BUTTON_ID = 3;
static const SelectButtonOption MODE_BUTTONS[] = {
    {TEXT_BUTTON, "TEXT", TEXT_BUTTON_ID},
    {HTTP_BUTTON, "HTTP", HTTP_BUTTON_ID},
    {HTTPS_BUTTON, "HTTPS", HTTPS_BUTTON_ID},
};
static const uint8_t QR_CONTEXT_VERSION = 1;

void QrApp::reset() {
  mode = MODE_TEXT;
  inputText = "";
  keyboardOpen = false;
  keyboardMode = KEYBOARD_T9;
  hasQr = false;
  press.clear();
  qr.clear();
}

bool QrApp::hasActiveSession() const { return keyboardOpen || hasQr; }

size_t QrApp::saveContext(uint8_t *buffer, size_t capacity) const {
  size_t textLength = inputText.length();
  if (textLength > MAX_PAYLOAD) {
    return 0;
  }

  RtcBitWriter writer(buffer, capacity);
  writer.writeBits(QR_CONTEXT_VERSION, 4);
  writer.writeBits(static_cast<uint8_t>(mode), 2);
  writer.writeBits(hasQr ? 1 : 0, 1);
  writer.writeBits(keyboardOpen ? 1 : 0, 1);
  writer.writeBits(keyboardMode == KEYBOARD_QWERTY_ZOOM ? 1 : 0, 1);
  writer.writeBits(static_cast<uint8_t>(textLength), 6);
  const char *text = inputText.c_str();
  for (size_t i = 0; i < textLength; i++) {
    char c = text[i];
    if (static_cast<uint8_t>(c) > 0x7f) {
      return 0;
    }
    writer.writeBits(static_cast<uint8_t>(c), 7);
  }
  return writer.ok() ? writer.bytesWritten() : 0;
}

void QrApp::restoreContext(const uint8_t *buffer, size_t length) {
  RtcBitReader reader(buffer, length);
  uint32_t value = 0;
  if (!reader.readBits(4, value) || value != QR_CONTEXT_VERSION) {
    return;
  }

  uint32_t savedMode = 0;
  uint32_t savedHasQr = 0;
  uint32_t savedKeyboardOpen = 0;
  uint32_t savedKeyboardMode = 0;
  uint32_t textLength = 0;
  if (!reader.readBits(2, savedMode) || savedMode > MODE_HTTPS ||
      !reader.readBits(1, savedHasQr) ||
      !reader.readBits(1, savedKeyboardOpen) ||
      !reader.readBits(1, savedKeyboardMode) ||
      !reader.readBits(6, textLength) || textLength > MAX_PAYLOAD) {
    return;
  }

  char text[MAX_PAYLOAD + 1] = {};
  for (uint32_t i = 0; i < textLength; i++) {
    if (!reader.readBits(7, value) || value > 0x7f) {
      return;
    }
    text[i] = static_cast<char>(value);
  }
  if (!reader.ok()) {
    return;
  }

  mode = static_cast<Mode>(savedMode);
  inputText = text;
  keyboardOpen = savedKeyboardOpen != 0;
  keyboardMode =
      savedKeyboardMode ? KEYBOARD_QWERTY_ZOOM : KEYBOARD_T9;
  hasQr = false;
  qr.clear();
  if (savedHasQr) {
    char payload[MAX_PAYLOAD + 1];
    buildPayload(payload, sizeof(payload));
    hasQr = qr.encode(payload);
  }
}

void QrApp::setText(const char *text) {
  inputText = text ? text : "";
  hasQr = qr.encode(inputText.c_str());
}

bool QrApp::handleMenuButton() {
  if (!keyboardOpen) {
    return false;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.toggleCaps();
  } else {
    inputKeyboard.toggleCaps();
  }
  return true;
}

bool QrApp::handleMenuDoubleButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardMode = keyboardMode == KEYBOARD_QWERTY_ZOOM ? KEYBOARD_T9
                                                      : KEYBOARD_QWERTY_ZOOM;
  return true;
}

bool QrApp::handleMenuLongButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardOpen = false;
  return true;
}

void QrApp::draw(Adafruit_GFX &gfx) {
  if (keyboardOpen) {
    drawKeyboard(gfx);
    return;
  }
  if (hasQr) {
    drawQr(gfx);
    return;
  }
  drawMenu(gfx);
}

bool QrApp::update() {
  bool dirty = press.update();
  if (keyboardOpen && keyboardMode == KEYBOARD_T9 && inputKeyboard.update()) {
    dirty = true;
  }
  return dirty;
}

bool QrApp::handleTouch(const TouchPoint &point) {
  if (keyboardOpen) {
    return handleKeyboardTouch(point);
  }
  if (hasQr) {
    hasQr = false;
    return true;
  }
  int8_t nextMode = -1;
  if (SelectButtonGroup(MODE_BUTTONS, 3).hitTest(point, nextMode)) {
    press.press(nextMode);
    mode = static_cast<Mode>(nextMode);
    if (mode == MODE_TEXT) {
      inputText = "";
    } else if (mode == MODE_HTTP) {
      inputText = "http://";
    } else {
      inputText = "https://";
    }
    return true;
  }
  if (uiTouchContains(INPUT_BUTTON, point)) {
    press.press(INPUT_BUTTON_ID);
    openKeyboard();
    return true;
  }
  return false;
}

void QrApp::openKeyboard() {
  if (mode == MODE_TEXT && inputText.length() == 0) {
    inputText = "";
  } else if (mode == MODE_HTTP && strncmp(inputText.c_str(), "http://", 7) != 0) {
    inputText = "http://";
  } else if (mode == MODE_HTTPS &&
             strncmp(inputText.c_str(), "https://", 8) != 0) {
    inputText = "https://";
  }
  keyboardOpen = true;
  keyboardMode = KEYBOARD_T9;
}

void QrApp::drawKeyboard(Adafruit_GFX &gfx) {
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.draw(gfx, inputText, MAX_PAYLOAD);
    return;
  }
  inputKeyboard.draw(gfx, inputText, MAX_PAYLOAD);
}

bool QrApp::handleKeyboardTouch(const TouchPoint &point) {
  KeyboardEvent event =
      keyboardMode == KEYBOARD_QWERTY_ZOOM
          ? zoomKeyboard.hitTest(point, inputText.length(), MAX_PAYLOAD)
          : inputKeyboard.hitTest(point, inputText, MAX_PAYLOAD);
  if (event.action == KEY_NONE) {
    return false;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    if (event.action == KEY_CHAR && inputText.length() < MAX_PAYLOAD) {
      inputText += event.value;
    } else if (event.action == KEY_SPACE && inputText.length() < MAX_PAYLOAD) {
      inputText += ' ';
    } else if (event.action == KEY_BACKSPACE && inputText.length() > 0) {
      inputText.remove(inputText.length() - 1);
    }
  }
  if (event.action == KEY_OK) {
    submit();
  }
  return true;
}

void QrApp::submit() {
  char payload[MAX_PAYLOAD + 1];
  buildPayload(payload, sizeof(payload));
  keyboardOpen = false;
  hasQr = qr.encode(payload);
}

void QrApp::drawMenu(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds("QR", 0, 0, &x, &y, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x, 16);
  gfx.print("QR");
  SelectButtonGroup(MODE_BUTTONS, 3)
      .draw(gfx, static_cast<int8_t>(mode), &press);
  uiDrawButton(gfx, INPUT_BUTTON, "INPUT", press.isPressed(INPUT_BUTTON_ID));
  gfx.setTextSize(1);
  gfx.setCursor(10, 134);
  gfx.print("Version 4-Q, max 46 bytes");
}

void QrApp::drawQr(Adafruit_GFX &gfx) {
  const int drawSize = 200;
  gfx.fillRect(0, 0, drawSize, drawSize, 0);
  qr.draw(gfx, 0, 0, drawSize);
}

void QrApp::buildPayload(char *out, int outSize) const {
  strncpy(out, inputText.c_str(), outSize - 1);
  out[outSize - 1] = '\0';
}
