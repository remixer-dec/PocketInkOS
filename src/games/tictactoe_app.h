#ifndef TICTACTOE_APP_H
#define TICTACTOE_APP_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>

class TicTacToeApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;
  size_t saveContext(uint8_t *buffer, size_t capacity) const;
  void restoreContext(const uint8_t *buffer, size_t length);

private:
  char board[9] = {0};
  bool vsCpu = true;
  char turn = 'X';
  char winner = 0;

  void resetBoard();
  void drawBoard(Adafruit_GFX &gfx);
  void drawMark(Adafruit_GFX &gfx, int cell, char mark);
  void makeMove(int cell);
  void cpuMove();
  int chooseCpuMove();
  int findWinningMove(char mark) const;
  int chooseRandomEmptyCell() const;
  bool isFirstCpuResponse() const;
  char calculateWinner() const;
  bool isFull() const;
};

#endif
