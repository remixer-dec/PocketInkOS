#ifndef TICTACTOE_APP_H
#define TICTACTOE_APP_H

#include "touch_input.h"
#include <Adafruit_GFX.h>

class TicTacToeApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);

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
  int chooseCpuMove() const;
  int findWinningMove(char mark) const;
  char calculateWinner() const;
  bool isFull() const;
};

#endif
