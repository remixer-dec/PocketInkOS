#include "hangman_app.h"
#include "ui_helpers.h"

#include <Arduino.h>
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

void HangmanApp::reset() {
  state = STATE_INTRO;
  word[0] = 0;
  inputText = "";
  for (int i = 0; i < 26; i++) {
    guessed[i] = false;
  }
  keyboardOpen = false;
  misses = 0;
}

void HangmanApp::draw(Adafruit_GFX &gfx) {
  if (keyboardOpen) {
    inputKeyboard.draw(gfx, inputText,
                       state == STATE_ENTER_WORD ? SECRET_MAX : GUESS_MAX);
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
    return true;
  }
  openKeyboard();
  return true;
}

bool HangmanApp::hasActiveSession() const { return state == STATE_GUESSING; }

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
  state = STATE_GUESSING;
}

void HangmanApp::drawIntro(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(44, 20);
  gfx.print("HANGMAN");

  gfx.setTextSize(1);
  gfx.setCursor(29, 48);
  gfx.print("Choose game mode");
  uiDrawButton(gfx, CPU_BUTTON, "VS CPU");
  uiDrawButton(gfx, PVP_BUTTON, "2 PLAYER");
}

void HangmanApp::drawEntry(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(56, 10);
  gfx.print("SECRET WORD");

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
  gfx.setCursor(12, 86);
  gfx.print("P2 looks away");
  uiDrawButton(gfx, INPUT_BUTTON, "WORD");
}

void HangmanApp::drawGame(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);

  gfx.setCursor(68, 10);
  if (state == STATE_WON) {
    gfx.print("YOU WIN");
  } else if (state == STATE_LOST) {
    gfx.print("WORD: ");
    gfx.print(word);
  } else {
    gfx.print(MAX_MISSES - misses);
    gfx.print(" <3");
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
  gfx.drawLine(52, 74, 90, 74, 1);
  gfx.drawLine(58, 74, 58, 28, 1);
  gfx.drawLine(58, 28, 82, 28, 1);
  gfx.drawLine(82, 28, 82, 36, 1);

  if (misses > 0)
    gfx.drawCircle(82, 42, 6, 1);
  if (misses > 1)
    gfx.drawLine(82, 48, 82, 60, 1);
  if (misses > 2)
    gfx.drawLine(82, 52, 74, 58, 1);
  if (misses > 3)
    gfx.drawLine(82, 52, 90, 58, 1);
  if (misses > 4)
    gfx.drawLine(82, 60, 75, 69, 1);
  if (misses > 5)
    gfx.drawLine(82, 60, 89, 69, 1);
}

bool HangmanApp::handleKeyboardTouch(const TouchPoint &point) {
  KeyboardEvent event = inputKeyboard.hitTest(
      point, inputText, state == STATE_ENTER_WORD ? SECRET_MAX : GUESS_MAX);
  if (event.action == KEY_NONE) {
    return false;
  }
  if (event.action == KEY_OK) {
    submitInput();
    return true;
  }
  return true;
}

void HangmanApp::openKeyboard() {
  inputText = "";
  keyboardOpen = true;
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
