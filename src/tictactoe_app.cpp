#include "tictactoe_app.h"

void TicTacToeApp::reset() {
  vsCpu = true;
  resetBoard();
}

void TicTacToeApp::resetBoard() {
  for (int i = 0; i < 9; i++) {
    board[i] = 0;
  }
  turn = 'X';
  winner = 0;
}

void TicTacToeApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(8, 8);
  gfx.print("TIC TAC TOE");

  gfx.setTextSize(1);
  gfx.drawRect(10, 32, 52, 20, 1);
  gfx.drawRect(70, 32, 52, 20, 1);
  if (vsCpu) {
    gfx.fillRect(11, 33, 50, 18, 1);
    gfx.setTextColor(0);
  }
  gfx.setCursor(25, 39);
  gfx.print("CPU");
  gfx.setTextColor(1);
  if (!vsCpu) {
    gfx.fillRect(71, 33, 50, 18, 1);
    gfx.setTextColor(0);
  }
  gfx.setCursor(86, 39);
  gfx.print("PVP");
  gfx.setTextColor(1);
  gfx.drawRect(130, 32, 56, 20, 1);
  gfx.setCursor(146, 39);
  gfx.print("NEW");

  drawBoard(gfx);

  gfx.setTextSize(1);
  gfx.setCursor(14, 187);
  if (winner == 'D') {
    gfx.print("Draw");
  } else if (winner) {
    gfx.print("Winner: ");
    gfx.print(winner);
  } else {
    gfx.print("Turn: ");
    gfx.print(turn);
  }
}

void TicTacToeApp::drawBoard(Adafruit_GFX &gfx) {
  int x = 35;
  int y = 62;
  int size = 42;
  gfx.drawLine(x + size, y, x + size, y + size * 3, 1);
  gfx.drawLine(x + size * 2, y, x + size * 2, y + size * 3, 1);
  gfx.drawLine(x, y + size, x + size * 3, y + size, 1);
  gfx.drawLine(x, y + size * 2, x + size * 3, y + size * 2, 1);
  gfx.drawRect(x, y, size * 3, size * 3, 1);
  for (int i = 0; i < 9; i++) {
    drawMark(gfx, i, board[i]);
  }
}

void TicTacToeApp::drawMark(Adafruit_GFX &gfx, int cell, char mark) {
  if (!mark) return;
  int size = 42;
  int x = 35 + (cell % 3) * size;
  int y = 62 + (cell / 3) * size;
  if (mark == 'X') {
    gfx.drawLine(x + 10, y + 10, x + size - 10, y + size - 10, 1);
    gfx.drawLine(x + size - 10, y + 10, x + 10, y + size - 10, 1);
  } else {
    gfx.drawCircle(x + size / 2, y + size / 2, 13, 1);
  }
}

bool TicTacToeApp::handleTouch(const TouchPoint &point) {
  if (point.y >= 32 && point.y < 52) {
    if (point.x >= 10 && point.x < 62) {
      vsCpu = true;
      resetBoard();
      return true;
    }
    if (point.x >= 70 && point.x < 122) {
      vsCpu = false;
      resetBoard();
      return true;
    }
    if (point.x >= 130 && point.x < 186) {
      resetBoard();
      return true;
    }
  }

  if (point.x < 35 || point.x >= 161 || point.y < 62 || point.y >= 188) {
    return false;
  }
  int col = (point.x - 35) / 42;
  int row = (point.y - 62) / 42;
  makeMove(row * 3 + col);
  return true;
}

void TicTacToeApp::makeMove(int cell) {
  if (winner || board[cell]) return;
  board[cell] = turn;
  winner = calculateWinner();
  if (!winner && isFull()) winner = 'D';
  if (winner) return;

  turn = (turn == 'X') ? 'O' : 'X';
  if (vsCpu && turn == 'O') {
    cpuMove();
  }
}

void TicTacToeApp::cpuMove() {
  int cell = chooseCpuMove();
  if (cell >= 0) {
    board[cell] = 'O';
  }
  winner = calculateWinner();
  if (!winner && isFull()) winner = 'D';
  if (!winner) turn = 'X';
}

int TicTacToeApp::chooseCpuMove() const {
  int move = findWinningMove('O');
  if (move >= 0) return move;
  move = findWinningMove('X');
  if (move >= 0) return move;
  if (!board[4]) return 4;
  const int order[] = {0, 2, 6, 8, 1, 3, 5, 7};
  for (int i = 0; i < 8; i++) {
    if (!board[order[i]]) return order[i];
  }
  return -1;
}

int TicTacToeApp::findWinningMove(char mark) const {
  const int lines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8},
                           {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
                           {0, 4, 8}, {2, 4, 6}};
  for (int i = 0; i < 8; i++) {
    int count = 0;
    int empty = -1;
    for (int j = 0; j < 3; j++) {
      char value = board[lines[i][j]];
      if (value == mark) count++;
      if (!value) empty = lines[i][j];
    }
    if (count == 2 && empty >= 0) return empty;
  }
  return -1;
}

char TicTacToeApp::calculateWinner() const {
  const int lines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8},
                           {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
                           {0, 4, 8}, {2, 4, 6}};
  for (int i = 0; i < 8; i++) {
    char a = board[lines[i][0]];
    if (a && a == board[lines[i][1]] && a == board[lines[i][2]]) {
      return a;
    }
  }
  return 0;
}

bool TicTacToeApp::isFull() const {
  for (int i = 0; i < 9; i++) {
    if (!board[i]) return false;
  }
  return true;
}
