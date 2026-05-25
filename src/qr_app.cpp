#include "qr_app.h"
#include "ui_helpers.h"

#include <Arduino.h>
#include <cstring>

static const UiRect TEXT_BUTTON = {12, 52, 52, 26};
static const UiRect HTTP_BUTTON = {74, 52, 52, 26};
static const UiRect HTTPS_BUTTON = {136, 52, 52, 26};
static const UiRect INPUT_BUTTON = {50, 96, 100, 24};
static const int DATA_CODEWORDS = 26;
static const int ECC_CODEWORDS = 22;
static const int BLOCKS = 2;
static const int BLOCK_DATA_CODEWORDS = DATA_CODEWORDS / BLOCKS;
static const int TOTAL_CODEWORDS = DATA_CODEWORDS + ECC_CODEWORDS * BLOCKS;
static const int QUIET_MODULES = 4;

static uint8_t gfMul(uint8_t x, uint8_t y) {
  uint8_t result = 0;
  while (y) {
    if (y & 1) {
      result ^= x;
    }
    bool carry = x & 0x80;
    x <<= 1;
    if (carry) {
      x ^= 0x1D;
    }
    y >>= 1;
  }
  return result;
}

static uint8_t gfPow2(int power) {
  uint8_t value = 1;
  for (int i = 0; i < power; i++) {
    value = gfMul(value, 2);
  }
  return value;
}

static void reedSolomon(const uint8_t *data, uint8_t *ecc) {
  uint8_t generator[ECC_CODEWORDS] = {};
  generator[ECC_CODEWORDS - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < ECC_CODEWORDS; i++) {
    for (int j = 0; j < ECC_CODEWORDS; j++) {
      generator[j] = gfMul(generator[j], root);
      if (j + 1 < ECC_CODEWORDS) {
        generator[j] ^= generator[j + 1];
      }
    }
    root = gfMul(root, 2);
  }

  memset(ecc, 0, ECC_CODEWORDS);
  for (int i = 0; i < BLOCK_DATA_CODEWORDS; i++) {
    uint8_t factor = data[i] ^ ecc[0];
    memmove(ecc, ecc + 1, ECC_CODEWORDS - 1);
    ecc[ECC_CODEWORDS - 1] = 0;
    for (int j = 0; j < ECC_CODEWORDS; j++) {
      ecc[j] ^= gfMul(generator[j], factor);
    }
  }
}

void QrApp::reset() {
  mode = MODE_TEXT;
  inputText = "";
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  keyboardMode = KEYBOARD_T9;
  hasQr = false;
  memset(modules, 0, sizeof(modules));
}

bool QrApp::hasActiveSession() const { return keyboardOpen || hasQr; }

void QrApp::setText(const char *text) {
  inputText = text ? text : "";
  encodeQr(inputText.c_str());
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
  clearActiveMenuButtonConsumer(this);
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
  return keyboardOpen && keyboardMode == KEYBOARD_T9 && inputKeyboard.update();
}

bool QrApp::handleTouch(const TouchPoint &point) {
  if (keyboardOpen) {
    return handleKeyboardTouch(point);
  }
  if (hasQr) {
    hasQr = false;
    return true;
  }
  if (uiContains(TEXT_BUTTON, point)) {
    mode = MODE_TEXT;
    inputText = "";
    return true;
  }
  if (uiContains(HTTP_BUTTON, point)) {
    mode = MODE_HTTP;
    inputText = "http://";
    return true;
  }
  if (uiContains(HTTPS_BUTTON, point)) {
    mode = MODE_HTTPS;
    inputText = "https://";
    return true;
  }
  if (uiContains(INPUT_BUTTON, point)) {
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
  setActiveMenuButtonConsumer(this);
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
  clearActiveMenuButtonConsumer(this);
  hasQr = encodeQr(payload);
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
  uiDrawButton(gfx, TEXT_BUTTON, "TEXT", mode == MODE_TEXT);
  uiDrawButton(gfx, HTTP_BUTTON, "HTTP", mode == MODE_HTTP);
  uiDrawButton(gfx, HTTPS_BUTTON, "HTTPS", mode == MODE_HTTPS);
  uiDrawButton(gfx, INPUT_BUTTON, "INPUT");
  gfx.setTextSize(1);
  gfx.setCursor(10, 134);
  gfx.print("Version 3-H, max 24 bytes");
}

void QrApp::drawQr(Adafruit_GFX &gfx) {
  const int drawSize = 200;
  const int totalModules = QR_SIZE + QUIET_MODULES * 2;
  gfx.fillRect(0, 0, drawSize, drawSize, 0);
  for (int y = 0; y < QR_SIZE; y++) {
    for (int x = 0; x < QR_SIZE; x++) {
      if (modules[y][x]) {
        int left = ((x + QUIET_MODULES) * drawSize) / totalModules;
        int top = ((y + QUIET_MODULES) * drawSize) / totalModules;
        int right = ((x + QUIET_MODULES + 1) * drawSize) / totalModules;
        int bottom = ((y + QUIET_MODULES + 1) * drawSize) / totalModules;
        gfx.fillRect(left, top, right - left, bottom - top, 1);
      }
    }
  }
}

void QrApp::buildPayload(char *out, int outSize) const {
  strncpy(out, inputText.c_str(), outSize - 1);
  out[outSize - 1] = '\0';
}

bool QrApp::encodeQr(const char *text) {
  int len = text ? strlen(text) : 0;
  if (len <= 0) {
    return false;
  }
  if (len > MAX_PAYLOAD) {
    len = MAX_PAYLOAD;
  }

  bool reserved[QR_SIZE][QR_SIZE] = {};
  memset(modules, 0, sizeof(modules));

  auto setModule = [&](int row, int col, bool value, bool reserve) {
    if (row < 0 || row >= QR_SIZE || col < 0 || col >= QR_SIZE) {
      return;
    }
    modules[row][col] = value;
    if (reserve) {
      reserved[row][col] = true;
    }
  };

  auto finder = [&](int row, int col) {
    for (int y = -1; y <= 7; y++) {
      for (int x = -1; x <= 7; x++) {
        bool dark = (x >= 0 && x <= 6 && y >= 0 && y <= 6 &&
                     (x == 0 || x == 6 || y == 0 || y == 6 ||
                      (x >= 2 && x <= 4 && y >= 2 && y <= 4)));
        setModule(row + y, col + x, dark, true);
      }
    }
  };

  finder(0, 0);
  finder(0, QR_SIZE - 7);
  finder(QR_SIZE - 7, 0);
  for (int i = 8; i < QR_SIZE - 8; i++) {
    setModule(6, i, i % 2 == 0, true);
    setModule(i, 6, i % 2 == 0, true);
  }
  for (int y = -2; y <= 2; y++) {
    for (int x = -2; x <= 2; x++) {
      int absX = abs(x);
      int absY = abs(y);
      int dist = absX > absY ? absX : absY;
      setModule(22 + y, 22 + x, dist != 1, true);
    }
  }
  setModule(21, 8, true, true);

  for (int i = 0; i < 9; i++) {
    if (i != 6) {
      reserved[8][i] = true;
      reserved[i][8] = true;
    }
  }
  for (int i = 0; i < 8; i++) {
    reserved[8][QR_SIZE - 1 - i] = true;
    reserved[QR_SIZE - 1 - i][8] = true;
  }

  uint8_t data[DATA_CODEWORDS] = {};
  int bitLen = 0;
  auto appendBits = [&](uint32_t value, int count) {
    for (int i = count - 1; i >= 0; i--) {
      if (bitLen / 8 < DATA_CODEWORDS && ((value >> i) & 1)) {
        data[bitLen / 8] |= 0x80 >> (bitLen % 8);
      }
      bitLen++;
    }
  };
  appendBits(0x4, 4);
  appendBits(len, 8);
  for (int i = 0; i < len; i++) {
    appendBits(static_cast<uint8_t>(text[i]), 8);
  }
  int capacityBits = DATA_CODEWORDS * 8;
  appendBits(0, capacityBits - bitLen < 4 ? capacityBits - bitLen : 4);
  while (bitLen % 8 != 0) {
    appendBits(0, 1);
  }
  static const uint8_t pads[2] = {0xEC, 0x11};
  int padIndex = 0;
  while (bitLen / 8 < DATA_CODEWORDS) {
    appendBits(pads[padIndex], 8);
    padIndex = 1 - padIndex;
  }

  uint8_t all[TOTAL_CODEWORDS] = {};
  uint8_t ecc[BLOCKS][ECC_CODEWORDS] = {};
  for (int block = 0; block < BLOCKS; block++) {
    reedSolomon(data + block * BLOCK_DATA_CODEWORDS, ecc[block]);
  }
  int write = 0;
  for (int i = 0; i < BLOCK_DATA_CODEWORDS; i++) {
    for (int block = 0; block < BLOCKS; block++) {
      all[write++] = data[block * BLOCK_DATA_CODEWORDS + i];
    }
  }
  for (int i = 0; i < ECC_CODEWORDS; i++) {
    for (int block = 0; block < BLOCKS; block++) {
      all[write++] = ecc[block][i];
    }
  }

  int bit = 0;
  int dir = -1;
  for (int col = QR_SIZE - 1; col > 0; col -= 2) {
    if (col == 6) {
      col--;
    }
    int row = dir < 0 ? QR_SIZE - 1 : 0;
    while (row >= 0 && row < QR_SIZE) {
      for (int offset = 0; offset < 2; offset++) {
        int c = col - offset;
        if (!reserved[row][c]) {
          bool value = false;
          if (bit < TOTAL_CODEWORDS * 8) {
            value = (all[bit / 8] & (0x80 >> (bit % 8))) != 0;
          }
          if ((row + c) % 2 == 0) {
            value = !value;
          }
          modules[row][c] = value;
          bit++;
        }
      }
      row += dir;
    }
    dir = -dir;
  }

  const uint16_t format = 0x1689;
  for (int i = 0; i < 15; i++) {
    bool value = (format >> i) & 1;
    if (i < 6) {
      setModule(i, 8, value, true);
    } else if (i < 8) {
      setModule(i + 1, 8, value, true);
    } else {
      setModule(QR_SIZE - 15 + i, 8, value, true);
    }
  }

  for (int i = 0; i < 15; i++) {
    bool value = (format >> i) & 1;
    if (i < 8) {
      setModule(8, QR_SIZE - i - 1, value, true);
    } else if (i < 9) {
      setModule(8, 7, value, true);
    } else {
      setModule(8, 14 - i, value, true);
    }
  }
  return true;
}
