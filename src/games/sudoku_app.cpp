#include "games/sudoku_app.h"
#include "sys/rtc_context.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cstring>

static const uint8_t SOLUTION[81] = {
    5, 3, 4, 6, 7, 8, 9, 1, 2, 6, 7, 2, 1, 9, 5, 3, 4, 8, 1, 9, 8,
    3, 4, 2, 5, 6, 7, 8, 5, 9, 7, 6, 1, 4, 2, 3, 4, 2, 6, 8, 5, 3,
    7, 9, 1, 7, 1, 3, 9, 2, 4, 8, 5, 6, 9, 6, 1, 5, 3, 7, 2, 8, 4,
    2, 8, 7, 4, 1, 9, 6, 3, 5, 3, 4, 5, 2, 8, 6, 1, 7, 9};

namespace {
static const uint16_t ALL_DIGITS_MASK = 0x3FE; // bits 1..9

struct DifficultyConfig {
  uint8_t minClues;
  uint8_t maxClues;
  uint8_t minScore;
  uint8_t maxScore;
};

struct SolveStats {
  bool solved;
  int passes;
  int nakedSingles;
  int hiddenSingles;
};

static void shuffleCells(int cells[81]) {
  for (int i = 0; i < 81; i++) {
    cells[i] = i;
  }
  for (int i = 80; i > 0; i--) {
    int j = random(i + 1);
    int tmp = cells[i];
    cells[i] = cells[j];
    cells[j] = tmp;
  }
}

static int countBits(uint16_t mask) {
  int count = 0;
  while (mask) {
    mask &= (mask - 1);
    count++;
  }
  return count;
}

static uint16_t candidatesForCell(const uint8_t board[81], int cell) {
  if (board[cell] > 0) {
    return 0;
  }
  int row = cell / 9;
  int col = cell % 9;
  uint16_t used = 0;

  for (int i = 0; i < 9; i++) {
    uint8_t rowValue = board[row * 9 + i];
    if (rowValue > 0) {
      used |= (1 << rowValue);
    }
    uint8_t colValue = board[i * 9 + col];
    if (colValue > 0) {
      used |= (1 << colValue);
    }
  }

  int boxRow = (row / 3) * 3;
  int boxCol = (col / 3) * 3;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      uint8_t value = board[(boxRow + r) * 9 + boxCol + c];
      if (value > 0) {
        used |= (1 << value);
      }
    }
  }

  return ALL_DIGITS_MASK & ~used;
}

static bool allFilled(const uint8_t board[81]) {
  for (int i = 0; i < 81; i++) {
    if (board[i] == 0) {
      return false;
    }
  }
  return true;
}

static int selectBestCell(const uint8_t board[81], uint16_t &maskOut) {
  int bestCell = -1;
  int bestCount = 10;
  maskOut = 0;
  for (int cell = 0; cell < 81; cell++) {
    if (board[cell] > 0) {
      continue;
    }
    uint16_t mask = candidatesForCell(board, cell);
    int options = countBits(mask);
    if (options == 0) {
      maskOut = 0;
      return -2;
    }
    if (options < bestCount) {
      bestCount = options;
      bestCell = cell;
      maskOut = mask;
      if (bestCount == 1) {
        break;
      }
    }
  }
  return bestCell;
}

static int countSolutions(uint8_t board[81], int limit) {
  if (allFilled(board)) {
    return 1;
  }

  uint16_t mask = 0;
  int cell = selectBestCell(board, mask);
  if (cell == -2) {
    return 0;
  }
  if (cell < 0) {
    return 1;
  }

  int solutions = 0;
  for (uint8_t digit = 1; digit <= 9; digit++) {
    if ((mask & (1 << digit)) == 0) {
      continue;
    }
    board[cell] = digit;
    solutions += countSolutions(board, limit - solutions);
    board[cell] = 0;
    if (solutions >= limit) {
      return solutions;
    }
  }
  return solutions;
}

static bool hasUniqueSolution(const uint8_t board[81]) {
  uint8_t working[81];
  memcpy(working, board, sizeof(working));
  return countSolutions(working, 2) == 1;
}

static bool applyHiddenSingles(uint8_t board[81], const uint16_t candidates[81],
                               int &placedCount) {
  placedCount = 0;
  uint8_t placements[81] = {0};

  for (int row = 0; row < 9; row++) {
    for (uint8_t digit = 1; digit <= 9; digit++) {
      int onlyCell = -1;
      for (int col = 0; col < 9; col++) {
        int cell = row * 9 + col;
        if ((candidates[cell] & (1 << digit)) == 0) {
          continue;
        }
        if (onlyCell >= 0) {
          onlyCell = -2;
          break;
        }
        onlyCell = cell;
      }
      if (onlyCell >= 0) {
        if (placements[onlyCell] == 0) {
          placements[onlyCell] = digit;
        }
      }
    }
  }

  for (int col = 0; col < 9; col++) {
    for (uint8_t digit = 1; digit <= 9; digit++) {
      int onlyCell = -1;
      for (int row = 0; row < 9; row++) {
        int cell = row * 9 + col;
        if ((candidates[cell] & (1 << digit)) == 0) {
          continue;
        }
        if (onlyCell >= 0) {
          onlyCell = -2;
          break;
        }
        onlyCell = cell;
      }
      if (onlyCell >= 0) {
        if (placements[onlyCell] == 0) {
          placements[onlyCell] = digit;
        }
      }
    }
  }

  for (int box = 0; box < 9; box++) {
    int boxRow = (box / 3) * 3;
    int boxCol = (box % 3) * 3;
    for (uint8_t digit = 1; digit <= 9; digit++) {
      int onlyCell = -1;
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
          int cell = (boxRow + r) * 9 + boxCol + c;
          if ((candidates[cell] & (1 << digit)) == 0) {
            continue;
          }
          if (onlyCell >= 0) {
            onlyCell = -2;
            r = 3;
            break;
          }
          onlyCell = cell;
        }
      }
      if (onlyCell >= 0) {
        if (placements[onlyCell] == 0) {
          placements[onlyCell] = digit;
        }
      }
    }
  }

  for (int cell = 0; cell < 81; cell++) {
    if (placements[cell] == 0 || board[cell] > 0) {
      continue;
    }
    board[cell] = placements[cell];
    placedCount++;
  }

  return placedCount > 0;
}

static SolveStats solveWithHumanLogic(const uint8_t puzzle[81]) {
  uint8_t board[81];
  memcpy(board, puzzle, sizeof(board));

  SolveStats stats = {false, 0, 0, 0};
  for (int pass = 0; pass < 128; pass++) {
    uint16_t candidates[81] = {0};
    bool progress = false;

    for (int cell = 0; cell < 81; cell++) {
      if (board[cell] > 0) {
        continue;
      }
      candidates[cell] = candidatesForCell(board, cell);
      int options = countBits(candidates[cell]);
      if (options == 0) {
        return stats;
      }
      if (options == 1) {
        for (uint8_t digit = 1; digit <= 9; digit++) {
          if (candidates[cell] & (1 << digit)) {
            board[cell] = digit;
            stats.nakedSingles++;
            progress = true;
            break;
          }
        }
      }
    }

    if (progress) {
      stats.passes++;
      if (allFilled(board)) {
        stats.solved = true;
        return stats;
      }
      continue;
    }

    int hiddenPlaced = 0;
    if (applyHiddenSingles(board, candidates, hiddenPlaced)) {
      stats.hiddenSingles += hiddenPlaced;
      stats.passes++;
      if (allFilled(board)) {
        stats.solved = true;
        return stats;
      }
      continue;
    }
    break;
  }

  stats.solved = allFilled(board);
  return stats;
}

static int difficultyScore(const SolveStats &stats, int clues) {
  int openness = 81 - clues;
  return stats.hiddenSingles * 3 + stats.passes + openness;
}

static bool hasQuadrantVariety(const uint8_t puzzle[81]) {
  int counts[9] = {0};
  for (int box = 0; box < 9; box++) {
    int boxRow = (box / 3) * 3;
    int boxCol = (box % 3) * 3;
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 3; c++) {
        if (puzzle[(boxRow + r) * 9 + boxCol + c] > 0) {
          counts[box]++;
        }
      }
    }
  }

  int minCount = counts[0];
  int maxCount = counts[0];
  bool seen[10] = {false};
  for (int i = 0; i < 9; i++) {
    if (counts[i] < minCount) {
      minCount = counts[i];
    }
    if (counts[i] > maxCount) {
      maxCount = counts[i];
    }
    if (counts[i] >= 0 && counts[i] <= 9) {
      seen[counts[i]] = true;
    }
  }

  int distinct = 0;
  for (int i = 0; i <= 9; i++) {
    if (seen[i]) {
      distinct++;
    }
  }

  return maxCount - minCount >= 2 && distinct >= 3 && minCount >= 2;
}
} // namespace

static const UiRect EASY_BUTTON = {18, 52, 74, 28};
static const UiRect MEDIUM_BUTTON = {108, 52, 74, 28};
static const UiRect HARD_BUTTON = {18, 92, 74, 28};
static const UiRect EXTREME_BUTTON = {108, 92, 74, 28};
static const UiRect PICKER_RECT = {45, 38, 110, 124};
static const UiRect INPUT_BUTTON = {53, 138, 46, 16};
static const UiRect GUESS_BUTTON = {101, 138, 46, 16};

static const int BOARD_X = 10;
static const int BOARD_Y = 10;
static const int CELL = 20;
static const int PICKER_GRID_X = 56;
static const int PICKER_GRID_Y = 48;
static const int PICKER_CELL = 28;
static const uint8_t SUDOKU_CONTEXT_VERSION = 1;

static bool writeSudokuDigits(RtcBitWriter &writer, const uint8_t *digits) {
  for (int i = 0; i < 81; i++) {
    if (digits[i] > 9) {
      return false;
    }
    writer.writeBits(digits[i], 4);
  }
  return writer.ok();
}

static bool readSudokuDigits(RtcBitReader &reader, uint8_t *digits) {
  uint32_t value = 0;
  for (int i = 0; i < 81; i++) {
    if (!reader.readBits(4, value) || value > 9) {
      return false;
    }
    digits[i] = static_cast<uint8_t>(value);
  }
  return true;
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
  randomSeed((unsigned long)micros());
  bool generated = false;
  for (int attempt = 0; attempt < 8; attempt++) {
    generateSolution();
    if (generatePuzzle()) {
      generated = true;
      break;
    }
  }
  if (!generated) {
    memcpy(fixed, solution, sizeof(fixed));
  }

  for (int i = 0; i < 81; i++) {
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
  drawCenteredText(gfx, "SUDOKU", 18, 2);
  drawCenteredText(gfx, "Select difficulty", 38, 1);
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
    gfx.drawRect(x + 1, y + 1, CELL - 2, CELL - 2, 1);
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
    if (values[i] != solution[i]) {
      return false;
    }
  }
  return true;
}

void SudokuApp::generateSolution() {
  memcpy(solution, SOLUTION, sizeof(solution));
  for (int i = 0; i < 12; i++) {
    swapDigits((uint8_t)(random(9) + 1), (uint8_t)(random(9) + 1));
    int band = random(3);
    swapRows(band * 3 + random(3), band * 3 + random(3));
    int stack = random(3);
    swapCols(stack * 3 + random(3), stack * 3 + random(3));
    swapRowBands(random(3), random(3));
    swapColStacks(random(3), random(3));
  }
}

void SudokuApp::swapDigits(uint8_t a, uint8_t b) {
  if (a == b) {
    return;
  }
  for (int i = 0; i < 81; i++) {
    if (solution[i] == a) {
      solution[i] = b;
    } else if (solution[i] == b) {
      solution[i] = a;
    }
  }
}

void SudokuApp::swapRows(int rowA, int rowB) {
  if (rowA == rowB) {
    return;
  }
  for (int col = 0; col < 9; col++) {
    int idxA = rowA * 9 + col;
    int idxB = rowB * 9 + col;
    uint8_t tmp = solution[idxA];
    solution[idxA] = solution[idxB];
    solution[idxB] = tmp;
  }
}

void SudokuApp::swapCols(int colA, int colB) {
  if (colA == colB) {
    return;
  }
  for (int row = 0; row < 9; row++) {
    int idxA = row * 9 + colA;
    int idxB = row * 9 + colB;
    uint8_t tmp = solution[idxA];
    solution[idxA] = solution[idxB];
    solution[idxB] = tmp;
  }
}

void SudokuApp::swapRowBands(int bandA, int bandB) {
  if (bandA == bandB) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    swapRows(bandA * 3 + i, bandB * 3 + i);
  }
}

void SudokuApp::swapColStacks(int stackA, int stackB) {
  if (stackA == stackB) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    swapCols(stackA * 3 + i, stackB * 3 + i);
  }
}

bool SudokuApp::generatePuzzle() {
  DifficultyConfig config = {28, 33, 42, 92};
  if (difficulty == EASY) {
    config = {40, 46, 0, 30};
  } else if (difficulty == MEDIUM) {
    config = {35, 40, 16, 46};
  } else if (difficulty == HARD) {
    config = {31, 36, 30, 64};
  }

  uint8_t fallbackPuzzle[81] = {0};
  bool hasFallback = false;
  int bestScoreDelta = 1000000;

  for (int attempt = 0; attempt < 140; attempt++) {
    uint8_t puzzle[81];
    memcpy(puzzle, solution, sizeof(puzzle));

    int cluesTarget =
        config.minClues + random(config.maxClues - config.minClues + 1);
    int clues = 81;
    int cellOrder[81];
    shuffleCells(cellOrder);

    for (int i = 0; i < 81 && clues > cluesTarget; i++) {
      int cell = cellOrder[i];
      uint8_t backup = puzzle[cell];
      puzzle[cell] = 0;
      if (!hasUniqueSolution(puzzle)) {
        puzzle[cell] = backup;
        continue;
      }
      clues--;
    }

    if (clues < config.minClues || clues > config.maxClues) {
      continue;
    }

    SolveStats stats = solveWithHumanLogic(puzzle);
    if (!stats.solved) {
      continue;
    }

    int score = difficultyScore(stats, clues);
    int scoreDelta = 0;
    if (score < config.minScore) {
      scoreDelta = config.minScore - score;
    } else if (score > config.maxScore) {
      scoreDelta = score - config.maxScore;
    }

    bool variedQuadrants = hasQuadrantVariety(puzzle);
    if (scoreDelta == 0 && variedQuadrants) {
      memcpy(fixed, puzzle, sizeof(fixed));
      return true;
    }

    if (scoreDelta < bestScoreDelta) {
      memcpy(fallbackPuzzle, puzzle, sizeof(fallbackPuzzle));
      bestScoreDelta = scoreDelta;
      hasFallback = true;
    }
  }

  if (hasFallback) {
    memcpy(fixed, fallbackPuzzle, sizeof(fixed));
    return true;
  }
  return false;
}

bool SudokuApp::hasActiveSession() const {
  return state == STATE_PLAYING && !isSolved();
}

size_t SudokuApp::saveContext(uint8_t *buffer, size_t capacity) const {
  if (state != STATE_PLAYING) {
    return 0;
  }

  RtcBitWriter writer(buffer, capacity);
  writer.writeBits(SUDOKU_CONTEXT_VERSION, 4);
  writer.writeBits(static_cast<uint8_t>(difficulty), 2);
  writer.writeBits(selected >= 0 ? static_cast<uint8_t>(selected) : 81, 7);
  writer.writeBits(pickerOpen ? 1 : 0, 1);
  writer.writeBits(guessMode ? 1 : 0, 1);
  if (!writeSudokuDigits(writer, solution)) {
    return 0;
  }
  for (int i = 0; i < 81; i++) {
    writer.writeBits(fixed[i] > 0 ? 1 : 0, 1);
  }
  if (!writeSudokuDigits(writer, values)) {
    return 0;
  }
  for (int i = 0; i < 81; i++) {
    writer.writeBits((notes[i] >> 1) & 0x1ff, 9);
  }
  return writer.ok() ? writer.bytesWritten() : 0;
}

void SudokuApp::restoreContext(const uint8_t *buffer, size_t length) {
  RtcBitReader reader(buffer, length);
  uint32_t value = 0;
  if (!reader.readBits(4, value) || value != SUDOKU_CONTEXT_VERSION) {
    return;
  }

  uint32_t savedDifficulty = 0;
  uint32_t savedSelected = 0;
  uint32_t savedPickerOpen = 0;
  uint32_t savedGuessMode = 0;
  if (!reader.readBits(2, savedDifficulty) ||
      !reader.readBits(7, savedSelected) ||
      !reader.readBits(1, savedPickerOpen) ||
      !reader.readBits(1, savedGuessMode) || savedDifficulty > EXTREME ||
      savedSelected > 81) {
    return;
  }

  uint8_t nextSolution[81] = {};
  uint8_t nextFixed[81] = {};
  uint8_t nextValues[81] = {};
  uint16_t nextNotes[81] = {};
  if (!readSudokuDigits(reader, nextSolution)) {
    return;
  }
  for (int i = 0; i < 81; i++) {
    if (!reader.readBits(1, value)) {
      return;
    }
    nextFixed[i] = value ? nextSolution[i] : 0;
  }
  if (!readSudokuDigits(reader, nextValues)) {
    return;
  }
  for (int i = 0; i < 81; i++) {
    if (!reader.readBits(9, value)) {
      return;
    }
    nextNotes[i] = static_cast<uint16_t>(value << 1);
  }
  if (!reader.ok()) {
    return;
  }

  memcpy(solution, nextSolution, sizeof(solution));
  memcpy(fixed, nextFixed, sizeof(fixed));
  memcpy(values, nextValues, sizeof(values));
  memcpy(notes, nextNotes, sizeof(notes));
  difficulty = static_cast<Difficulty>(savedDifficulty);
  selected = savedSelected == 81 ? -1 : static_cast<int>(savedSelected);
  pickerOpen = savedPickerOpen != 0 && selected >= 0 && fixed[selected] == 0;
  guessMode = savedGuessMode != 0;
  state = STATE_PLAYING;
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
