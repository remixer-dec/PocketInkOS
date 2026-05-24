#include "sudoku_app.h"
#include "ui_helpers.h"

#include <cstring>

static const uint8_t SOLUTION[81] = {
    5, 3, 4, 6, 7, 8, 9, 1, 2, 6, 7, 2, 1, 9, 5, 3, 4, 8, 1, 9, 8,
    3, 4, 2, 5, 6, 7, 8, 5, 9, 7, 6, 1, 4, 2, 3, 4, 2, 6, 8, 5, 3,
    7, 9, 1, 7, 1, 3, 9, 2, 4, 8, 5, 6, 9, 6, 1, 5, 3, 7, 2, 8, 4,
    2, 8, 7, 4, 1, 9, 6, 3, 5, 3, 4, 5, 2, 8, 6, 1, 7, 9};

static const char *EASY_MASK =
    "110110011"
    "011011010"
    "101001101"
    "010110011"
    "101010101"
    "110011010"
    "101100101"
    "010110110"
    "110011011";

static const char *MEDIUM_MASK =
    "100100011"
    "010010010"
    "001001100"
    "010100001"
    "100010001"
    "100001010"
    "001100100"
    "010010010"
    "110001001";

static const char *HARD_MASK =
    "100000010"
    "010010000"
    "001000100"
    "000100001"
    "100010001"
    "100001000"
    "001000100"
    "000010010"
    "010000001";

static const char *EXTREME_MASK =
    "100000000"
    "000010000"
    "001000000"
    "000100000"
    "000010001"
    "000001000"
    "000000100"
    "010000000"
    "000000001";

static const UiRect EASY_BUTTON = {18, 52, 74, 28};
static const UiRect MEDIUM_BUTTON = {108, 52, 74, 28};
static const UiRect HARD_BUTTON = {18, 92, 74, 28};
static const UiRect EXTREME_BUTTON = {108, 92, 74, 28};
static const UiRect PICKER_RECT = {45, 38, 110, 124};
static const UiRect INPUT_BUTTON = {53, 138, 46, 16};
static const UiRect GUESS_BUTTON = {101, 138, 46, 16};

static const int BOARD_X = 10;
static const int BOARD_Y = 16;
static const int CELL = 20;
static const int PICKER_GRID_X = 56;
static const int PICKER_GRID_Y = 48;
static const int PICKER_CELL = 28;

void SudokuApp::reset() {
  state = STATE_DIFFICULTY;
  selected = -1;
  pickerOpen = false;
  guessMode = false;
}

void SudokuApp::draw(Adafruit_GFX &gfx) {
  if (state == STATE_DIFFICULTY) {
    drawDifficulty(gfx);
    return;
  }
  drawBoard(gfx);
  if (pickerOpen) {
    drawPicker(gfx);
  }
}

bool SudokuApp::handleTouch(const TouchPoint &point) {
  if (state == STATE_DIFFICULTY) {
    if (uiContains(EASY_BUTTON, point)) {
      start(EASY);
      return true;
    }
    if (uiContains(MEDIUM_BUTTON, point)) {
      start(MEDIUM);
      return true;
    }
    if (uiContains(HARD_BUTTON, point)) {
      start(HARD);
      return true;
    }
    if (uiContains(EXTREME_BUTTON, point)) {
      start(EXTREME);
      return true;
    }
    return false;
  }

  if (pickerOpen) {
    if (uiContains(INPUT_BUTTON, point)) {
      guessMode = false;
      return true;
    }
    if (uiContains(GUESS_BUTTON, point)) {
      guessMode = true;
      return true;
    }
    int number = pickerNumberAt(point);
    if (number > 0) {
      applyNumber(number);
      return true;
    }
    if (!uiContains(PICKER_RECT, point)) {
      pickerOpen = false;
      return true;
    }
    return false;
  }

  int cell = boardCellAt(point);
  if (cell < 0) {
    return false;
  }
  selected = cell;
  pickerOpen = fixed[cell] == 0;
  return true;
}

void SudokuApp::start(Difficulty nextDifficulty) {
  difficulty = nextDifficulty;
  const char *mask = EASY_MASK;
  if (difficulty == MEDIUM) {
    mask = MEDIUM_MASK;
  } else if (difficulty == HARD) {
    mask = HARD_MASK;
  } else if (difficulty == EXTREME) {
    mask = EXTREME_MASK;
  }

  for (int i = 0; i < 81; i++) {
    fixed[i] = mask[i] == '1' ? SOLUTION[i] : 0;
    values[i] = fixed[i];
    notes[i] = 0;
  }
  selected = -1;
  pickerOpen = false;
  guessMode = false;
  state = STATE_PLAYING;
}

void SudokuApp::drawDifficulty(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(56, 18);
  gfx.print("SUDOKU");

  gfx.setTextSize(1);
  gfx.setCursor(35, 38);
  gfx.print("Select difficulty");
  uiDrawButton(gfx, EASY_BUTTON, "EASY");
  uiDrawButton(gfx, MEDIUM_BUTTON, "MEDIUM");
  uiDrawButton(gfx, HARD_BUTTON, "HARD");
  uiDrawButton(gfx, EXTREME_BUTTON, "EXTREME");
}

void SudokuApp::drawBoard(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  if (isSolved()) {
    gfx.setCursor(82, 4);
    gfx.print("SOLVED");
  }

  for (int i = 0; i <= 9; i++) {
    int p = BOARD_X + i * CELL;
    gfx.drawLine(p, BOARD_Y, p, BOARD_Y + 9 * CELL, 1);
    gfx.drawLine(BOARD_X, BOARD_Y + i * CELL, BOARD_X + 9 * CELL,
                 BOARD_Y + i * CELL, 1);
    if (i % 3 == 0) {
      gfx.drawLine(p + 1, BOARD_Y, p + 1, BOARD_Y + 9 * CELL, 1);
      gfx.drawLine(BOARD_X, BOARD_Y + i * CELL + 1, BOARD_X + 9 * CELL,
                   BOARD_Y + i * CELL + 1, 1);
    }
  }

  if (selected >= 0) {
    int x = BOARD_X + (selected % 9) * CELL;
    int y = BOARD_Y + (selected / 9) * CELL;
    gfx.drawRect(x + 2, y + 2, CELL - 4, CELL - 4, 1);
  }

  for (int cell = 0; cell < 81; cell++) {
    int x = BOARD_X + (cell % 9) * CELL;
    int y = BOARD_Y + (cell / 9) * CELL;
    drawCellValue(gfx, cell, x, y);
  }
}

void SudokuApp::drawCellValue(Adafruit_GFX &gfx, int cell, int x, int y) {
  if (values[cell] > 0) {
    gfx.setTextSize(2);
    gfx.setCursor(x + 5, y + 3);
    gfx.print(values[cell]);
    if (fixed[cell] > 0) {
      gfx.setCursor(x + 6, y + 3);
      gfx.print(values[cell]);
    }
    return;
  }
  if (notes[cell]) {
    drawNotes(gfx, notes[cell], x, y);
  }
}

void SudokuApp::drawNotes(Adafruit_GFX &gfx, uint16_t mask, int x, int y) {
  gfx.setTextSize(1);
  for (int n = 1; n <= 9; n++) {
    if ((mask & (1 << n)) == 0) {
      continue;
    }
    int noteIndex = n - 1;
    int col = noteIndex % 3;
    int row = noteIndex / 3;
    gfx.setCursor(x + 2 + col * 6, y + 1 + row * 6);
    gfx.print(n);
  }
}

void SudokuApp::drawPicker(Adafruit_GFX &gfx) {
  gfx.fillRect(PICKER_RECT.x, PICKER_RECT.y, PICKER_RECT.w, PICKER_RECT.h, 0);
  gfx.drawRect(PICKER_RECT.x, PICKER_RECT.y, PICKER_RECT.w, PICKER_RECT.h, 1);

  gfx.setTextSize(2);
  for (int n = 1; n <= 9; n++) {
    int index = n - 1;
    int col = index % 3;
    int row = index / 3;
    int x = PICKER_GRID_X + col * PICKER_CELL;
    int y = PICKER_GRID_Y + row * PICKER_CELL;
    gfx.drawRect(x, y, PICKER_CELL, PICKER_CELL, 1);
    gfx.setCursor(x + 9, y + 6);
    gfx.print(n);
  }

  uiDrawButton(gfx, INPUT_BUTTON, "INPUT", !guessMode);
  uiDrawButton(gfx, GUESS_BUTTON, "GUESS", guessMode);
}

void SudokuApp::applyNumber(uint8_t number) {
  if (selected < 0 || fixed[selected] > 0) {
    return;
  }
  if (guessMode) {
    toggleNote(selected, number);
  } else {
    values[selected] = number;
    notes[selected] = 0;
  }
  pickerOpen = false;
}

void SudokuApp::toggleNote(int cell, uint8_t number) {
  uint16_t bit = 1 << number;
  values[cell] = 0;
  if (notes[cell] & bit) {
    notes[cell] &= ~bit;
  } else {
    notes[cell] |= bit;
  }
}

bool SudokuApp::isSolved() const {
  for (int i = 0; i < 81; i++) {
    if (values[i] != SOLUTION[i]) {
      return false;
    }
  }
  return true;
}

bool SudokuApp::hasActiveSession() const {
  return state == STATE_PLAYING && !isSolved();
}

int SudokuApp::boardCellAt(const TouchPoint &point) const {
  if (point.x < BOARD_X || point.x >= BOARD_X + 9 * CELL ||
      point.y < BOARD_Y || point.y >= BOARD_Y + 9 * CELL) {
    return -1;
  }
  int col = (point.x - BOARD_X) / CELL;
  int row = (point.y - BOARD_Y) / CELL;
  return row * 9 + col;
}

int SudokuApp::pickerNumberAt(const TouchPoint &point) const {
  if (point.x < PICKER_GRID_X || point.x >= PICKER_GRID_X + 3 * PICKER_CELL ||
      point.y < PICKER_GRID_Y || point.y >= PICKER_GRID_Y + 3 * PICKER_CELL) {
    return 0;
  }
  int col = (point.x - PICKER_GRID_X) / PICKER_CELL;
  int row = (point.y - PICKER_GRID_Y) / PICKER_CELL;
  return row * 3 + col + 1;
}
