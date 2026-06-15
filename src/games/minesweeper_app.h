#ifndef MINESWEEPER_APP_H
#define MINESWEEPER_APP_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>

class MinesweeperApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool handlePowerButton();
  bool hasActiveSession() const;
  size_t saveContext(uint8_t *buffer, size_t capacity) const;
  void restoreContext(const uint8_t *buffer, size_t length);

private:
  static const int W = 5;
  static const int H = 5;
  bool mines[W * H] = {false};
  bool revealed[W * H] = {false};
  bool flagged[W * H] = {false};
  bool flagMode = false;
  bool started = false;
  bool lost = false;
  bool won = false;

  int idx(int x, int y) const { return y * W + x; }
  int neighborMines(int x, int y) const;
  void reveal(int x, int y);
  void checkWin();
  void refreshOutcomeFromBoard();
};

#endif
