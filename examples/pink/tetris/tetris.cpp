#include "pink_app_api.h"

namespace {

static const uint8_t BOARD_W = 10;
static const uint8_t BOARD_H = 20;
static const uint8_t CELL = 8;
static const int16_t BOARD_X = 18;
static const int16_t BOARD_Y = 20;
static const uint32_t STATE_MAGIC = 0x54525453UL;

struct TetrisState {
  uint32_t magic;
  uint32_t lastDrop;
  uint32_t rng;
  uint16_t score;
  uint8_t board[BOARD_W * BOARD_H];
  int8_t piece;
  int8_t rotation;
  int8_t x;
  int8_t y;
  uint8_t gameOver;
};

TetrisState *stateFromHost(const PinkHost *host) {
  if (host == nullptr || host->memory == nullptr ||
      host->memorySize < sizeof(TetrisState)) {
    return nullptr;
  }
  return reinterpret_cast<TetrisState *>(host->memory);
}

void copyBytes(uint8_t *dest, const uint8_t *src, size_t length) {
  for (size_t i = 0; i < length; i++) {
    dest[i] = src[i];
  }
}

uint16_t shapeMask(uint8_t piece, uint8_t rotation) {
  const uint8_t r = rotation & 3U;
  switch (piece) {
  case 0:
    return r & 1U ? 0x2222U : 0x00f0U;
  case 1:
    return r == 0 ? 0x8e00U : (r == 1 ? 0x6440U : (r == 2 ? 0x0e20U : 0x44c0U));
  case 2:
    return r == 0 ? 0x2e00U : (r == 1 ? 0x4460U : (r == 2 ? 0x0e80U : 0xc440U));
  case 3:
    return 0x6600U;
  case 4:
    return r & 1U ? 0x4620U : 0x06c0U;
  case 5:
    return r == 0 ? 0x4e00U : (r == 1 ? 0x4640U : (r == 2 ? 0x0e40U : 0x4c40U));
  default:
    return r & 1U ? 0x2640U : 0x0c60U;
  }
}

bool shapeCell(uint8_t piece, uint8_t rotation, uint8_t x, uint8_t y) {
  const uint8_t bit = y * 4U + x;
  return (shapeMask(piece, rotation) & (0x8000U >> bit)) != 0;
}

uint8_t boardIndex(uint8_t x, uint8_t y) { return y * BOARD_W + x; }

uint8_t nextPiece(TetrisState *state) {
  state->rng = state->rng * 1103515245UL + 12345UL;
  uint8_t value = static_cast<uint8_t>((state->rng >> 16) & 0x1fU);
  while (value >= 7U) {
    value -= 7U;
  }
  return value;
}

bool collides(const TetrisState *state, int8_t px, int8_t py, uint8_t rotation) {
  for (uint8_t sy = 0; sy < 4U; sy++) {
    for (uint8_t sx = 0; sx < 4U; sx++) {
      if (!shapeCell(state->piece, rotation, sx, sy)) {
        continue;
      }
      const int8_t bx = px + static_cast<int8_t>(sx);
      const int8_t by = py + static_cast<int8_t>(sy);
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) {
        return true;
      }
      if (by >= 0 && state->board[boardIndex(static_cast<uint8_t>(bx),
                                             static_cast<uint8_t>(by))] != 0) {
        return true;
      }
    }
  }
  return false;
}

void spawnPiece(TetrisState *state) {
  state->piece = nextPiece(state);
  state->rotation = 0;
  state->x = 3;
  state->y = -1;
  if (collides(state, state->x, state->y, state->rotation)) {
    state->gameOver = 1;
  }
}

void clearBoard(TetrisState *state) {
  for (uint16_t i = 0; i < BOARD_W * BOARD_H; i++) {
    state->board[i] = 0;
  }
}

void initGame(TetrisState *state, uint32_t seed) {
  state->magic = STATE_MAGIC;
  state->lastDrop = seed;
  state->rng = seed ^ 0x31415926UL;
  state->score = 0;
  state->gameOver = 0;
  clearBoard(state);
  spawnPiece(state);
}

void lockPiece(TetrisState *state) {
  for (uint8_t sy = 0; sy < 4U; sy++) {
    for (uint8_t sx = 0; sx < 4U; sx++) {
      if (!shapeCell(state->piece, state->rotation, sx, sy)) {
        continue;
      }
      const int8_t bx = state->x + static_cast<int8_t>(sx);
      const int8_t by = state->y + static_cast<int8_t>(sy);
      if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H) {
        state->board[boardIndex(static_cast<uint8_t>(bx),
                                static_cast<uint8_t>(by))] = 1;
      }
    }
  }
}

bool rowFull(const TetrisState *state, uint8_t y) {
  for (uint8_t x = 0; x < BOARD_W; x++) {
    if (state->board[boardIndex(x, y)] == 0) {
      return false;
    }
  }
  return true;
}

void clearRow(TetrisState *state, uint8_t row) {
  for (int8_t y = static_cast<int8_t>(row); y > 0; y--) {
    for (uint8_t x = 0; x < BOARD_W; x++) {
      state->board[boardIndex(x, static_cast<uint8_t>(y))] =
          state->board[boardIndex(x, static_cast<uint8_t>(y - 1))];
    }
  }
  for (uint8_t x = 0; x < BOARD_W; x++) {
    state->board[boardIndex(x, 0)] = 0;
  }
}

void clearLines(TetrisState *state) {
  for (uint8_t y = 0; y < BOARD_H; y++) {
    if (rowFull(state, y)) {
      clearRow(state, y);
      state->score += 10;
    }
  }
}

bool movePiece(TetrisState *state, int8_t dx, int8_t dy) {
  if (state->gameOver != 0) {
    return false;
  }
  const int8_t nx = state->x + dx;
  const int8_t ny = state->y + dy;
  if (collides(state, nx, ny, state->rotation)) {
    if (dy > 0) {
      lockPiece(state);
      state->score++;
      clearLines(state);
      spawnPiece(state);
      return true;
    }
    return false;
  }
  state->x = nx;
  state->y = ny;
  return true;
}

void rotatePiece(TetrisState *state) {
  if (state->gameOver != 0) {
    return;
  }
  const uint8_t next = (state->rotation + 1) & 3U;
  if (!collides(state, state->x, state->y, next)) {
    state->rotation = next;
  }
}

void drawNumber(const PinkHost *host, uint16_t value, int16_t x, int16_t y) {
  char text[6];
  uint8_t out = 0;
  uint16_t place = 10000;
  bool started = false;
  while (place > 0 && out < 5) {
    uint8_t digit = 0;
    while (value >= place) {
      value -= place;
      digit++;
    }
    if (digit != 0 || started || place == 1) {
      text[out++] = static_cast<char>('0' + digit);
      started = true;
    }
    if (place == 10000) {
      place = 1000;
    } else if (place == 1000) {
      place = 100;
    } else if (place == 100) {
      place = 10;
    } else {
      place = 1;
      if (out > 0 && started) {
        break;
      }
    }
  }
  text[out] = '\0';
  host->setCursor(x, y);
  host->print(text);
}

void drawText(const PinkHost *host, const char *text, int16_t x, int16_t y) {
  host->setCursor(x, y);
  host->print(text);
}

void drawGame(const PinkHost *host, const TetrisState *state) {
  host->fillScreen(0);
  host->setFont(PINK_FONT_DEFAULT);
  host->setTextColor(1);
  host->setTextSize(1);

  char title[] = {'T', 'E', 'T', 'R', 'I', 'S', '\0'};
  drawText(host, title, 122, 22);
  host->setFont(PINK_FONT_ICON_12);
  char icon[] = {'2', '\0'};
  drawText(host, icon, 126, 48);
  host->setFont(PINK_FONT_DEFAULT);

  char score[] = {'S', 'C', 'O', 'R', 'E', '\0'};
  drawText(host, score, 122, 84);
  drawNumber(host, state->score, 122, 96);

  host->drawRect(BOARD_X - 1, BOARD_Y - 1, BOARD_W * CELL + 2,
                 BOARD_H * CELL + 2, 1);
  for (uint8_t y = 0; y < BOARD_H; y++) {
    for (uint8_t x = 0; x < BOARD_W; x++) {
      if (state->board[boardIndex(x, y)] != 0) {
        host->fillRect(BOARD_X + x * CELL, BOARD_Y + y * CELL, CELL - 1,
                       CELL - 1, 1);
      }
    }
  }

  if (state->gameOver == 0) {
    for (uint8_t sy = 0; sy < 4U; sy++) {
      for (uint8_t sx = 0; sx < 4U; sx++) {
        if (!shapeCell(state->piece, state->rotation, sx, sy)) {
          continue;
        }
        const int8_t bx = state->x + static_cast<int8_t>(sx);
        const int8_t by = state->y + static_cast<int8_t>(sy);
        if (bx >= 0 && bx < BOARD_W && by >= 0 && by < BOARD_H) {
          host->fillRect(BOARD_X + bx * CELL, BOARD_Y + by * CELL, CELL - 1,
                         CELL - 1, 1);
        }
      }
    }
  } else {
    char over[] = {'G', 'A', 'M', 'E', '\0'};
    char done[] = {'O', 'V', 'E', 'R', '\0'};
    drawText(host, over, 42, 88);
    drawText(host, done, 42, 102);
  }
}

void handleTouch(const PinkHost *host, TetrisState *state, PinkEvent *event) {
  if (event->type != PINK_EVENT_TOUCH_DOWN && event->type != PINK_EVENT_TOUCH) {
    return;
  }
  event->handled = 1;
  event->dirty = 1;
  if (state->gameOver != 0) {
    initGame(state, host->millis());
    return;
  }
  if (event->y < 50U) {
    rotatePiece(state);
    return;
  }
  if (event->y > 150U) {
    movePiece(state, 0, 1);
    return;
  }
  if (event->x < 100U) {
    movePiece(state, -1, 0);
  } else {
    movePiece(state, 1, 0);
  }
}

void handleSave(TetrisState *state, PinkEvent *event) {
  if (event->data == nullptr || event->dataCapacity < sizeof(TetrisState)) {
    event->dataLength = 0;
    return;
  }
  copyBytes(event->data, reinterpret_cast<const uint8_t *>(state),
            sizeof(TetrisState));
  event->dataLength = sizeof(TetrisState);
}

void handleRestore(TetrisState *state, PinkEvent *event) {
  if (event->data == nullptr || event->dataLength != sizeof(TetrisState)) {
    return;
  }
  const TetrisState *saved = reinterpret_cast<const TetrisState *>(event->data);
  if (saved->magic != STATE_MAGIC) {
    return;
  }
  copyBytes(reinterpret_cast<uint8_t *>(state),
            reinterpret_cast<const uint8_t *>(saved), sizeof(TetrisState));
  event->dirty = 1;
}

} // namespace

extern "C" __attribute__((section(".pink_entry"), used)) void
pinkEntry(const PinkHost *host, PinkEvent *event) {
  TetrisState *state = stateFromHost(host);
  if (state == nullptr || event == nullptr) {
    return;
  }

  if (event->type == PINK_EVENT_START) {
    initGame(state, host->millis());
    host->deepSleepPreventAcquire();
    event->dirty = 1;
    return;
  }
  if (event->type == PINK_EVENT_STOP) {
    host->deepSleepPreventRelease();
    return;
  }
  if (state->magic != STATE_MAGIC) {
    initGame(state, host->millis());
  }

  if (event->type == PINK_EVENT_DRAW) {
    drawGame(host, state);
    return;
  }
  if (event->type == PINK_EVENT_UPDATE) {
    host->keepAwake();
    const uint32_t now = host->millis();
    if (now - state->lastDrop > 700UL) {
      state->lastDrop = now;
      movePiece(state, 0, 1);
      event->dirty = 1;
    }
    return;
  }
  if (event->type == PINK_EVENT_SAVE_STATE) {
    handleSave(state, event);
    return;
  }
  if (event->type == PINK_EVENT_RESTORE_STATE) {
    handleRestore(state, event);
    return;
  }
  handleTouch(host, state, event);
}
