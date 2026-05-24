#include "tictactoe_app.h"
#include "ui_helpers.h"

static const int BOARD_X = 25;
static const int BOARD_Y = 50;
static const int CELL_SIZE = 50;
static const UiRect CPU_BUTTON = {10, 28, 52, 18};
static const UiRect PVP_BUTTON = {70, 28, 52, 18};
static const UiRect NEW_BUTTON = {130, 28, 56, 18};

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
  gfx.print("TIC TAC");

  gfx.setTextSize(1);
  gfx.setCursor(154, 8);
  if (winner == 'D') {
    gfx.print("DRAW");
  } else if (winner) {
    gfx.print("WIN ");
    gfx.print(winner);
  } else {
    gfx.print("TURN ");
    gfx.print(turn);
  }

  uiDrawButton(gfx, CPU_BUTTON, "CPU", vsCpu);
  uiDrawButton(gfx, PVP_BUTTON, "PVP", !vsCpu);
  uiDrawButton(gfx, NEW_BUTTON, "NEW");

  drawBoard(gfx);
}

void TicTacToeApp::drawBoard(Adafruit_GFX &gfx) {
  int x = BOARD_X;
  int y = BOARD_Y;
  int size = CELL_SIZE;
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
  if (!mark)
    return;
  int size = CELL_SIZE;
  int x = BOARD_X + (cell % 3) * size;
  int y = BOARD_Y + (cell / 3) * size;
  if (mark == 'X') {
    gfx.drawLine(x + 12, y + 12, x + size - 12, y + size - 12, 1);
    gfx.drawLine(x + size - 12, y + 12, x + 12, y + size - 12, 1);
  } else {
    gfx.drawCircle(x + size / 2, y + size / 2, 16, 1);
  }
}

bool TicTacToeApp::handleTouch(const TouchPoint &point) {
  if (uiContains(CPU_BUTTON, point)) {
    vsCpu = true;
    resetBoard();
    return true;
  }
  if (uiContains(PVP_BUTTON, point)) {
    vsCpu = false;
    resetBoard();
    return true;
  }
  if (uiContains(NEW_BUTTON, point)) {
    resetBoard();
    return true;
  }

  if (point.x < BOARD_X || point.x >= BOARD_X + CELL_SIZE * 3 ||
      point.y < BOARD_Y || point.y >= BOARD_Y + CELL_SIZE * 3) {
    return false;
  }
  int col = (point.x - BOARD_X) / CELL_SIZE;
  int row = (point.y - BOARD_Y) / CELL_SIZE;
  makeMove(row * 3 + col);
  return true;
}

void TicTacToeApp::makeMove(int cell) {
  if (winner || board[cell])
    return;
  board[cell] = turn;
  winner = calculateWinner();
  if (!winner && isFull())
    winner = 'D';
  if (winner)
    return;

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
  if (!winner && isFull())
    winner = 'D';
  if (!winner)
    turn = 'X';
}

int TicTacToeApp::chooseCpuMove() const {
  int move = findWinningMove('O');
  if (move >= 0)
    return move;
  move = findWinningMove('X');
  if (move >= 0)
    return move;
  if (!board[4])
    return 4;
  const int order[] = {0, 2, 6, 8, 1, 3, 5, 7};
  for (int i = 0; i < 8; i++) {
    if (!board[order[i]])
      return order[i];
  }
  return -1;
}

int TicTacToeApp::findWinningMove(char mark) const {
  const int lines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                           {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
  for (int i = 0; i < 8; i++) {
    int count = 0;
    int empty = -1;
    for (int j = 0; j < 3; j++) {
      char value = board[lines[i][j]];
      if (value == mark)
        count++;
      if (!value)
        empty = lines[i][j];
    }
    if (count == 2 && empty >= 0)
      return empty;
  }
  return -1;
}

char TicTacToeApp::calculateWinner() const {
  const int lines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                           {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
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
    if (!board[i])
      return false;
  }
  return true;
}
