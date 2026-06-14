#include "games/wordle_app.h"
#include "sys/rtc_context.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

static const char *CPU_WORDS[] = {
    "APPLE", "BRAVE", "CRANE", "DRIVE", "EAGER", "FLAME", "GRAPE",
    "HOUSE", "INDEX", "JELLY", "KNIFE", "LEMON", "MONEY", "NURSE",
    "OCEAN", "PLANT", "QUEEN", "ROBOT", "SHEEP", "TRAIN", "UNITY",
    "VOTER", "WATER", "XENON", "YOUTH", "ZEBRA", "BRAIN", "CHAIR",
    "DELTA", "EARTH", "FIELD", "GIANT", "HONEY", "IVORY", "JOKER",
    "LIGHT", "MAGIC", "NIGHT", "OPERA", "PIZZA", "QUICK", "RIVER",
    "STONE", "TIGER", "URBAN", "VOICE", "WHEEL", "YEAST", "CLOUD",
    "SOLAR"};
static const int CPU_WORD_COUNT = sizeof(CPU_WORDS) / sizeof(CPU_WORDS[0]);

static const UiRect CPU_BUTTON = {18, 54, 74, 32};
static const UiRect PVP_BUTTON = {108, 54, 74, 32};
static const UiRect INPUT_BUTTON = {50, 176, 100, 22};
static const int WORD_LENGTH = 5;

static const int BOARD_X = 25;
static const int BOARD_Y = 20;
static const int TILE = 28;
static const int TILE_GAP = 2;
static const uint8_t WORDLE_CONTEXT_VERSION = 1;

static bool writeWordleLetters(RtcBitWriter &writer, const char *letters,
                               int count) {
  for (int i = 0; i < count; i++) {
    if (letters[i] < 'A' || letters[i] > 'Z') {
      return false;
    }
    writer.writeBits(static_cast<uint8_t>(letters[i] - 'A'), 5);
  }
  return writer.ok();
}

static bool readWordleLetters(RtcBitReader &reader, char *letters, int count) {
  uint32_t value = 0;
  for (int i = 0; i < count; i++) {
    if (!reader.readBits(5, value) || value >= 26) {
      return false;
    }
    letters[i] = static_cast<char>('A' + value);
  }
  letters[count] = '\0';
  return true;
}

static void drawDiagonalHatch(Adafruit_GFX &gfx, int x, int y, int w, int h,
                              bool falling) {
  for (int offset = -h; offset < w; offset += 5) {
    int x1 = x + offset;
    int y1 = falling ? y : y + h - 1;
    int x2 = x + offset + h - 1;
    int y2 = falling ? y + h - 1 : y;
    if (x1 < x) {
      int delta = x - x1;
      x1 += delta;
      y1 += falling ? delta : -delta;
    }
    if (x2 >= x + w) {
      int delta = x2 - (x + w - 1);
      x2 -= delta;
      y2 -= falling ? delta : -delta;
    }
    if (y1 >= y && y1 < y + h && y2 >= y && y2 < y + h) {
      gfx.drawLine(x1, y1, x2, y2, 1);
    }
  }
}

static void drawCenteredText(Adafruit_GFX &gfx, const char *text, int y,
                             uint8_t textSize) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.setTextSize(textSize);
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void WordleApp::reset() {
  state = STATE_INTRO;
  target[0] = 0;
  inputText = "";
  row = 0;
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  keyboardMode = KEYBOARD_T9;
  for (int i = 0; i < 6; i++) {
    memset(guesses[i], 0, sizeof(guesses[i]));
    for (int j = 0; j < 5; j++) {
      marks[i][j] = MARK_NONE;
    }
  }
  for (int i = 0; i < 26; i++) {
    keyMarks[i] = MARK_NONE;
  }
}

void WordleApp::draw(Adafruit_GFX &gfx) {
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

bool WordleApp::update() {
  return keyboardOpen && keyboardMode == KEYBOARD_T9 && inputKeyboard.update();
}

bool WordleApp::handleTouch(const TouchPoint &point) {
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

  if (state == STATE_WON || state == STATE_LOST) {
    reset();
    return true;
  }

  if (keyboardOpen) {
    return handleKeyboardTouch(point);
  }
  if (uiContains(INPUT_BUTTON, point)) {
    openKeyboard();
    return true;
  }
  return false;
}

bool WordleApp::openKeyboardFromButton() {
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

bool WordleApp::hasActiveSession() const { return state == STATE_GUESSING; }

size_t WordleApp::saveContext(uint8_t *buffer, size_t capacity) const {
  if (target[0] == '\0' ||
      (state != STATE_GUESSING && state != STATE_WON &&
       state != STATE_LOST) ||
      row < 0 || row > 5) {
    return 0;
  }

  uint8_t guessRows = state == STATE_GUESSING ? row : row + 1;
  if (guessRows > 6) {
    return 0;
  }

  RtcBitWriter writer(buffer, capacity);
  writer.writeBits(WORDLE_CONTEXT_VERSION, 4);
  writer.writeBits(static_cast<uint8_t>(state), 3);
  writer.writeBits(static_cast<uint8_t>(row), 3);
  writer.writeBits(guessRows, 3);
  if (!writeWordleLetters(writer, target, WORD_LENGTH)) {
    return 0;
  }
  for (uint8_t r = 0; r < guessRows; r++) {
    if (!writeWordleLetters(writer, guesses[r], WORD_LENGTH)) {
      return 0;
    }
  }
  return writer.ok() ? writer.bytesWritten() : 0;
}

void WordleApp::restoreContext(const uint8_t *buffer, size_t length) {
  RtcBitReader reader(buffer, length);
  uint32_t value = 0;
  if (!reader.readBits(4, value) || value != WORDLE_CONTEXT_VERSION) {
    return;
  }

  uint32_t savedState = 0;
  uint32_t savedRow = 0;
  uint32_t guessRows = 0;
  if (!reader.readBits(3, savedState) || !reader.readBits(3, savedRow) ||
      !reader.readBits(3, guessRows) || savedRow > 5 || guessRows > 6 ||
      (savedState != STATE_GUESSING && savedState != STATE_WON &&
       savedState != STATE_LOST)) {
    return;
  }
  if ((savedState == STATE_GUESSING && guessRows != savedRow) ||
      (savedState != STATE_GUESSING && guessRows != savedRow + 1)) {
    return;
  }

  char nextTarget[WORD_LENGTH + 1] = {};
  char nextGuesses[6][WORD_LENGTH + 1] = {{0}};
  if (!readWordleLetters(reader, nextTarget, WORD_LENGTH)) {
    return;
  }
  for (uint32_t r = 0; r < guessRows; r++) {
    if (!readWordleLetters(reader, nextGuesses[r], WORD_LENGTH)) {
      return;
    }
  }
  if (!reader.ok()) {
    return;
  }

  strcpy(target, nextTarget);
  for (int r = 0; r < 6; r++) {
    memset(guesses[r], 0, sizeof(guesses[r]));
    for (int c = 0; c < WORD_LENGTH; c++) {
      marks[r][c] = MARK_NONE;
    }
  }
  for (int i = 0; i < 26; i++) {
    keyMarks[i] = MARK_NONE;
  }
  for (uint32_t r = 0; r < guessRows; r++) {
    strcpy(guesses[r], nextGuesses[r]);
    row = static_cast<int>(r);
    evaluateGuess();
  }
  row = static_cast<int>(savedRow);
  state = static_cast<State>(savedState);
  inputText = "";
  keyboardOpen = false;
  keyboardMode = KEYBOARD_T9;
  clearActiveMenuButtonConsumer(this);
}

bool WordleApp::handleMenuButton() {
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

bool WordleApp::handleMenuDoubleButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardMode = keyboardMode == KEYBOARD_QWERTY_ZOOM ? KEYBOARD_T9
                                                      : KEYBOARD_QWERTY_ZOOM;
  return true;
}

bool WordleApp::handleMenuLongButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  return true;
}

void WordleApp::startCpuGame() {
  randomSeed((unsigned long)micros());
  beginGuessing(CPU_WORDS[random(CPU_WORD_COUNT)]);
}

void WordleApp::startPlayerGame() {
  state = STATE_ENTER_WORD;
  inputText = "";
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
}

void WordleApp::beginGuessing(const char *word) {
  strncpy(target, word, sizeof(target) - 1);
  target[5] = 0;
  for (int i = 0; i < 6; i++) {
    memset(guesses[i], 0, sizeof(guesses[i]));
    for (int j = 0; j < 5; j++) {
      marks[i][j] = MARK_NONE;
    }
  }
  for (int i = 0; i < 26; i++) {
    keyMarks[i] = MARK_NONE;
  }
  inputText = "";
  row = 0;
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  state = STATE_GUESSING;
}

void WordleApp::drawIntro(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  drawCenteredText(gfx, "WORDLE", 20, 2);
  drawCenteredText(gfx, "Choose game mode", 44, 1);
  uiDrawButton(gfx, CPU_BUTTON, "VS CPU");
  uiDrawButton(gfx, PVP_BUTTON, "2 PLAYER");
}

void WordleApp::drawEntry(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  drawCenteredText(gfx, "SECRET WORD", 6, 1);

  for (int i = 0; i < 5; i++) {
    int x = BOARD_X + i * (TILE + TILE_GAP);
    char submitted[6] = {0};
    copyCurrentWord(submitted);
    drawTile(gfx, x, 34, submitted[i] ? '*' : 0, MARK_NONE, true);
  }

  gfx.setCursor(24, 66);
  gfx.print("Player 1 enters 5 letters");
  gfx.setCursor(24, 78);
  gfx.print("Player 2 looks away");
  uiDrawButton(gfx, INPUT_BUTTON, "WORD");
}

void WordleApp::drawGame(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  if (state == STATE_WON) {
    drawCenteredText(gfx, "SOLVED", 4, 1);
  } else if (state == STATE_LOST) {
    drawCenteredText(gfx, target, 4, 1);
  } else {
    char label[8];
    snprintf(label, sizeof(label), "TRY %d/6", row + 1);
    drawCenteredText(gfx, label, 4, 1);
  }
  drawBoard(gfx);
  uiDrawButton(gfx, INPUT_BUTTON, "INPUT");
}

void WordleApp::drawBoard(Adafruit_GFX &gfx) {
  char submitted[6] = {0};
  copyCurrentWord(submitted);
  bool showSubmitted = strlen(submitted) == WORD_LENGTH;
  for (int r = 0; r < 6; r++) {
    for (int c = 0; c < 5; c++) {
      int x = BOARD_X + c * (TILE + TILE_GAP);
      int y = BOARD_Y + r * (TILE - 2);
      char letter = guesses[r][c];
      if (r == row && state == STATE_GUESSING && showSubmitted) {
        letter = submitted[c];
      }
      drawTile(gfx, x, y, letter, marks[r][c], r == row);
    }
  }
}

void WordleApp::drawTile(Adafruit_GFX &gfx, int x, int y, char letter,
                         uint8_t mark, bool active) {
  gfx.drawRect(x, y, TILE, TILE - 4, 1);
  if (mark == MARK_CORRECT) {
    gfx.fillRect(x + 1, y + 1, TILE - 2, TILE - 6, 1);
  } else if (mark == MARK_PRESENT) {
    drawDiagonalHatch(gfx, x + 2, y + 2, TILE - 4, TILE - 8, true);
  } else if (active) {
    gfx.drawPixel(x + TILE - 3, y + TILE - 7, 1);
  }

  if (!letter) {
    return;
  }
  gfx.setTextSize(1);
  gfx.setTextColor(mark == MARK_CORRECT ? 0 : 1);
  gfx.setTextSize(2);
  gfx.setCursor(x + 8, y + 5);
  gfx.print(letter);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
}

void WordleApp::openKeyboard() {
  inputText = "";
  keyboardMode = KEYBOARD_T9;
  keyboardOpen = true;
  setActiveMenuButtonConsumer(this);
}

void WordleApp::drawKeyboard(Adafruit_GFX &gfx) {
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.draw(gfx, inputText, WORD_LENGTH);
    return;
  }
  inputKeyboard.draw(gfx, inputText, WORD_LENGTH);
}

void WordleApp::submit() {
  char submitted[6] = {0};
  copyCurrentWord(submitted);
  if (strlen(submitted) != 5) {
    return;
  }
  if (state == STATE_ENTER_WORD) {
    beginGuessing(submitted);
    return;
  }
  if (state != STATE_GUESSING) {
    return;
  }
  strncpy(guesses[row], submitted, sizeof(guesses[row]) - 1);
  guesses[row][5] = 0;
  evaluateGuess();
  if (strcmp(guesses[row], target) == 0) {
    state = STATE_WON;
  } else if (row == 5) {
    state = STATE_LOST;
  } else {
    row++;
    inputText = "";
  }
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
}

void WordleApp::copyCurrentWord(char *word) const {
  const char *source = inputText.c_str();
  int write = 0;
  for (int i = 0; source[i] && write < 5; i++) {
    char letter = source[i];
    if (letter >= 'a' && letter <= 'z') {
      letter = letter - 'a' + 'A';
    }
    if (letter >= 'A' && letter <= 'Z') {
      word[write++] = letter;
    }
  }
  word[write] = 0;
}

void WordleApp::evaluateGuess() {
  uint8_t remaining[26] = {0};
  for (int i = 0; i < 5; i++) {
    marks[row][i] = MARK_ABSENT;
    if (guesses[row][i] == target[i]) {
      marks[row][i] = MARK_CORRECT;
    } else {
      remaining[target[i] - 'A']++;
    }
  }

  for (int i = 0; i < 5; i++) {
    if (marks[row][i] == MARK_CORRECT) {
      setKeyMark(guesses[row][i], MARK_CORRECT);
      continue;
    }
    int index = guesses[row][i] - 'A';
    if (remaining[index] > 0) {
      marks[row][i] = MARK_PRESENT;
      remaining[index]--;
      setKeyMark(guesses[row][i], MARK_PRESENT);
    } else {
      setKeyMark(guesses[row][i], MARK_ABSENT);
    }
  }
}

void WordleApp::setKeyMark(char letter, uint8_t mark) {
  int index = letter - 'A';
  if (index < 0 || index >= 26 || keyMarks[index] >= mark) {
    return;
  }
  keyMarks[index] = mark;
}

bool WordleApp::handleKeyboardTouch(const TouchPoint &point) {
  KeyboardEvent event =
      keyboardMode == KEYBOARD_QWERTY_ZOOM
          ? zoomKeyboard.hitTest(point, inputText.length(), WORD_LENGTH)
          : inputKeyboard.hitTest(point, inputText, WORD_LENGTH);
  if (event.action == KEY_NONE) {
    return false;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    if (event.action == KEY_CHAR && inputText.length() < WORD_LENGTH) {
      inputText += event.value;
    } else if (event.action == KEY_SPACE && inputText.length() < WORD_LENGTH) {
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
