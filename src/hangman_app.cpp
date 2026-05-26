#include "hangman_app.h"
#include "ui_helpers.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

static const char *CPU_WORDS[] = {"CODE",  "PIXEL", "BOARD", "TOUCH",
                                  "POWER", "GAME",  "INPUT", "DISPLAY"};
static const int CPU_WORD_COUNT = sizeof(CPU_WORDS) / sizeof(CPU_WORDS[0]);
static const int MAX_MISSES = 6;

static const UiRect CPU_BUTTON = {18, 64, 74, 34};
static const UiRect PVP_BUTTON = {108, 64, 74, 34};
static const UiRect INPUT_BUTTON = {50, 176, 100, 22};
static const int SECRET_MAX = 12;
static const int GUESS_MAX = 1;
static const int SCREEN_WIDTH = 200;

static void drawCenteredText(Adafruit_GFX &gfx, const char *text, int y,
                             uint8_t textSize) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.setTextSize(textSize);
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((SCREEN_WIDTH - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

static void drawRightAlignedText(Adafruit_GFX &gfx, const char *text, int x,
                                 int y, uint8_t textSize) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.setTextSize(textSize);
  gfx.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  gfx.setCursor(x - static_cast<int>(w) - x1, y);
  gfx.print(text);
}

void HangmanApp::reset() {
  state = STATE_INTRO;
  word[0] = 0;
  inputText = "";
  for (int i = 0; i < 26; i++) {
    guessed[i] = false;
  }
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  keyboardMode = KEYBOARD_T9;
  misses = 0;
}

void HangmanApp::draw(Adafruit_GFX &gfx) {
  if (keyboardOpen) {
    drawKeyboard(gfx);
    return;
  }
  if (state == STATE_INTRO) {
    drawIntro(gfx);
    return;
  }
  if (state == STATE_ENTER_WORD) {
    drawEntry(gfx);
    return;
  }
  drawGame(gfx);
}

bool HangmanApp::update() {
  return keyboardOpen && keyboardMode == KEYBOARD_T9 && inputKeyboard.update();
}

bool HangmanApp::handleTouch(const TouchPoint &point) {
  if (state == STATE_INTRO) {
    if (uiContains(CPU_BUTTON, point)) {
      startCpuGame();
      return true;
    }
    if (uiContains(PVP_BUTTON, point)) {
      startPlayerGame();
      return true;
    }
    return false;
  }

  if (keyboardOpen) {
    return handleKeyboardTouch(point);
  }

  if (state == STATE_ENTER_WORD) {
    if (uiContains(INPUT_BUTTON, point)) {
      openKeyboard();
      return true;
    }
    return false;
  }

  if (state == STATE_WON || state == STATE_LOST) {
    reset();
    return true;
  }

  if (uiContains(INPUT_BUTTON, point)) {
    openKeyboard();
    return true;
  }
  return false;
}

bool HangmanApp::openKeyboardFromButton() {
  if (state != STATE_ENTER_WORD && state != STATE_GUESSING) {
    return false;
  }
  if (keyboardOpen) {
    keyboardOpen = false;
    clearActiveMenuButtonConsumer(this);
    return true;
  }
  openKeyboard();
  return true;
}

bool HangmanApp::hasActiveSession() const { return state == STATE_GUESSING; }

bool HangmanApp::handleMenuButton() {
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

bool HangmanApp::handleMenuDoubleButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardMode = keyboardMode == KEYBOARD_QWERTY_ZOOM ? KEYBOARD_T9
                                                      : KEYBOARD_QWERTY_ZOOM;
  return true;
}

bool HangmanApp::handleMenuLongButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  return true;
}

void HangmanApp::startCpuGame() {
  randomSeed((unsigned long)micros());
  beginGuessing(CPU_WORDS[random(CPU_WORD_COUNT)]);
}

void HangmanApp::startPlayerGame() {
  state = STATE_ENTER_WORD;
  inputText = "";
}

void HangmanApp::beginGuessing(const char *secret) {
  strncpy(word, secret, sizeof(word) - 1);
  word[sizeof(word) - 1] = 0;
  for (int i = 0; i < 26; i++) {
    guessed[i] = false;
  }
  misses = 0;
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  state = STATE_GUESSING;
}

void HangmanApp::drawIntro(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  drawCenteredText(gfx, "HANGMAN", 20, 2);
  drawCenteredText(gfx, "Choose game mode", 48, 1);
  uiDrawButton(gfx, CPU_BUTTON, "VS CPU");
  uiDrawButton(gfx, PVP_BUTTON, "2 PLAYER");
}

void HangmanApp::drawEntry(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  drawCenteredText(gfx, "SECRET WORD", 10, 1);

  gfx.drawRect(12, 32, 176, 28, 1);
  gfx.setTextSize(2);
  gfx.setCursor(18, 38);
  const char *text = inputText.c_str();
  for (int i = 0; text[i]; i++) {
    gfx.print("*");
  }

  gfx.setTextSize(1);
  gfx.setCursor(17, 70);
  gfx.print("Player 1 enters word");
  gfx.setCursor(17, 82);
  gfx.print("Player 2 looks away");
  uiDrawButton(gfx, INPUT_BUTTON, "WORD");
}

void HangmanApp::drawGame(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);

  if (state == STATE_WON) {
    drawCenteredText(gfx, "YOU WIN", 10, 1);
  } else if (state == STATE_LOST) {
    drawCenteredText(gfx, "WORD:", 10, 1);
    drawRightAlignedText(gfx, word, 188, 10, 1);
  } else {
    char lives[8];
    snprintf(lives, sizeof(lives), "%d <3", MAX_MISSES - misses);
    drawRightAlignedText(gfx, lives, 180, 10, 1);
  }

  drawGallows(gfx);

  gfx.setTextSize(2);
  int wordLen = strlen(word);
  int startX = 100 - (wordLen * 12) / 2;
  if (startX < 4) {
    startX = 4;
  }
  gfx.setCursor(startX, 88);
  for (int i = 0; word[i]; i++) {
    char letter = word[i];
    gfx.print(guessed[letter - 'A'] || state == STATE_LOST ? letter : '_');
  }

  gfx.setTextSize(1);
  gfx.setCursor(16, 122);
  gfx.print("USED:");
  int x = 50;
  int y = 122;
  for (int i = 0; i < 26; i++) {
    if (!guessed[i]) {
      continue;
    }
    gfx.setCursor(x, y);
    gfx.print((char)('A' + i));
    x += 8;
    if (x > 184) {
      x = 16;
      y += 12;
    }
  }

  uiDrawButton(gfx, INPUT_BUTTON, "GUESS");
}

void HangmanApp::drawGallows(Adafruit_GFX &gfx) {
  const int shiftX = 29;
  gfx.drawLine(52 + shiftX, 74, 90 + shiftX, 74, 1);
  gfx.drawLine(58 + shiftX, 74, 58 + shiftX, 28, 1);
  gfx.drawLine(58 + shiftX, 28, 82 + shiftX, 28, 1);
  gfx.drawLine(82 + shiftX, 28, 82 + shiftX, 36, 1);

  if (misses > 0)
    gfx.drawCircle(82 + shiftX, 42, 6, 1);
  if (misses > 1)
    gfx.drawLine(82 + shiftX, 48, 82 + shiftX, 60, 1);
  if (misses > 2)
    gfx.drawLine(82 + shiftX, 52, 74 + shiftX, 58, 1);
  if (misses > 3)
    gfx.drawLine(82 + shiftX, 52, 90 + shiftX, 58, 1);
  if (misses > 4)
    gfx.drawLine(82 + shiftX, 60, 75 + shiftX, 69, 1);
  if (misses > 5)
    gfx.drawLine(82 + shiftX, 60, 89 + shiftX, 69, 1);
}

bool HangmanApp::handleKeyboardTouch(const TouchPoint &point) {
  size_t maxLength = state == STATE_ENTER_WORD ? SECRET_MAX : GUESS_MAX;
  KeyboardEvent event =
      keyboardMode == KEYBOARD_QWERTY_ZOOM
          ? zoomKeyboard.hitTest(point, inputText.length(), maxLength)
          : inputKeyboard.hitTest(point, inputText, maxLength);
  if (event.action == KEY_NONE) {
    return false;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    if (event.action == KEY_CHAR && inputText.length() < maxLength) {
      inputText += event.value;
    } else if (event.action == KEY_SPACE && inputText.length() < maxLength) {
      inputText += ' ';
    } else if (event.action == KEY_BACKSPACE && inputText.length() > 0) {
      inputText.remove(inputText.length() - 1);
    }
  }
  if (event.action == KEY_OK) {
    submitInput();
    return true;
  }
  return true;
}

void HangmanApp::openKeyboard() {
  inputText = "";
  keyboardMode = KEYBOARD_T9;
  keyboardOpen = true;
  setActiveMenuButtonConsumer(this);
}

void HangmanApp::drawKeyboard(Adafruit_GFX &gfx) {
  int maxLength = state == STATE_ENTER_WORD ? SECRET_MAX : GUESS_MAX;
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.draw(gfx, inputText, maxLength);
    return;
  }
  inputKeyboard.draw(gfx, inputText, maxLength);
}

void HangmanApp::submitInput() {
  if (state == STATE_ENTER_WORD) {
    char secret[13] = {0};
    copySanitizedWord(secret, sizeof(secret));
    if (secret[0]) {
      beginGuessing(secret);
    }
    return;
  }

  char letter = firstInputLetter();
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  if (letter) {
    guessLetter(letter);
  }
}

void HangmanApp::copySanitizedWord(char *target, int targetSize) const {
  const char *source = inputText.c_str();
  int write = 0;
  for (int i = 0; source[i] && write < targetSize - 1; i++) {
    char letter = source[i];
    if (letter >= 'a' && letter <= 'z') {
      letter = letter - 'a' + 'A';
    }
    if (letter >= 'A' && letter <= 'Z') {
      target[write++] = letter;
    }
  }
  target[write] = 0;
}

char HangmanApp::firstInputLetter() const {
  const char *source = inputText.c_str();
  for (int i = 0; source[i]; i++) {
    char letter = source[i];
    if (letter >= 'a' && letter <= 'z') {
      letter = letter - 'a' + 'A';
    }
    if (letter >= 'A' && letter <= 'Z') {
      return letter;
    }
  }
  return 0;
}

void HangmanApp::guessLetter(char letter) {
  int index = letter - 'A';
  if (index < 0 || index >= 26 || guessed[index]) {
    return;
  }
  guessed[index] = true;
  if (!containsLetter(letter)) {
    misses++;
  }
  if (misses >= MAX_MISSES) {
    state = STATE_LOST;
  } else if (allLettersGuessed()) {
    state = STATE_WON;
  }
}

bool HangmanApp::containsLetter(char letter) const {
  for (int i = 0; word[i]; i++) {
    if (word[i] == letter) {
      return true;
    }
  }
  return false;
}

bool HangmanApp::allLettersGuessed() const {
  for (int i = 0; word[i]; i++) {
    if (!guessed[word[i] - 'A']) {
      return false;
    }
  }
  return true;
}
