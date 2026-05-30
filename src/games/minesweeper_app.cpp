#include "games/minesweeper_app.h"
#include "ui/ui_helpers.h"

static const int MINE_COUNT = 4;
static const int GRID_X = 15;
static const int GRID_Y = 16;
static const int CELL_SIZE = 34;
static const int MODE_Y = 188;
static const UiRect OPEN_BUTTON = {16, MODE_Y, 76, 12};
static const UiRect FLAG_BUTTON = {108, MODE_Y, 76, 12};

void MinesweeperApp::reset() {
  for (int i = 0; i < W * H; i++) {
    mines[i] = false;
    revealed[i] = false;
    flagged[i] = false;
  }
  randomSeed((unsigned long)micros());
  int placed = 0;
  while (placed < MINE_COUNT) {
    int cell = random(W * H);
    if (!mines[cell]) {
      mines[cell] = true;
      placed++;
    }
  }
  flagMode = false;
  started = false;
  lost = false;
  won = false;
}

void MinesweeperApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(GRID_X, 4);
  gfx.print("MINES");
  gfx.setCursor(GRID_X + CELL_SIZE * 3, 4);
  if (lost)
    gfx.print("BOOM");
  else if (won)
    gfx.print("CLEAR");
  else
    gfx.print(flagMode ? "FLAG" : "OPEN");

  int originX = GRID_X;
  int originY = GRID_Y;
  int cell = CELL_SIZE;
  gfx.setTextSize(2);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int i = idx(x, y);
      int px = originX + x * cell;
      int py = originY + y * cell;
      gfx.drawRect(px, py, cell, cell, 1);
      if (revealed[i]) {
        gfx.fillRect(px + 1, py + 1, cell - 2, cell - 2, 1);
        gfx.setTextColor(0);
        if (mines[i]) {
          gfx.setCursor(px + 10, py + 8);
          gfx.print("*");
        } else {
          int n = neighborMines(x, y);
          if (n > 0) {
            gfx.setCursor(px + 10, py + 8);
            gfx.print(n);
          }
        }
        gfx.setTextColor(1);
      } else if (flagged[i]) {
        gfx.setCursor(px + 10, py + 8);
        gfx.print("F");
      }
    }
  }

  uiDrawButton(gfx, OPEN_BUTTON, "OPEN", !flagMode);
  uiDrawButton(gfx, FLAG_BUTTON, "FLAG", flagMode);
}

bool MinesweeperApp::handleTouch(const TouchPoint &point) {
  if (uiContains(OPEN_BUTTON, point)) {
    flagMode = false;
    return true;
  }
  if (uiContains(FLAG_BUTTON, point)) {
    flagMode = true;
    return true;
  }

  if (lost || won) {
    reset();
    return true;
  }

  int x = (point.x - GRID_X) / CELL_SIZE;
  int y = (point.y - GRID_Y) / CELL_SIZE;
  if (point.x < GRID_X || point.x >= GRID_X + W * CELL_SIZE ||
      point.y < GRID_Y || point.y >= GRID_Y + H * CELL_SIZE || x < 0 ||
      x >= W || y < 0 || y >= H) {
    return false;
  }

  int i = idx(x, y);
  started = true;
  if (flagMode) {
    if (!revealed[i])
      flagged[i] = !flagged[i];
  } else if (!flagged[i]) {
    reveal(x, y);
  }
  checkWin();
  return true;
}

bool MinesweeperApp::handlePowerButton() {
  if (lost || won) {
    return false;
  }
  flagMode = !flagMode;
  return true;
}

bool MinesweeperApp::hasActiveSession() const {
  return started && !lost && !won;
}

int MinesweeperApp::neighborMines(int x, int y) const {
  int count = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0)
        continue;
      int nx = x + dx;
      int ny = y + dy;
      if (nx >= 0 && nx < W && ny >= 0 && ny < H && mines[idx(nx, ny)]) {
        count++;
      }
    }
  }
  return count;
}

void MinesweeperApp::reveal(int x, int y) {
  int i = idx(x, y);
  if (revealed[i] || flagged[i])
    return;
  revealed[i] = true;
  if (mines[i]) {
    lost = true;
    for (int j = 0; j < W * H; j++) {
      if (mines[j])
        revealed[j] = true;
    }
    return;
  }
  if (neighborMines(x, y) != 0)
    return;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int nx = x + dx;
      int ny = y + dy;
      if (nx >= 0 && nx < W && ny >= 0 && ny < H)
        reveal(nx, ny);
    }
  }
}

void MinesweeperApp::checkWin() {
  if (lost)
    return;
  for (int i = 0; i < W * H; i++) {
    if (!mines[i] && !revealed[i])
      return;
  }
  won = true;
}
