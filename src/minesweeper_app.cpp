#include "minesweeper_app.h"

void MinesweeperApp::reset() {
  for (int i = 0; i < W * H; i++) {
    mines[i] = false;
    revealed[i] = false;
    flagged[i] = false;
  }
  const int preset[] = {2, 8, 16, 23};
  for (int i = 0; i < 4; i++) {
    mines[preset[i]] = true;
  }
  flagMode = false;
  lost = false;
  won = false;
}

void MinesweeperApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(22, 8);
  gfx.print("MINES");

  int originX = 25;
  int originY = 36;
  int cell = 30;
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

  gfx.drawRect(16, 188, 76, 12, 1);
  gfx.drawRect(108, 188, 76, 12, 1);
  if (!flagMode) gfx.fillRect(17, 189, 74, 10, 1);
  if (flagMode) gfx.fillRect(109, 189, 74, 10, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(flagMode ? 1 : 0);
  gfx.setCursor(40, 191);
  gfx.print("OPEN");
  gfx.setTextColor(flagMode ? 0 : 1);
  gfx.setCursor(132, 191);
  gfx.print("FLAG");
  gfx.setTextColor(1);

  gfx.setCursor(116, 16);
  if (lost) gfx.print("BOOM");
  else if (won) gfx.print("CLEAR");
  else gfx.print(flagMode ? "FLAG" : "OPEN");
}

bool MinesweeperApp::handleTouch(const TouchPoint &point) {
  if (point.y >= 188) {
    if (point.x >= 16 && point.x < 92) {
      flagMode = false;
      return true;
    }
    if (point.x >= 108 && point.x < 184) {
      flagMode = true;
      return true;
    }
  }

  if (lost || won) {
    reset();
    return true;
  }

  int x = (point.x - 25) / 30;
  int y = (point.y - 36) / 30;
  if (point.x < 25 || point.x >= 175 || point.y < 36 || point.y >= 186 ||
      x < 0 || x >= W || y < 0 || y >= H) {
    return false;
  }

  int i = idx(x, y);
  if (flagMode) {
    if (!revealed[i]) flagged[i] = !flagged[i];
  } else if (!flagged[i]) {
    reveal(x, y);
  }
  checkWin();
  return true;
}

int MinesweeperApp::neighborMines(int x, int y) const {
  int count = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;
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
  if (revealed[i] || flagged[i]) return;
  revealed[i] = true;
  if (mines[i]) {
    lost = true;
    for (int j = 0; j < W * H; j++) {
      if (mines[j]) revealed[j] = true;
    }
    return;
  }
  if (neighborMines(x, y) != 0) return;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int nx = x + dx;
      int ny = y + dy;
      if (nx >= 0 && nx < W && ny >= 0 && ny < H) reveal(nx, ny);
    }
  }
}

void MinesweeperApp::checkWin() {
  if (lost) return;
  for (int i = 0; i < W * H; i++) {
    if (!mines[i] && !revealed[i]) return;
  }
  won = true;
}
