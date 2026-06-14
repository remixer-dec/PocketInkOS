#ifndef SUDOKU_APP_H
#define SUDOKU_APP_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

class SudokuApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;
  size_t saveContext(uint8_t *buffer, size_t capacity) const;
  void restoreContext(const uint8_t *buffer, size_t length);

private:
  enum State { STATE_DIFFICULTY, STATE_PLAYING };
  enum Difficulty { EASY, MEDIUM, HARD, EXTREME };

  State state = STATE_DIFFICULTY;
  Difficulty difficulty = EASY;
  uint8_t solution[81] = {0};
  uint8_t fixed[81] = {0};
  uint8_t values[81] = {0};
  uint16_t notes[81] = {0};
  int selected = -1;
  bool pickerOpen = false;
  bool guessMode = false;

  void start(Difficulty nextDifficulty);
  void drawDifficulty(Adafruit_GFX &gfx);
  void drawBoard(Adafruit_GFX &gfx);
  void drawCellValue(Adafruit_GFX &gfx, int cell, int x, int y);
  void drawNotes(Adafruit_GFX &gfx, uint16_t mask, int x, int y);
  void drawPicker(Adafruit_GFX &gfx);
  void applyNumber(uint8_t number);
  void toggleNote(int cell, uint8_t number);
  void generateSolution();
  void swapDigits(uint8_t a, uint8_t b);
  void swapRows(int rowA, int rowB);
  void swapCols(int colA, int colB);
  void swapRowBands(int bandA, int bandB);
  void swapColStacks(int stackA, int stackB);
  bool generatePuzzle();
  bool isSolved() const;
  int boardCellAt(const TouchPoint &point) const;
  int pickerNumberAt(const TouchPoint &point) const;
};

#endif
