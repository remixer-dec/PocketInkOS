#include "games/minesweeper_app.h"
#include "ui/ui_helpers.h"

#include <stdint.h>

static const int MINE_COUNT = 4;
static const int GRID_X = 15;
static const int GRID_Y = 16;
static const int CELL_SIZE = 34;
static const int MODE_Y = 188;
static const UiRect OPEN_BUTTON = {16, MODE_Y, 76, 12};
static const UiRect FLAG_BUTTON = {108, MODE_Y, 76, 12};
static const uint8_t MINESWEEPER_CONTEXT_VERSION = 1;

static uint32_t packMask(const bool cells[], int count) {
  uint32_t mask = 0;
  for (int i = 0; i < count; i++) {
    if (cells[i]) {
      mask |= (1UL << i);
    }
  }
  return mask;
}

static void unpackMask(uint32_t mask, bool cells[], int count) {
  for (int i = 0; i < count; i++) {
    cells[i] = (mask & (1UL << i)) != 0;
  }
}

static void writeU32(uint8_t *target, uint32_t value) {
  target[0] = static_cast<uint8_t>(value & 0xff);
  target[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  target[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  target[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

static uint32_t readU32(const uint8_t *source) {
  return static_cast<uint32_t>(source[0]) |
         (static_cast<uint32_t>(source[1]) << 8) |
         (static_cast<uint32_t>(source[2]) << 16) |
         (static_cast<uint32_t>(source[3]) << 24);
}

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

size_t MinesweeperApp::saveContext(uint8_t *buffer, size_t capacity) const {
  if (capacity < 14) {
    return 0;
  }

  buffer[0] = MINESWEEPER_CONTEXT_VERSION;
  buffer[1] = (flagMode ? 1U : 0U) | (started ? 2U : 0U) |
              (lost ? 4U : 0U) | (won ? 8U : 0U);
  writeU32(buffer + 2, packMask(mines, W * H));
  writeU32(buffer + 6, packMask(revealed, W * H));
  writeU32(buffer + 10, packMask(flagged, W * H));
  return 14;
}

void MinesweeperApp::restoreContext(const uint8_t *buffer, size_t length) {
  if (length != 14 || buffer[0] != MINESWEEPER_CONTEXT_VERSION) {
    return;
  }

  flagMode = (buffer[1] & 1U) != 0;
  started = (buffer[1] & 2U) != 0;
  lost = (buffer[1] & 4U) != 0;
  won = (buffer[1] & 8U) != 0;
  unpackMask(readU32(buffer + 2), mines, W * H);
  unpackMask(readU32(buffer + 6), revealed, W * H);
  unpackMask(readU32(buffer + 10), flagged, W * H);
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
