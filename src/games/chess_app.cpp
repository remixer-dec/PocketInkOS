#include "games/chess_app.h"
#include "games/chess_fonts.h"
#include "sys/rtc_context.h"
#include "ui/ui_helpers.h"
#include <Arduino.h>
#include <cstring>

static const int BOARD_X = 8;
static const int BOARD_Y = 8;
static const int BOARD_SIZE = 184;
static const int CELL = BOARD_SIZE / 8;
static const int BOARD_TOUCH_ASSIST = CELL / 2;
static const int DISPLAY_WIDTH = 200;
static const int DISPLAY_HEIGHT = 200;
static const int AI_MOVE_LIMIT = 96;
static const unsigned long AI3_TIME_BUDGET_MS = 2200;
static const uint8_t AI3_MAX_DEPTH = 5;
static const uint8_t AI3_BOOK_PLY_LIMIT = 8;
static const int AI_MATE_SCORE = 30000;
static const int AI_INF_SCORE = 32000;
static const uint8_t CHESS_CONTEXT_VERSION = 1;
static const uint8_t CHESS_MAX_HISTORY_TOKENS = 192;

static const UiRect PVP_BUTTON = {8, 54, 42, 28};
static const UiRect AI1_BUTTON = {56, 54, 42, 28};
static const UiRect AI2_BUTTON = {104, 54, 42, 28};
static const UiRect AI3_BUTTON = {152, 54, 42, 28};
static const UiRect COLOR_BUTTON = {48, 118, 104, 30};
static const UiRect START_BUTTON = {48, 156, 104, 30};
static const UiRect PROMOTION_BUTTONS[4] = {{28, 94, 30, 24},
                                            {68, 94, 30, 24},
                                            {108, 94, 30, 24},
                                            {148, 94, 30, 24}};
static const PieceType PROMOTION_TYPES[4] = {
    PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};

static uint8_t chessPromotionCode(PieceType type) {
  switch (type) {
  case PieceType::Rook:
    return 1;
  case PieceType::Bishop:
    return 2;
  case PieceType::Knight:
    return 3;
  case PieceType::Queen:
  default:
    return 0;
  }
}

static PieceType chessPromotionFromCode(uint8_t code) {
  switch (code) {
  case 1:
    return PieceType::Rook;
  case 2:
    return PieceType::Bishop;
  case 3:
    return PieceType::Knight;
  case 0:
  default:
    return PieceType::Queen;
  }
}

static PieceType chessPromotionFromChar(char c) {
  switch (c) {
  case 'R':
    return PieceType::Rook;
  case 'B':
    return PieceType::Bishop;
  case 'N':
    return PieceType::Knight;
  case 'Q':
  default:
    return PieceType::Queen;
  }
}

static char chessPromotionChar(const char *historyEntry) {
  if (historyEntry == nullptr || historyEntry[0] != 'P') {
    return '\0';
  }
  for (int i = 6; historyEntry[i]; i++) {
    if (historyEntry[i] == 'Q' || historyEntry[i] == 'R' ||
        historyEntry[i] == 'B' || historyEntry[i] == 'N') {
      return historyEntry[i];
    }
  }
  return '\0';
}

static constexpr int bookSquare(char file, char rank) {
  return ('8' - rank) * 8 + (file - 'A');
}

struct BookPly {
  int8_t from;
  int8_t to;
  PieceType promotion;
  bool hasPromotion;
};

static const char *AI3_OPENINGS[] = {
    // Ruy Lopez
    "E2E4 E7E5 G1F3 B8C6 F1B5 A7A6 B5A4 G8F6",
    // Italian Game
    "E2E4 E7E5 G1F3 B8C6 F1C4 F8C5 C2C3 G8F6",
    // Sicilian Defense
    "E2E4 C7C5 G1F3 D7D6 D2D4 C5D4 F3D4 G8F6",
    // French Defense
    "E2E4 E7E6 D2D4 D7D5 B1C3 G8F6 C1G5 F8E7",
    // Caro-Kann Defense
    "E2E4 C7C6 D2D4 D7D5 B1C3 D5E4 C3E4 B8D7",
    // Queen's Gambit Declined
    "D2D4 D7D5 C2C4 E7E6 B1C3 G8F6 C1G5 F8E7",
    // King's Indian Defense
    "D2D4 G8F6 C2C4 G7G6 B1C3 F8G7 E2E4 D7D6",
    // English Opening
    "C2C4 E7E5 B1C3 G8F6 G2G3 D7D5 C4D5 F6D5"};

static char asciiUpper(char c) {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

static bool parseBookMoveToken(const char *line, uint8_t ply, BookPly &out) {
  if (!line) {
    return false;
  }
  uint8_t index = 0;
  const char *p = line;
  while (*p) {
    while (*p == ' ') {
      p++;
    }
    if (!*p) {
      break;
    }
    const char *token = p;
    int len = 0;
    while (*p && *p != ' ') {
      p++;
      len++;
    }
    if (index != ply) {
      index++;
      continue;
    }
    if (len < 4 || len > 5) {
      return false;
    }

    char fromFile = asciiUpper(token[0]);
    char fromRank = token[1];
    char toFile = asciiUpper(token[2]);
    char toRank = token[3];
    if (fromFile < 'A' || fromFile > 'H' || toFile < 'A' || toFile > 'H' ||
        fromRank < '1' || fromRank > '8' || toRank < '1' || toRank > '8') {
      return false;
    }

    out.from = static_cast<int8_t>(bookSquare(fromFile, fromRank));
    out.to = static_cast<int8_t>(bookSquare(toFile, toRank));
    out.promotion = PieceType::Queen;
    out.hasPromotion = false;
    if (len == 5) {
      char promo = asciiUpper(token[4]);
      if (promo == 'Q') {
        out.promotion = PieceType::Queen;
      } else if (promo == 'R') {
        out.promotion = PieceType::Rook;
      } else if (promo == 'B') {
        out.promotion = PieceType::Bishop;
      } else if (promo == 'N') {
        out.promotion = PieceType::Knight;
      } else {
        return false;
      }
      out.hasPromotion = true;
    }
    return true;
  }
  return false;
}

static void drawCenteredText(Adafruit_GFX &gfx, const char *text, int y,
                             uint8_t textSize) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.setTextSize(textSize);
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void ChessApp::reset() {
  placeInitialPieces();
  resetGameState();
  mode = Mode::Setup;
}

void ChessApp::resetGameState() {
  turn = PieceColor::White;
  humanColor = PieceColor::White;
  vsAi = false;
  aiLevel = 1;
  aiThinking = false;
  aiThinkingSinceMs = 0;
  aiBookEnabled = false;
  menuCancelBlockUntilMs = 0;
  enPassantSquare = -1;
  whiteKingMoved = false;
  blackKingMoved = false;
  whiteKingsideRookMoved = false;
  whiteQueensideRookMoved = false;
  blackKingsideRookMoved = false;
  blackQueensideRookMoved = false;
  gameOver = false;
  started = false;
  alertVisible = false;
  hintText[0] = '\0';
  historyCount = 0;
  historyOffset = 0;
  historyReplayable = true;
  hasLastMove = false;
  lastMoveFrom = -1;
  lastMoveTo = -1;
  clearSelection();
}

void ChessApp::placeInitialPieces() {
  for (int i = 0; i < 64; ++i) {
    board[i] = {};
  }

  const PieceType backRank[8] = {PieceType::Rook,   PieceType::Knight,
                                 PieceType::Bishop, PieceType::Queen,
                                 PieceType::King,   PieceType::Bishop,
                                 PieceType::Knight, PieceType::Rook};

  for (int col = 0; col < 8; ++col) {
    board[col] = {backRank[col], PieceColor::Black, true};
    board[8 + col] = {PieceType::Pawn, PieceColor::Black, true};
    board[48 + col] = {PieceType::Pawn, PieceColor::White, true};
    board[56 + col] = {backRank[col], PieceColor::White, true};
  }
}

bool ChessApp::loadPosition(const char *fen, PieceColor sideToMove) {
  if (!fen) {
    return false;
  }

  Piece nextBoard[64] = {};
  int row = 0;
  int col = 0;
  for (const char *p = fen; *p && *p != ' '; ++p) {
    if (*p == '/') {
      if (col != 8) {
        return false;
      }
      row++;
      col = 0;
      continue;
    }
    if (*p >= '1' && *p <= '8') {
      col += *p - '0';
      if (col > 8) {
        return false;
      }
      continue;
    }
    if (row >= 8 || col >= 8) {
      return false;
    }
    Piece piece = {};
    piece.present = true;
    piece.color = (*p >= 'a' && *p <= 'z') ? PieceColor::Black
                                           : PieceColor::White;
    char c = (*p >= 'a' && *p <= 'z') ? *p - ('a' - 'A') : *p;
    switch (c) {
    case 'K':
      piece.type = PieceType::King;
      break;
    case 'Q':
      piece.type = PieceType::Queen;
      break;
    case 'R':
      piece.type = PieceType::Rook;
      break;
    case 'B':
      piece.type = PieceType::Bishop;
      break;
    case 'N':
      piece.type = PieceType::Knight;
      break;
    case 'P':
      piece.type = PieceType::Pawn;
      break;
    default:
      return false;
    }
    nextBoard[row * 8 + col] = piece;
    col++;
  }

  if (row != 7 || col != 8) {
    return false;
  }

  for (int i = 0; i < 64; ++i) {
    board[i] = nextBoard[i];
  }
  resetGameState();
  historyReplayable = false;
  turn = sideToMove;
  started = true;
  mode = Mode::Playing;
  return true;
}

size_t ChessApp::saveContext(uint8_t *buffer, size_t capacity) const {
  uint16_t historyTokens[CHESS_MAX_HISTORY_TOKENS] = {};
  uint8_t tokenCount = 0;
  uint8_t savedHistoryCount = historyReplayable ? historyCount : 0;
  for (uint8_t i = 0; i < savedHistoryCount; i++) {
    if (historyFrom[i] < 0 || historyFrom[i] >= 64 || historyTo[i] < 0 ||
        historyTo[i] >= 64 || tokenCount >= CHESS_MAX_HISTORY_TOKENS) {
      return 0;
    }
    historyTokens[tokenCount++] =
        static_cast<uint16_t>(historyFrom[i]) |
        (static_cast<uint16_t>(historyTo[i]) << 6);

    char promotion = chessPromotionChar(history[i]);
    if (promotion != '\0' && promotion != 'Q') {
      if (tokenCount >= CHESS_MAX_HISTORY_TOKENS) {
        return 0;
      }
      uint8_t code = chessPromotionCode(chessPromotionFromChar(promotion));
      historyTokens[tokenCount++] =
          static_cast<uint16_t>(code) | (static_cast<uint16_t>(code) << 6);
    }
  }

  uint8_t pieceCodes[32] = {};
  uint8_t pieceCount = 0;
  for (int square = 0; square < 64; square++) {
    if (!board[square].present) {
      continue;
    }
    if (pieceCount >= 32 || board[square].type == PieceType::Checker) {
      return 0;
    }
    uint8_t typeCode = 0;
    switch (board[square].type) {
    case PieceType::King:
      typeCode = 0;
      break;
    case PieceType::Queen:
      typeCode = 1;
      break;
    case PieceType::Rook:
      typeCode = 2;
      break;
    case PieceType::Bishop:
      typeCode = 3;
      break;
    case PieceType::Knight:
      typeCode = 4;
      break;
    case PieceType::Pawn:
      typeCode = 5;
      break;
    case PieceType::Checker:
      return 0;
    }
    pieceCodes[pieceCount++] =
        typeCode + (board[square].color == PieceColor::Black ? 6 : 0);
  }

  RtcBitWriter writer(buffer, capacity);
  writer.writeBits(CHESS_CONTEXT_VERSION, 4);
  writer.writeBits(static_cast<uint8_t>(mode), 2);
  writer.writeBits(turn == PieceColor::Black ? 1 : 0, 1);
  writer.writeBits(humanColor == PieceColor::Black ? 1 : 0, 1);
  writer.writeBits(vsAi ? 1 : 0, 1);
  writer.writeBits(aiLevel > 3 ? 3 : aiLevel, 2);
  writer.writeBits(started ? 1 : 0, 1);
  writer.writeBits(gameOver ? 1 : 0, 1);
  writer.writeBits(aiBookEnabled ? 1 : 0, 1);
  writer.writeBits(whiteKingMoved ? 1 : 0, 1);
  writer.writeBits(blackKingMoved ? 1 : 0, 1);
  writer.writeBits(whiteKingsideRookMoved ? 1 : 0, 1);
  writer.writeBits(whiteQueensideRookMoved ? 1 : 0, 1);
  writer.writeBits(blackKingsideRookMoved ? 1 : 0, 1);
  writer.writeBits(blackQueensideRookMoved ? 1 : 0, 1);
  writer.writeBits(enPassantSquare >= 0 ? enPassantSquare : 64, 7);
  writer.writeBits(savedHistoryCount, 7);
  writer.writeBits(historyOffset, 7);
  writer.writeBits(hasLastMove ? 1 : 0, 1);
  writer.writeBits(lastMoveFrom >= 0 ? lastMoveFrom : 0, 6);
  writer.writeBits(lastMoveTo >= 0 ? lastMoveTo : 0, 6);
  writer.writeBits(lastMoveColor == PieceColor::Black ? 1 : 0, 1);
  writer.writeBits(historyReplayable ? 1 : 0, 1);

  for (int square = 0; square < 64; square++) {
    writer.writeBits(board[square].present ? 1 : 0, 1);
  }
  for (int i = 0; i < 32; i++) {
    writer.writeBits(pieceCodes[i], 4);
  }

  writer.writeBits(tokenCount, 8);
  for (uint8_t i = 0; i < tokenCount; i++) {
    writer.writeBits(historyTokens[i] & 0x3f, 6);
    writer.writeBits((historyTokens[i] >> 6) & 0x3f, 6);
  }
  return writer.ok() ? writer.bytesWritten() : 0;
}

void ChessApp::restoreContext(const uint8_t *buffer, size_t length) {
  RtcBitReader reader(buffer, length);
  uint32_t value = 0;
  if (!reader.readBits(4, value) || value != CHESS_CONTEXT_VERSION) {
    return;
  }

  uint32_t savedMode = 0;
  uint32_t savedTurn = 0;
  uint32_t savedHumanColor = 0;
  uint32_t savedVsAi = 0;
  uint32_t savedAiLevel = 0;
  uint32_t savedStarted = 0;
  uint32_t savedGameOver = 0;
  uint32_t savedAiBook = 0;
  uint32_t savedWhiteKingMoved = 0;
  uint32_t savedBlackKingMoved = 0;
  uint32_t savedWhiteKingsideRookMoved = 0;
  uint32_t savedWhiteQueensideRookMoved = 0;
  uint32_t savedBlackKingsideRookMoved = 0;
  uint32_t savedBlackQueensideRookMoved = 0;
  uint32_t savedEnPassant = 0;
  uint32_t savedHistoryCount = 0;
  uint32_t savedHistoryOffset = 0;
  uint32_t savedHasLastMove = 0;
  uint32_t savedLastMoveFrom = 0;
  uint32_t savedLastMoveTo = 0;
  uint32_t savedLastMoveColor = 0;
  uint32_t savedHistoryReplayable = 0;

  if (!reader.readBits(2, savedMode) || savedMode > 2 ||
      !reader.readBits(1, savedTurn) ||
      !reader.readBits(1, savedHumanColor) ||
      !reader.readBits(1, savedVsAi) ||
      !reader.readBits(2, savedAiLevel) || savedAiLevel == 0 ||
      !reader.readBits(1, savedStarted) ||
      !reader.readBits(1, savedGameOver) ||
      !reader.readBits(1, savedAiBook) ||
      !reader.readBits(1, savedWhiteKingMoved) ||
      !reader.readBits(1, savedBlackKingMoved) ||
      !reader.readBits(1, savedWhiteKingsideRookMoved) ||
      !reader.readBits(1, savedWhiteQueensideRookMoved) ||
      !reader.readBits(1, savedBlackKingsideRookMoved) ||
      !reader.readBits(1, savedBlackQueensideRookMoved) ||
      !reader.readBits(7, savedEnPassant) || savedEnPassant > 64 ||
      !reader.readBits(7, savedHistoryCount) || savedHistoryCount > 96 ||
      !reader.readBits(7, savedHistoryOffset) ||
      !reader.readBits(1, savedHasLastMove) ||
      !reader.readBits(6, savedLastMoveFrom) ||
      !reader.readBits(6, savedLastMoveTo) ||
      !reader.readBits(1, savedLastMoveColor) ||
      !reader.readBits(1, savedHistoryReplayable)) {
    return;
  }

  bool occupied[64] = {};
  uint8_t pieceCount = 0;
  for (int square = 0; square < 64; square++) {
    if (!reader.readBits(1, value)) {
      return;
    }
    occupied[square] = value != 0;
    if (occupied[square]) {
      pieceCount++;
      if (pieceCount > 32) {
        return;
      }
    }
  }

  uint8_t pieceCodes[32] = {};
  for (int i = 0; i < 32; i++) {
    if (!reader.readBits(4, value) || value > 11) {
      return;
    }
    pieceCodes[i] = static_cast<uint8_t>(value);
  }

  Piece nextBoard[64] = {};
  uint8_t pieceIndex = 0;
  for (int square = 0; square < 64; square++) {
    if (!occupied[square]) {
      continue;
    }
    uint8_t code = pieceCodes[pieceIndex++];
    Piece piece = {};
    piece.present = true;
    piece.color = code >= 6 ? PieceColor::Black : PieceColor::White;
    switch (code % 6) {
    case 0:
      piece.type = PieceType::King;
      break;
    case 1:
      piece.type = PieceType::Queen;
      break;
    case 2:
      piece.type = PieceType::Rook;
      break;
    case 3:
      piece.type = PieceType::Bishop;
      break;
    case 4:
      piece.type = PieceType::Knight;
      break;
    case 5:
      piece.type = PieceType::Pawn;
      break;
    }
    nextBoard[square] = piece;
  }

  uint32_t tokenCount = 0;
  if (!reader.readBits(8, tokenCount) ||
      tokenCount > CHESS_MAX_HISTORY_TOKENS) {
    return;
  }

  Move replayMoves[96] = {};
  PieceType replayPromotions[96] = {};
  uint8_t replayCount = 0;
  for (uint32_t i = 0; i < tokenCount; i++) {
    uint32_t from = 0;
    uint32_t to = 0;
    if (!reader.readBits(6, from) || !reader.readBits(6, to)) {
      return;
    }
    if (from == to && from < 4) {
      if (replayCount == 0) {
        return;
      }
      replayPromotions[replayCount - 1] =
          chessPromotionFromCode(static_cast<uint8_t>(from));
      replayMoves[replayCount - 1].promotion = replayPromotions[replayCount - 1];
      continue;
    }
    if (from == to || replayCount >= 96) {
      return;
    }
    replayMoves[replayCount] = {static_cast<int8_t>(from),
                                static_cast<int8_t>(to), PieceType::Queen,
                                true};
    replayPromotions[replayCount] = PieceType::Queen;
    replayCount++;
  }
  if (!reader.ok() || replayCount != savedHistoryCount) {
    return;
  }

  placeInitialPieces();
  resetGameState();
  mode = Mode::Playing;
  started = true;
  bool replayOk = true;
  for (uint8_t i = 0; i < replayCount; i++) {
    Move move = replayMoves[i];
    move.promotion = replayPromotions[i];
    move.hasPromotion = true;
    if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64 ||
        !board[move.from].present) {
      replayOk = false;
      break;
    }
    Piece moving = board[move.from];
    Piece captured = board[move.to];
    if (moving.type == PieceType::Pawn && move.to == enPassantSquare &&
        !captured.present) {
      int capturedSquare =
          move.to + (moving.color == PieceColor::White ? 8 : -8);
      if (capturedSquare >= 0 && capturedSquare < 64) {
        captured = board[capturedSquare];
      }
    }
    appendHistory(move, moving, captured);
    applyMoveUnchecked(move, moving, captured, false);
    hasLastMove = true;
    lastMoveFrom = move.from;
    lastMoveTo = move.to;
    lastMoveColor = moving.color;
    turn = opposite(turn);
    if (isInCheck(turn)) {
      appendHistorySuffix(hasAnyLegalMove(turn) ? '+' : '#');
    }
  }
  if (!replayOk) {
    historyCount = 0;
    historyOffset = 0;
  }

  for (int i = 0; i < 64; i++) {
    board[i] = nextBoard[i];
  }
  mode = static_cast<Mode>(savedMode);
  turn = savedTurn ? PieceColor::Black : PieceColor::White;
  humanColor = savedHumanColor ? PieceColor::Black : PieceColor::White;
  vsAi = savedVsAi != 0;
  aiLevel = static_cast<uint8_t>(savedAiLevel);
  started = savedStarted != 0;
  gameOver = savedGameOver != 0;
  aiBookEnabled = savedAiBook != 0;
  whiteKingMoved = savedWhiteKingMoved != 0;
  blackKingMoved = savedBlackKingMoved != 0;
  whiteKingsideRookMoved = savedWhiteKingsideRookMoved != 0;
  whiteQueensideRookMoved = savedWhiteQueensideRookMoved != 0;
  blackKingsideRookMoved = savedBlackKingsideRookMoved != 0;
  blackQueensideRookMoved = savedBlackQueensideRookMoved != 0;
  enPassantSquare =
      savedEnPassant == 64 ? -1 : static_cast<int>(savedEnPassant);
  historyOffset = static_cast<uint8_t>(savedHistoryOffset);
  if (historyCount <= 7) {
    historyOffset = 0;
  } else if (historyOffset + 7 > historyCount) {
    historyOffset = historyCount - 7;
  }
  historyReplayable = savedHistoryReplayable != 0 && replayOk;
  hasLastMove = savedHasLastMove != 0;
  lastMoveFrom = hasLastMove ? static_cast<int>(savedLastMoveFrom) : -1;
  lastMoveTo = hasLastMove ? static_cast<int>(savedLastMoveTo) : -1;
  lastMoveColor = savedLastMoveColor ? PieceColor::Black : PieceColor::White;

  alertVisible = false;
  aiThinking = false;
  aiThinkingSinceMs = 0;
  menuCancelBlockUntilMs = 0;
  clearSelection();
  if (!started) {
    mode = Mode::Setup;
  } else if (mode == Mode::History && historyCount == 0) {
    mode = Mode::Playing;
  }
  setHint(gameOver ? "Game over"
                   : (turn == PieceColor::White ? "White to move"
                                                : "Black to move"));
  maybeRunAi();
}

bool ChessApp::hasActiveSession() const { return started && !gameOver; }

bool ChessApp::isGameOver() const { return started && gameOver; }

bool ChessApp::isHistoryOpen() const { return mode == Mode::History; }

bool ChessApp::isTargetSelectionActive() const {
  return mode == Mode::Playing && selectedFrom >= 0 && sourceConfirmed;
}

bool ChessApp::update() {
  bool changed = false;
  if (mode == Mode::Playing && aiThinking && !alertVisible && !gameOver &&
      !isHumanTurn()) {
    aiThinking = false;
    setHint("");
    Move move = chooseAiMove();
    if (move.from >= 0) {
      makeLegalMove(move, true);
    }
    if (!alertVisible) {
      setHint("");
    }
    changed = true;
  }

  if (mode != Mode::Playing || (selectedFrom < 0 && selectedTo < 0)) {
    return changed;
  }
  unsigned long now = millis();
  if (now - lastAnimationMs < 450) {
    return changed;
  }
  lastAnimationMs = now;
  return true;
}

bool ChessApp::handlePowerButton() {
  if (aiThinking) {
    return true;
  }
  if (alertVisible) {
    alertVisible = false;
    maybeRunAi();
    return true;
  }
  if (mode != Mode::Playing || gameOver || !isHumanTurn() ||
      awaitingPromotion) {
    return false;
  }
  if (selectedFrom >= 0 && !sourceConfirmed) {
    sourceConfirmed = true;
    pendingSelection = PendingSelection::Target;
    return true;
  }
  if (selectedFrom >= 0 && sourceConfirmed && selectedTo < 0) {
    clearSelection();
    setHint(turn == PieceColor::White ? "White to move" : "Black to move");
    return true;
  }
  if (selectedFrom >= 0 && selectedTo >= 0) {
    return tryMoveSelection();
  }
  return false;
}

bool ChessApp::handleMenuButton() {
  if (aiThinking) {
    return true;
  }
  unsigned long now = millis();
  if (mode == Mode::Playing && now < menuCancelBlockUntilMs) {
    return true;
  }
  if (mode == Mode::Playing) {
    if (cancelTargetSelection()) {
      return true;
    }
    mode = Mode::History;
    return true;
  }
  if (mode == Mode::History) {
    mode = Mode::Playing;
    return true;
  }
  return false;
}

bool ChessApp::cancelTargetSelection() {
  if (!isTargetSelectionActive()) {
    return false;
  }
  clearSelection();
  setHint(turn == PieceColor::White ? "White to move" : "Black to move");
  menuCancelBlockUntilMs = millis() + 350;
  return true;
}

bool ChessApp::handleMenuLongPress() {
  if (aiThinking) {
    return true;
  }
  return started && !isTargetSelectionActive() &&
         (mode == Mode::Playing || mode == Mode::History);
}

bool ChessApp::handleTouch(const TouchPoint &point) {
  hasLastTouchPoint = true;
  lastTouchX = point.x;
  lastTouchY = point.y;

  if (aiThinking) {
    return true;
  }
  if (alertVisible) {
    alertVisible = false;
    maybeRunAi();
    return true;
  }

  if (mode == Mode::Setup) {
    if (uiContains(PVP_BUTTON, point)) {
      vsAi = false;
      return true;
    }
    if (uiContains(AI1_BUTTON, point) || uiContains(AI2_BUTTON, point) ||
        uiContains(AI3_BUTTON, point)) {
      vsAi = true;
      aiLevel = uiContains(AI1_BUTTON, point) ? 1
                : uiContains(AI2_BUTTON, point) ? 2
                                                : 3;
      return true;
    }
    if (uiContains(COLOR_BUTTON, point)) {
      humanColor = opposite(humanColor);
      return true;
    }
    if (uiContains(START_BUTTON, point)) {
      startGame();
      return true;
    }
    setHint("Tap START first");
    return true;
  }

  if (mode == Mode::History) {
    if (point.y < 96 && historyOffset > 0) {
      historyOffset--;
    } else if (point.y >= 96 && historyOffset + 7 < historyCount) {
      historyOffset++;
    }
    return true;
  }

  if (awaitingPromotion) {
    for (int i = 0; i < 4; ++i) {
      if (uiContains(PROMOTION_BUTTONS[i], point)) {
        return finishPromotion(PROMOTION_TYPES[i]);
      }
    }
    return true;
  }

  if (gameOver || !isBoardRowPoint(point)) {
    return true;
  }
  if (!isHumanTurn()) {
    setHint("AI turn");
    return true;
  }

  int square = pointToSquare(point);
  if (pendingSelection == PendingSelection::Source || !sourceConfirmed) {
    if (selectedFrom == square) {
      clearSelection();
      setHint(turn == PieceColor::White ? "White to move" : "Black to move");
      return true;
    }
    square = resolveSourceSquare(square);
    const Piece &piece = board[square];
    if (piece.present && piece.color == turn) {
      selectedFrom = square;
      selectedTo = -1;
      sourceConfirmed = false;
      targetConfirmed = false;
      pendingSelection = PendingSelection::Source;
      char name[3];
      squareName(square, name);
      snprintf(hintText, sizeof(hintText), "From %s", name);
      return true;
    }
    setHint(turn == PieceColor::White ? "White to move" : "Black to move");
    return true;
  }

  if (selectedTo == square) {
    if (targetConfirmed && millis() - targetSelectedAtMs < 3000) {
      return true;
    }
    selectedTo = -1;
    targetConfirmed = false;
    targetSelectedAtMs = 0;
    setHint("Pick target");
    return true;
  }
  if (selectedFrom == square) {
    setHint("Pick target");
    return true;
  }
  square = resolveTargetSquare(square);
  selectedTo = square;
  targetConfirmed = true;
  targetSelectedAtMs = millis();
  char name[3];
  squareName(square, name);
  snprintf(hintText, sizeof(hintText), "To %s", name);
  return true;
}

void ChessApp::startGame() {
  placeInitialPieces();
  randomSeed((unsigned long)micros());
  turn = PieceColor::White;
  enPassantSquare = -1;
  historyCount = 0;
  historyOffset = 0;
  hasLastMove = false;
  lastMoveFrom = -1;
  lastMoveTo = -1;
  aiThinking = false;
  aiThinkingSinceMs = 0;
  aiBookEnabled = true;
  gameOver = false;
  started = true;
  alertVisible = false;
  mode = Mode::Playing;
  clearSelection();
  maybeRunAi();
}

void ChessApp::clearSelection() {
  selectedFrom = -1;
  selectedTo = -1;
  sourceConfirmed = false;
  targetConfirmed = false;
  targetSelectedAtMs = 0;
  awaitingPromotion = false;
  promotionMove = {-1, -1, PieceType::Queen, true};
  pendingSelection = PendingSelection::Source;
}

void ChessApp::setAlert(const char *text) {
  strncpy(alertText, text, sizeof(alertText) - 1);
  alertText[sizeof(alertText) - 1] = '\0';
  alertVisible = true;
}

void ChessApp::setHint(const char *text) {
  strncpy(hintText, text, sizeof(hintText) - 1);
  hintText[sizeof(hintText) - 1] = '\0';
}

bool ChessApp::isHumanTurn() const { return !vsAi || turn == humanColor; }

PieceColor ChessApp::opposite(PieceColor color) const {
  return color == PieceColor::White ? PieceColor::Black : PieceColor::White;
}

bool ChessApp::isInsideBoardPoint(const TouchPoint &point) const {
  return point.x >= BOARD_X && point.x < BOARD_X + BOARD_SIZE &&
         point.y >= BOARD_Y && point.y < BOARD_Y + BOARD_SIZE;
}

bool ChessApp::isBoardRowPoint(const TouchPoint &point) const {
  return point.x >= BOARD_X - BOARD_TOUCH_ASSIST &&
         point.x < BOARD_X + BOARD_SIZE + BOARD_TOUCH_ASSIST &&
         point.y >= BOARD_Y - BOARD_TOUCH_ASSIST &&
         point.y < BOARD_Y + BOARD_SIZE + BOARD_TOUCH_ASSIST;
}

int ChessApp::displayRowToBoardRow(int displayRow) const {
  return humanColor == PieceColor::White ? displayRow : 7 - displayRow;
}

int ChessApp::boardRowToDisplayRow(int boardRow) const {
  return humanColor == PieceColor::White ? boardRow : 7 - boardRow;
}

int ChessApp::pointToSquare(const TouchPoint &point) const {
  int clampedX = point.x;
  if (clampedX < BOARD_X) {
    clampedX = BOARD_X;
  } else if (clampedX >= BOARD_X + BOARD_SIZE) {
    clampedX = BOARD_X + BOARD_SIZE - 1;
  }
  int clampedY = point.y;
  if (clampedY < BOARD_Y) {
    clampedY = BOARD_Y;
  } else if (clampedY >= BOARD_Y + BOARD_SIZE) {
    clampedY = BOARD_Y + BOARD_SIZE - 1;
  }
  int displayCol = (clampedX - BOARD_X) / CELL;
  int displayRow = (clampedY - BOARD_Y) / CELL;
  int row = displayRowToBoardRow(displayRow);
  int col = humanColor == PieceColor::White ? displayCol : 7 - displayCol;
  return row * 8 + col;
}

int ChessApp::resolveSourceSquare(int square) const {
  if (square < 0 || square >= 64 ||
      (board[square].present && board[square].color == turn)) {
    return square;
  }

  int row = square / 8;
  int col = square % 8;
  int candidate = -1;
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      if (dr == 0 && dc == 0) {
        continue;
      }
      int r = row + dr;
      int c = col + dc;
      if (r < 0 || r >= 8 || c < 0 || c >= 8) {
        continue;
      }
      int next = r * 8 + c;
      if (board[next].present && board[next].color == turn) {
        if (candidate >= 0) {
          return square;
        }
        candidate = next;
      }
    }
  }
  return candidate >= 0 ? candidate : square;
}

int ChessApp::resolveTargetSquare(int square) const {
  if (square < 0 || square >= 64 || selectedFrom < 0 || selectedFrom >= 64) {
    return square;
  }

  Move direct = {static_cast<int8_t>(selectedFrom), static_cast<int8_t>(square),
                 PieceType::Queen, true};
  if (isLegalMove(direct)) {
    return square;
  }

  const int tapRow = square / 8;
  const int tapCol = square % 8;
  const int fromRow = selectedFrom / 8;
  const int fromCol = selectedFrom % 8;
  const int tapVecRow = tapRow - fromRow;
  const int tapVecCol = tapCol - fromCol;

  int bestSquare = -1;
  int bestChebyshev = 9;
  int bestManhattan = 99;
  int bestDirection = -1;
  int bestCross = 32767;

  for (int to = 0; to < 64; ++to) {
    Move assisted = {static_cast<int8_t>(selectedFrom), static_cast<int8_t>(to),
                     PieceType::Queen, true};
    if (!isLegalMove(assisted)) {
      continue;
    }

    const int row = to / 8;
    const int col = to % 8;
    const int dRow = abs(row - tapRow);
    const int dCol = abs(col - tapCol);
    const int chebyshev = dRow > dCol ? dRow : dCol;
    if (chebyshev > 1) {
      continue;
    }

    const int manhattan = dRow + dCol;
    const int moveVecRow = row - fromRow;
    const int moveVecCol = col - fromCol;
    const int dot = tapVecRow * moveVecRow + tapVecCol * moveVecCol;
    const int direction = dot >= 0 ? 1 : 0;
    const int cross = abs(tapVecRow * moveVecCol - tapVecCol * moveVecRow);

    const bool better =
        chebyshev < bestChebyshev ||
        (chebyshev == bestChebyshev &&
         (manhattan < bestManhattan ||
          (manhattan == bestManhattan &&
           (direction > bestDirection ||
            (direction == bestDirection && cross < bestCross)))));
    if (better) {
      bestSquare = to;
      bestChebyshev = chebyshev;
      bestManhattan = manhattan;
      bestDirection = direction;
      bestCross = cross;
    }
  }
  return bestSquare >= 0 ? bestSquare : square;
}

void ChessApp::draw(Adafruit_GFX &gfx) {
  if (mode == Mode::Setup) {
    drawSetup(gfx);
  } else if (mode == Mode::History) {
    drawHistory(gfx);
  } else {
    drawBoard(gfx);
    drawStatus(gfx);
  }
  if (awaitingPromotion) {
    drawPromotionChooser(gfx);
  }
  if (alertVisible) {
    drawAlert(gfx);
  } else if (aiThinking) {
    drawThinkingOverlay(gfx);
  }
  drawTouchMarker(gfx);
}

void ChessApp::drawSetup(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  drawCenteredText(gfx, "CHESS", 10, 2);
  drawCenteredText(gfx, "Choose mode and side", 34, 1);
  uiDrawButton(gfx, PVP_BUTTON, "PVP", !vsAi);
  uiDrawButton(gfx, AI1_BUTTON, "AI1", vsAi && aiLevel == 1);
  uiDrawButton(gfx, AI2_BUTTON, "AI2", vsAi && aiLevel == 2);
  uiDrawButton(gfx, AI3_BUTTON, "AI3", vsAi && aiLevel == 3);
  uiDrawButton(gfx, COLOR_BUTTON,
               humanColor == PieceColor::White ? "WHITE" : "BLACK",
               humanColor == PieceColor::Black);
  uiDrawButton(gfx, START_BUTTON, "START");
}

void ChessApp::drawStatus(Adafruit_GFX &gfx) {
  gfx.fillRect(0, 193, 200, 7, 0);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  gfx.setCursor(2, 193);
  if (gameOver) {
    gfx.print("Game over");
  } else if (hintText[0]) {
    gfx.print(hintText);
  } else {
    gfx.print(turn == PieceColor::White ? "White" : "Black");
    gfx.print(isInCheck(turn) ? " in check" : " to move");
  }
  gfx.setCursor(122, 193);
  if (sourceConfirmed) {
    gfx.print(selectedTo >= 0 ? "pwr: move" : "Pick target");
  } else if (selectedFrom >= 0) {
    gfx.print("pwr: confirm");
  } else {
    gfx.print("pwr: select");
  }
}

void ChessApp::drawHistory(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(6, 6);
  gfx.print("MOVE HISTORY");
  gfx.setCursor(126, 6);
  gfx.print("MENU: back");
  for (int i = 0; i < 7; ++i) {
    int index = historyOffset + i;
    if (index >= historyCount) {
      break;
    }
    int y = 24 + i * 24;
    gfx.setFont();
    gfx.setTextSize(1);
    gfx.setCursor(8, y);
    gfx.print(index + 1);
    gfx.print(".");
    const CaseFontGlyph *glyph =
        findGlyph(historyPiece[index], historyColor[index], BoardColor::Light);
    if (glyph) {
      char text[2] = {static_cast<char>(glyph->codepoint), 0};
      gfx.setFont(&CHESS_CASEFONT12pt7b);
      gfx.setTextColor(1);
      gfx.setCursor(34, y + 14);
      gfx.print(text);
      gfx.setFont();
    }
    gfx.setTextSize(2);
    gfx.setCursor(58, y - 1);
    gfx.print(history[index] + 1);
    gfx.setTextSize(1);
    if (historyHasCapture[index]) {
      const CaseFontGlyph *capturedGlyph =
          findGlyph(historyCapturedPiece[index], historyCapturedColor[index],
                    BoardColor::Light);
      if (capturedGlyph) {
        char capturedText[2] = {static_cast<char>(capturedGlyph->codepoint), 0};
        gfx.setFont(&CHESS_CASEFONT12pt7b);
        gfx.setTextColor(1);
        gfx.setCursor(166, y + 14);
        gfx.print(capturedText);
        gfx.setFont();
      }
    }
  }
}

void ChessApp::drawBoard(Adafruit_GFX &gfx) {
  gfx.drawRect(BOARD_X - 1, BOARD_Y - 1, BOARD_SIZE + 2, BOARD_SIZE + 2, 1);

  for (int displayRow = 0; displayRow < 8; ++displayRow) {
    for (int displayCol = 0; displayCol < 8; ++displayCol) {
      int row = displayRowToBoardRow(displayRow);
      int col = humanColor == PieceColor::White ? displayCol : 7 - displayCol;
      BoardColor boardColor = ((row + col) % 2 == 0) ? BoardColor::Light
                                                      : BoardColor::Dark;
      int x = BOARD_X + displayCol * CELL;
      int y = BOARD_Y + displayRow * CELL;
      gfx.fillRect(x, y, CELL, CELL, boardColor == BoardColor::Dark ? 1 : 0);
      gfx.drawRect(x, y, CELL, CELL, 1);

      const Piece &piece = board[row * 8 + col];
      if (piece.present) {
        drawPiece(gfx, row, col, piece);
      }
    }
  }

  if (hasLastMove) {
    drawMoveOutline(gfx, lastMoveFrom);
    drawMoveOutline(gfx, lastMoveTo);
  }
  drawExperimentalCoordinates(gfx);

  if (selectedFrom >= 0 || selectedTo >= 0) {
    selectionBorderPhase = !selectionBorderPhase;
  }
  if (selectedFrom >= 0) {
    drawSelection(gfx, selectedFrom);
  }
  if (selectedTo >= 0) {
    drawSelection(gfx, selectedTo);
  }
}

void ChessApp::drawExperimentalCoordinates(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  for (int displayCol = 0; displayCol < 8; ++displayCol) {
    int boardCol = humanColor == PieceColor::White ? displayCol : 7 - displayCol;
    char file[2] = {static_cast<char>('A' + boardCol), '\0'};
    gfx.setCursor(BOARD_X + displayCol * CELL + 8, 0);
    gfx.print(file);
  }
  for (int displayRow = 0; displayRow < 8; ++displayRow) {
    int boardRow = displayRowToBoardRow(displayRow);
    char label[2] = {static_cast<char>('8' - boardRow), '\0'};
    gfx.setCursor(195, BOARD_Y + displayRow * CELL + 2);
    gfx.print(label);
  }
}

void ChessApp::drawMoveOutline(Adafruit_GFX &gfx, int square) {
  if (square < 0 || square >= 64) {
    return;
  }
  int row = square / 8;
  int col = square % 8;
  int displayRow = boardRowToDisplayRow(row);
  int displayCol = humanColor == PieceColor::White ? col : 7 - col;
  BoardColor boardColor = ((row + col) % 2 == 0) ? BoardColor::Light
                                                  : BoardColor::Dark;
  uint16_t color = boardColor == BoardColor::Dark ? 0 : 1;
  int x = BOARD_X + displayCol * CELL + 1;
  int y = BOARD_Y + displayRow * CELL + 1;
  gfx.drawRect(x, y, CELL - 2, CELL - 2, color);

  const int dot = 3;
  const int centerX = x + (CELL - 2) / 2;
  const int centerY = y + (CELL - 2) / 2;
  gfx.fillRect(centerX - dot / 2, y - dot / 2, dot, dot, color);
  gfx.fillRect(centerX - dot / 2, y + CELL - 2 - dot / 2, dot, dot, color);
  gfx.fillRect(x - dot / 2, centerY - dot / 2, dot, dot, color);
  gfx.fillRect(x + CELL - 2 - dot / 2, centerY - dot / 2, dot, dot, color);
}

void ChessApp::drawTouchMarker(Adafruit_GFX &gfx) {
  if (!hasLastTouchPoint) {
    return;
  }

  const int gap = 3;
  const int dot = 2;
  struct Dot {
    int16_t x;
    int16_t y;
    uint16_t color;
  };
  const Dot dots[4] = {
      {static_cast<int16_t>(lastTouchX - gap),
       static_cast<int16_t>(lastTouchY - gap), 1},
      {static_cast<int16_t>(lastTouchX + gap),
       static_cast<int16_t>(lastTouchY - gap), 0},
      {static_cast<int16_t>(lastTouchX - gap),
       static_cast<int16_t>(lastTouchY + gap), 0},
      {static_cast<int16_t>(lastTouchX + gap),
       static_cast<int16_t>(lastTouchY + gap), 1},
  };

  for (const Dot &markerDot : dots) {
    if (markerDot.x < 0 || markerDot.y < 0 ||
        markerDot.x + dot > DISPLAY_WIDTH ||
        markerDot.y + dot > DISPLAY_HEIGHT) {
      continue;
    }
    gfx.fillRect(markerDot.x, markerDot.y, dot, dot, markerDot.color);
  }
}

void ChessApp::drawSelection(Adafruit_GFX &gfx, int square) {
  int row = square / 8;
  int col = square % 8;
  int displayRow = boardRowToDisplayRow(row);
  int displayCol = humanColor == PieceColor::White ? col : 7 - col;
  static const int BORDER_INSET = 2;
  static const int BORDER_WIDTH = 2;
  int x = BOARD_X + displayCol * CELL + BORDER_INSET;
  int y = BOARD_Y + displayRow * CELL + BORDER_INSET;
  uint16_t color = selectionBorderPhase ? 1 : 0;
  for (int inset = 0; inset < BORDER_WIDTH; ++inset) {
    gfx.drawRect(x + inset, y + inset, CELL - BORDER_INSET * 2 - inset * 2,
                 CELL - BORDER_INSET * 2 - inset * 2, color);
  }
}

void ChessApp::drawPromotionChooser(Adafruit_GFX &gfx) {
  gfx.fillRect(18, 70, 164, 62, 0);
  gfx.drawRect(18, 70, 164, 62, 1);
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(48, 78);
  gfx.print("Promote pawn");

  for (int i = 0; i < 4; ++i) {
    const UiRect &button = PROMOTION_BUTTONS[i];
    gfx.drawRect(button.x, button.y, button.w, button.h, 1);
    const CaseFontGlyph *glyph =
        findGlyph(PROMOTION_TYPES[i], turn, BoardColor::Light);
    if (!glyph) {
      continue;
    }
    char text[2] = {static_cast<char>(glyph->codepoint), 0};
    gfx.setFont(&CHESS_CASEFONT12pt7b);
    gfx.setTextColor(1);
    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;
    gfx.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    gfx.setCursor(button.x + (button.w - static_cast<int>(w)) / 2 - x1,
                  button.y + (button.h - static_cast<int>(h)) / 2 - y1);
    gfx.print(text);
    gfx.setFont();
  }
}

void ChessApp::drawAlert(Adafruit_GFX &gfx) {
  static const int POPUP_X = 22;
  static const int POPUP_Y = 72;
  static const int POPUP_W = 156;
  static const int POPUP_H = 56;
  gfx.fillRect(POPUP_X, POPUP_Y, POPUP_W, POPUP_H, 0);
  gfx.drawRect(POPUP_X, POPUP_Y, POPUP_W, POPUP_H, 1);
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(alertText, 0, 0, &x, &y, &w, &h);
  gfx.setCursor(POPUP_X + (POPUP_W - static_cast<int>(w)) / 2, 88);
  gfx.print(alertText);
  const char *okText = "OK";
  gfx.getTextBounds(okText, 0, 0, &x, &y, &w, &h);
  gfx.setCursor(POPUP_X + (POPUP_W - static_cast<int>(w)) / 2, 110);
  gfx.print(okText);
}

void ChessApp::drawThinkingOverlay(Adafruit_GFX &gfx) {
  static const int POPUP_X = 22;
  static const int POPUP_Y = 72;
  static const int POPUP_W = 156;
  static const int POPUP_H = 56;
  gfx.fillRect(POPUP_X, POPUP_Y, POPUP_W, POPUP_H, 0);
  gfx.drawRect(POPUP_X, POPUP_Y, POPUP_W, POPUP_H, 1);
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  const char *thinkingText = "Thinking";
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(thinkingText, 0, 0, &x, &y, &w, &h);
  int textX = POPUP_X + (POPUP_W - static_cast<int>(w)) / 2 - x;
  int textY = POPUP_Y + (POPUP_H - static_cast<int>(h)) / 2 - y;
  gfx.setCursor(textX, textY);
  gfx.print(thinkingText);
}

const CaseFontGlyph *ChessApp::findGlyph(PieceType type, PieceColor color,
                                         BoardColor boardColor) const {
  for (const auto &glyph : CASEFONT_PIECES) {
    if (glyph.piece == type && glyph.pieceColor == color &&
        glyph.boardColor == boardColor) {
      return &glyph;
    }
  }
  return nullptr;
}

void ChessApp::drawPiece(Adafruit_GFX &gfx, int row, int col,
                         const Piece &piece) {
  BoardColor boardColor = ((row + col) % 2 == 0) ? BoardColor::Light
                                                 : BoardColor::Dark;
  const CaseFontGlyph *glyph = findGlyph(piece.type, piece.color, boardColor);
  if (!glyph) {
    return;
  }

  char text[2] = {static_cast<char>(glyph->codepoint), 0};
  gfx.setFont(&CHESS_CASEFONT12pt7b);
  gfx.setTextSize(1);
  gfx.setTextColor(boardColor == BoardColor::Dark ? 0 : 1);

  int displayRow = boardRowToDisplayRow(row);
  int displayCol = humanColor == PieceColor::White ? col : 7 - col;
  int x = BOARD_X + displayCol * CELL;
  int y = BOARD_Y + displayRow * CELL;
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int cursorX = x + (CELL - static_cast<int>(w)) / 2 - x1;
  int cursorY = y + (CELL - static_cast<int>(h)) / 2 - y1;
  if (piece.type == PieceType::Rook && piece.color == PieceColor::Black &&
      boardColor == BoardColor::Light) {
    cursorY += 4;
  }
  gfx.setCursor(cursorX, cursorY);
  gfx.print(text);
  gfx.setFont();
}

bool ChessApp::tryMoveSelection() {
  Move move = {static_cast<int8_t>(selectedFrom), static_cast<int8_t>(selectedTo),
               PieceType::Queen, true};
  if (isPromotionMove(move) && isLegalMove(move) && isHumanTurn()) {
    promotionMove = move;
    awaitingPromotion = true;
    selectedFrom = -1;
    selectedTo = -1;
    sourceConfirmed = false;
    targetConfirmed = false;
    pendingSelection = PendingSelection::Source;
    return true;
  }
  if (!makeLegalMove(move, true)) {
    setAlert("Illegal move");
    selectedTo = -1;
    targetConfirmed = false;
    sourceConfirmed = true;
    pendingSelection = PendingSelection::Target;
    return true;
  }
  clearSelection();
  if (!alertVisible) {
    maybeRunAi();
  }
  return true;
}

bool ChessApp::isPromotionMove(const Move &move) const {
  if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
    return false;
  }
  const Piece &piece = board[move.from];
  if (!piece.present || piece.type != PieceType::Pawn) {
    return false;
  }
  int toRow = move.to / 8;
  return (piece.color == PieceColor::White && toRow == 0) ||
         (piece.color == PieceColor::Black && toRow == 7);
}

bool ChessApp::finishPromotion(PieceType promotion) {
  if (!awaitingPromotion) {
    return false;
  }
  promotionMove.promotion = promotion;
  promotionMove.hasPromotion = true;
  awaitingPromotion = false;
  bool moved = makeLegalMove(promotionMove, true);
  promotionMove = {-1, -1, PieceType::Queen, true};
  if (!moved) {
    setAlert("Illegal promotion");
    return true;
  }
  if (!alertVisible) {
    maybeRunAi();
  }
  return true;
}

bool ChessApp::makeLegalMove(const Move &move, bool recordHistory) {
  if (!isLegalMove(move)) {
    return false;
  }

  Piece moving = board[move.from];
  Piece captured = board[move.to];
  if (moving.type == PieceType::Pawn && move.to == enPassantSquare &&
      !captured.present) {
    int capturedSquare = move.to + (moving.color == PieceColor::White ? 8 : -8);
    captured = board[capturedSquare];
  }
  if (recordHistory) {
    appendHistory(move, moving, captured);
  }
  applyMoveUnchecked(move, moving, captured, false);
  hasLastMove = true;
  lastMoveFrom = move.from;
  lastMoveTo = move.to;
  lastMoveColor = moving.color;
  turn = opposite(turn);

  if (isInCheck(turn)) {
    if (!hasAnyLegalMove(turn)) {
      gameOver = true;
      appendHistorySuffix('#');
      setAlert(turn == PieceColor::White ? "Checkmate: Black wins"
                                         : "Checkmate: White wins");
    } else {
      appendHistorySuffix('+');
      setAlert(turn == PieceColor::White ? "White in check"
                                         : "Black in check");
    }
  } else if (!hasAnyLegalMove(turn)) {
    gameOver = true;
    setAlert("Stalemate");
  }
  return true;
}

void ChessApp::applyMoveUnchecked(const Move &move, Piece moving,
                                  Piece captured, bool simulation) {
  (void)captured;
  int oldEnPassant = enPassantSquare;
  enPassantSquare = -1;

  if (moving.type == PieceType::Pawn && move.to == oldEnPassant &&
      !board[move.to].present) {
    int capturedSquare = move.to + (moving.color == PieceColor::White ? 8 : -8);
    board[capturedSquare] = {};
  }

  board[move.to] = moving;
  board[move.from] = {};

  int toRow = move.to / 8;
  if (moving.type == PieceType::Pawn && (toRow == 0 || toRow == 7)) {
    board[move.to].type = move.hasPromotion ? move.promotion : PieceType::Queen;
  }

  if (moving.type == PieceType::King && move.to - move.from == 2) {
    board[move.from + 1] = board[move.from + 3];
    board[move.from + 3] = {};
  } else if (moving.type == PieceType::King && move.from - move.to == 2) {
    board[move.from - 1] = board[move.from - 4];
    board[move.from - 4] = {};
  }

  if (moving.type == PieceType::Pawn && abs(move.to - move.from) == 16) {
    enPassantSquare = (move.from + move.to) / 2;
  }

  if (simulation) {
    return;
  }

  if (moving.type == PieceType::King) {
    if (moving.color == PieceColor::White) {
      whiteKingMoved = true;
    } else {
      blackKingMoved = true;
    }
  }
  if (moving.type == PieceType::Rook) {
    if (move.from == 56) {
      whiteQueensideRookMoved = true;
    } else if (move.from == 63) {
      whiteKingsideRookMoved = true;
    } else if (move.from == 0) {
      blackQueensideRookMoved = true;
    } else if (move.from == 7) {
      blackKingsideRookMoved = true;
    }
  }
  if (move.to == 56) {
    whiteQueensideRookMoved = true;
  } else if (move.to == 63) {
    whiteKingsideRookMoved = true;
  } else if (move.to == 0) {
    blackQueensideRookMoved = true;
  } else if (move.to == 7) {
    blackKingsideRookMoved = true;
  }
}

bool ChessApp::isLegalMove(const Move &move) const {
  if (!isPseudoLegalMove(move)) {
    return false;
  }
  return !wouldLeaveKingInCheck(move);
}

bool ChessApp::wouldLeaveKingInCheck(const Move &move) const {
  Piece position[64];
  for (int i = 0; i < 64; ++i) {
    position[i] = board[i];
  }

  Piece moving = position[move.from];
  int oldEnPassant = enPassantSquare;
  if (moving.type == PieceType::Pawn && move.to == oldEnPassant &&
      !position[move.to].present) {
    int capturedSquare = move.to + (moving.color == PieceColor::White ? 8 : -8);
    position[capturedSquare] = {};
  }

  position[move.to] = moving;
  position[move.from] = {};

  int toRow = move.to / 8;
  if (moving.type == PieceType::Pawn && (toRow == 0 || toRow == 7)) {
    position[move.to].type =
        move.hasPromotion ? move.promotion : PieceType::Queen;
  }

  if (moving.type == PieceType::King && move.to - move.from == 2) {
    position[move.from + 1] = position[move.from + 3];
    position[move.from + 3] = {};
  } else if (moving.type == PieceType::King && move.from - move.to == 2) {
    position[move.from - 1] = position[move.from - 4];
    position[move.from - 4] = {};
  }

  int king = findKingOnBoard(position, moving.color);
  return king >= 0 && isSquareAttackedOnBoard(position, king, opposite(moving.color));
}

bool ChessApp::isPseudoLegalMove(const Move &move) const {
  if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64 ||
      move.from == move.to) {
    return false;
  }
  const Piece &moving = board[move.from];
  const Piece &target = board[move.to];
  if (!moving.present || moving.color != turn ||
      (target.present && target.color == moving.color)) {
    return false;
  }
  if (move.hasPromotion && move.promotion != PieceType::Queen &&
      move.promotion != PieceType::Rook && move.promotion != PieceType::Bishop &&
      move.promotion != PieceType::Knight) {
    return false;
  }

  int fromRow = move.from / 8;
  int fromCol = move.from % 8;
  int toRow = move.to / 8;
  int toCol = move.to % 8;
  int dr = toRow - fromRow;
  int dc = toCol - fromCol;

  switch (moving.type) {
  case PieceType::Pawn: {
    int dir = moving.color == PieceColor::White ? -1 : 1;
    int startRow = moving.color == PieceColor::White ? 6 : 1;
    if (dc == 0 && dr == dir && !target.present) {
      return true;
    }
    if (dc == 0 && dr == 2 * dir && fromRow == startRow && !target.present &&
        !board[(fromRow + dir) * 8 + fromCol].present) {
      return true;
    }
    if (abs(dc) == 1 && dr == dir) {
      return (target.present && target.color != moving.color) ||
             move.to == enPassantSquare;
    }
    return false;
  }
  case PieceType::Knight:
    return (abs(dr) == 2 && abs(dc) == 1) ||
           (abs(dr) == 1 && abs(dc) == 2);
  case PieceType::Bishop:
    return abs(dr) == abs(dc) &&
           pathClear(move.from, move.to, dr > 0 ? 1 : -1, dc > 0 ? 1 : -1);
  case PieceType::Rook:
    return (dr == 0 || dc == 0) &&
           pathClear(move.from, move.to, dr == 0 ? 0 : (dr > 0 ? 1 : -1),
                     dc == 0 ? 0 : (dc > 0 ? 1 : -1));
  case PieceType::Queen:
    if (abs(dr) == abs(dc)) {
      return pathClear(move.from, move.to, dr > 0 ? 1 : -1,
                       dc > 0 ? 1 : -1);
    }
    if (dr == 0 || dc == 0) {
      return pathClear(move.from, move.to, dr == 0 ? 0 : (dr > 0 ? 1 : -1),
                       dc == 0 ? 0 : (dc > 0 ? 1 : -1));
    }
    return false;
  case PieceType::King:
    if (abs(dr) <= 1 && abs(dc) <= 1) {
      return true;
    }
    if (dr != 0 || abs(dc) != 2 || isInCheck(moving.color)) {
      return false;
    }
    if (moving.color == PieceColor::White) {
      if (move.from != 60) {
        return false;
      }
      if (dc == 2) {
        return !whiteKingMoved && !whiteKingsideRookMoved &&
               board[63].present && board[63].type == PieceType::Rook &&
               board[63].color == PieceColor::White &&
               !board[61].present && !board[62].present &&
               !isSquareAttacked(61, PieceColor::Black) &&
               !isSquareAttacked(62, PieceColor::Black);
      }
      return !whiteKingMoved && !whiteQueensideRookMoved && board[56].present &&
             board[56].type == PieceType::Rook &&
             board[56].color == PieceColor::White && !board[57].present &&
             !board[58].present && !board[59].present &&
             !isSquareAttacked(59, PieceColor::Black) &&
             !isSquareAttacked(58, PieceColor::Black);
    }
    if (move.from != 4) {
      return false;
    }
    if (dc == 2) {
      return !blackKingMoved && !blackKingsideRookMoved && board[7].present &&
             board[7].type == PieceType::Rook &&
             board[7].color == PieceColor::Black && !board[5].present &&
             !board[6].present && !isSquareAttacked(5, PieceColor::White) &&
             !isSquareAttacked(6, PieceColor::White);
    }
    return !blackKingMoved && !blackQueensideRookMoved && board[0].present &&
           board[0].type == PieceType::Rook &&
           board[0].color == PieceColor::Black && !board[1].present &&
           !board[2].present && !board[3].present &&
           !isSquareAttacked(3, PieceColor::White) &&
           !isSquareAttacked(2, PieceColor::White);
  case PieceType::Checker:
    return false;
  }
  return false;
}

bool ChessApp::pathClear(int from, int to, int rowStep, int colStep) const {
  int row = from / 8 + rowStep;
  int col = from % 8 + colStep;
  int toRow = to / 8;
  int toCol = to % 8;
  while (row != toRow || col != toCol) {
    if (board[row * 8 + col].present) {
      return false;
    }
    row += rowStep;
    col += colStep;
  }
  return true;
}

bool ChessApp::isSquareAttacked(int square, PieceColor byColor) const {
  return isSquareAttackedOnBoard(board, square, byColor);
}

bool ChessApp::isSquareAttackedOnBoard(const Piece *position, int square,
                                       PieceColor byColor) const {
  int row = square / 8;
  int col = square % 8;

  int pawnRow = row + (byColor == PieceColor::White ? 1 : -1);
  if (pawnRow >= 0 && pawnRow < 8) {
    for (int dc = -1; dc <= 1; dc += 2) {
      int c = col + dc;
      if (c >= 0 && c < 8) {
        const Piece &p = position[pawnRow * 8 + c];
        if (p.present && p.color == byColor && p.type == PieceType::Pawn) {
          return true;
        }
      }
    }
  }

  const int knightMoves[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                                 {1, -2},  {1, 2},  {2, -1},  {2, 1}};
  for (int i = 0; i < 8; ++i) {
    int r = row + knightMoves[i][0];
    int c = col + knightMoves[i][1];
    if (r >= 0 && r < 8 && c >= 0 && c < 8) {
      const Piece &p = position[r * 8 + c];
      if (p.present && p.color == byColor && p.type == PieceType::Knight) {
        return true;
      }
    }
  }

  const int dirs[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
                          {0, 1},   {1, -1}, {1, 0},  {1, 1}};
  for (int i = 0; i < 8; ++i) {
    int r = row + dirs[i][0];
    int c = col + dirs[i][1];
    int distance = 1;
    while (r >= 0 && r < 8 && c >= 0 && c < 8) {
      const Piece &p = position[r * 8 + c];
      if (p.present) {
        if (p.color == byColor) {
          bool diagonal = dirs[i][0] != 0 && dirs[i][1] != 0;
          if (p.type == PieceType::King && distance == 1) {
            return true;
          }
          if (diagonal &&
              (p.type == PieceType::Bishop || p.type == PieceType::Queen)) {
            return true;
          }
          if (!diagonal &&
              (p.type == PieceType::Rook || p.type == PieceType::Queen)) {
            return true;
          }
        }
        break;
      }
      r += dirs[i][0];
      c += dirs[i][1];
      distance++;
    }
  }
  return false;
}

int ChessApp::findKing(PieceColor color) const {
  return findKingOnBoard(board, color);
}

int ChessApp::findKingOnBoard(const Piece *position, PieceColor color) const {
  for (int i = 0; i < 64; ++i) {
    if (position[i].present && position[i].color == color &&
        position[i].type == PieceType::King) {
      return i;
    }
  }
  return -1;
}

bool ChessApp::isInCheck(PieceColor color) const {
  int king = findKing(color);
  return king >= 0 && isSquareAttacked(king, opposite(color));
}

bool ChessApp::hasAnyLegalMove(PieceColor color) const {
  Move moves[1];
  int count = 0;
  return collectLegalMoves(color, moves, 1, count);
}

bool ChessApp::collectLegalMoves(PieceColor color, Move *moves, int maxMoves,
                                 int &count) const {
  count = 0;
  if (color != turn) {
    return false;
  }
  for (int from = 0; from < 64; ++from) {
    if (!board[from].present || board[from].color != color) {
      continue;
    }
    for (int to = 0; to < 64; ++to) {
      Move move = {static_cast<int8_t>(from), static_cast<int8_t>(to),
                   PieceType::Queen, true};
      if (isLegalMove(move)) {
        if (count < maxMoves) {
          moves[count] = move;
        }
        count++;
        if (count >= maxMoves && maxMoves == 1) {
          return true;
        }
      }
    }
  }
  return count > 0;
}

void ChessApp::maybeRunAi() {
  if (!vsAi || gameOver || isHumanTurn()) {
    return;
  }
  if (aiLevel >= 3) {
    aiThinking = true;
    aiThinkingSinceMs = millis();
    setHint("AI thinking...");
    return;
  }
  Move move = chooseAiMove();
  if (move.from >= 0) {
    makeLegalMove(move, true);
  }
}

ChessApp::Move ChessApp::chooseAiMove() {
  if (aiLevel >= 3) {
    Move bookMove = {-1, -1, PieceType::Queen, true};
    if (chooseAiBookMove(bookMove)) {
      return bookMove;
    }
    return chooseAiMoveLevel3(AI3_TIME_BUDGET_MS);
  }

  Move *moves = searchMoves[0];
  int count = 0;
  collectLegalMoves(turn, moves, AI_MOVE_LIMIT, count);
  if (count <= 0) {
    return {-1, -1, PieceType::Queen, true};
  }
  int storedCount = count < AI_MOVE_LIMIT ? count : AI_MOVE_LIMIT;

  int bestIndex = 0;
  int bestScore = -30000;
  for (int i = 0; i < storedCount; ++i) {
    int score = aiLevel >= 2 ? scoreMoveForLevel3(moves[i], turn)
                             : moveScore(moves[i], turn);
    if (score > bestScore) {
      bestScore = score;
      bestIndex = i;
    }
  }
  return moves[bestIndex];
}

bool ChessApp::chooseAiBookMove(Move &outMove) {
  if (!aiBookEnabled || historyCount >= AI3_BOOK_PLY_LIMIT) {
    aiBookEnabled = false;
    return false;
  }

  Move candidates[16];
  int candidateCount = 0;

  const int openingCount =
      static_cast<int>(sizeof(AI3_OPENINGS) / sizeof(AI3_OPENINGS[0]));
  for (int line = 0; line < openingCount; ++line) {
    const char *opening = AI3_OPENINGS[line];
    bool prefixMatches = true;
    BookPly expected = {};
    for (int ply = 0; ply < historyCount; ++ply) {
      if (!parseBookMoveToken(opening, ply, expected) ||
          historyFrom[ply] != expected.from || historyTo[ply] != expected.to) {
        prefixMatches = false;
        break;
      }
    }
    if (!prefixMatches) {
      continue;
    }

    BookPly next = {};
    if (!parseBookMoveToken(opening, historyCount, next)) {
      continue;
    }
    const Piece &fromPiece = board[next.from];
    if (!fromPiece.present || fromPiece.color != turn) {
      continue;
    }

    Move candidate = {static_cast<int8_t>(next.from), static_cast<int8_t>(next.to),
                      next.promotion, next.hasPromotion};
    if (!isLegalMove(candidate)) {
      continue;
    }

    if (candidateCount < static_cast<int>(sizeof(candidates) / sizeof(candidates[0]))) {
      candidates[candidateCount++] = candidate;
    }
  }

  if (candidateCount <= 0) {
    aiBookEnabled = false;
    return false;
  }

  outMove = candidates[random(candidateCount)];
  return true;
}

ChessApp::Move ChessApp::chooseAiMoveLevel3(unsigned long budgetMs) {
  Move *rootMoves = searchMoves[0];
  int16_t *ordering = searchOrdering[0];
  int count = 0;
  collectLegalMoves(turn, rootMoves, AI_MOVE_LIMIT, count);
  if (count <= 0) {
    return {-1, -1, PieceType::Queen, true};
  }
  int storedCount = count < AI_MOVE_LIMIT ? count : AI_MOVE_LIMIT;
  if (storedCount == 1) {
    return rootMoves[0];
  }

  PieceColor aiColor = turn;
  Move bestMove = rootMoves[0];
  unsigned long deadlineMs = millis() + budgetMs;

  for (uint8_t depth = 1; depth <= AI3_MAX_DEPTH; ++depth) {
    if (millis() >= deadlineMs) {
      break;
    }

    Move depthBestMove = bestMove;
    int depthBestScore = -AI_INF_SCORE;
    bool timedOut = false;
    for (int i = 0; i < storedCount; ++i) {
      ordering[i] = static_cast<int16_t>(scoreMoveForLevel3(rootMoves[i], aiColor));
    }
    for (int i = 0; i < storedCount; ++i) {
      int bestOrderIndex = i;
      for (int j = i + 1; j < storedCount; ++j) {
        if (ordering[j] > ordering[bestOrderIndex]) {
          bestOrderIndex = j;
        }
      }
      if (bestOrderIndex != i) {
        Move swapMove = rootMoves[i];
        rootMoves[i] = rootMoves[bestOrderIndex];
        rootMoves[bestOrderIndex] = swapMove;
        int swapOrder = ordering[i];
        ordering[i] = ordering[bestOrderIndex];
        ordering[bestOrderIndex] = swapOrder;
      }
    }

    for (int i = 0; i < storedCount; ++i) {
      if (millis() >= deadlineMs) {
        timedOut = true;
        break;
      }
      saveSearchSnapshot(0);
      applySearchMove(rootMoves[i]);
      int score = alphaBetaSearch(depth - 1, -AI_INF_SCORE, AI_INF_SCORE,
                                  aiColor, 1, deadlineMs, timedOut);
      restoreSearchSnapshot(0);
      if (timedOut) {
        break;
      }
      if (score > depthBestScore) {
        depthBestScore = score;
        depthBestMove = rootMoves[i];
      }
    }

    if (timedOut) {
      break;
    }
    bestMove = depthBestMove;
  }

  return bestMove;
}

int ChessApp::alphaBetaSearch(uint8_t depth, int alpha, int beta,
                              PieceColor aiColor, uint8_t ply,
                              unsigned long deadlineMs, bool &timedOut) {
  if (timedOut || millis() >= deadlineMs) {
    timedOut = true;
    return evaluateBoard(aiColor);
  }

  if (ply >= SEARCH_MAX_PLY) {
    return evaluateBoard(aiColor);
  }

  Move *moves = searchMoves[ply];
  int16_t *ordering = searchOrdering[ply];
  int count = 0;
  collectLegalMoves(turn, moves, AI_MOVE_LIMIT, count);
  int storedCount = count < AI_MOVE_LIMIT ? count : AI_MOVE_LIMIT;
  if (depth == 0 || storedCount == 0) {
    if (storedCount == 0) {
      if (isInCheck(turn)) {
        return turn == aiColor ? -AI_MATE_SCORE + ply : AI_MATE_SCORE - ply;
      }
      return 0;
    }
    return evaluateBoard(aiColor);
  }

  for (int i = 0; i < storedCount; ++i) {
    ordering[i] = static_cast<int16_t>(scoreMoveForLevel3(moves[i], turn));
  }
  for (int i = 0; i < storedCount; ++i) {
    int bestOrderIndex = i;
    for (int j = i + 1; j < storedCount; ++j) {
      if (ordering[j] > ordering[bestOrderIndex]) {
        bestOrderIndex = j;
      }
    }
    if (bestOrderIndex != i) {
      Move swapMove = moves[i];
      moves[i] = moves[bestOrderIndex];
      moves[bestOrderIndex] = swapMove;
      int swapOrder = ordering[i];
      ordering[i] = ordering[bestOrderIndex];
      ordering[bestOrderIndex] = swapOrder;
    }
  }

  bool maximizing = turn == aiColor;
  int best = maximizing ? -AI_INF_SCORE : AI_INF_SCORE;
  int explored = 0;

  for (int i = 0; i < storedCount; ++i) {
    if (millis() >= deadlineMs) {
      timedOut = true;
      break;
    }
    saveSearchSnapshot(ply);
    applySearchMove(moves[i]);
    int score =
        alphaBetaSearch(depth - 1, alpha, beta, aiColor, ply + 1, deadlineMs,
                        timedOut);
    restoreSearchSnapshot(ply);
    if (timedOut) {
      break;
    }
    explored++;
    if (maximizing) {
      if (score > best) {
        best = score;
      }
      if (best > alpha) {
        alpha = best;
      }
    } else {
      if (score < best) {
        best = score;
      }
      if (best < beta) {
        beta = best;
      }
    }
    if (beta <= alpha) {
      break;
    }
  }

  if (explored == 0) {
    return evaluateBoard(aiColor);
  }
  return best;
}

int ChessApp::scoreMoveForLevel3(const Move &move, PieceColor perspective) const {
  int score = 0;
  const Piece &target = board[move.to];
  if (target.present) {
    score += pieceValue(target.type);
  }
  int centerDistance = abs((move.to / 8) - 3) + abs((move.to % 8) - 3);
  score += 8 - centerDistance;
  score -= destinationRiskAfter(move, perspective);
  if (hasLastMove && move.from == lastMoveTo && move.to == lastMoveFrom) {
    score -= 250;
  }
  return score;
}

void ChessApp::saveSearchSnapshot(uint8_t ply) {
  SearchSnapshot &snapshot = searchSnapshots[ply];
  for (int i = 0; i < 64; ++i) {
    snapshot.board[i] = board[i];
  }
  snapshot.turn = turn;
  snapshot.enPassantSquare = enPassantSquare;
  snapshot.whiteKingMoved = whiteKingMoved;
  snapshot.blackKingMoved = blackKingMoved;
  snapshot.whiteKingsideRookMoved = whiteKingsideRookMoved;
  snapshot.whiteQueensideRookMoved = whiteQueensideRookMoved;
  snapshot.blackKingsideRookMoved = blackKingsideRookMoved;
  snapshot.blackQueensideRookMoved = blackQueensideRookMoved;
}

void ChessApp::restoreSearchSnapshot(uint8_t ply) {
  const SearchSnapshot &snapshot = searchSnapshots[ply];
  for (int i = 0; i < 64; ++i) {
    board[i] = snapshot.board[i];
  }
  turn = snapshot.turn;
  enPassantSquare = snapshot.enPassantSquare;
  whiteKingMoved = snapshot.whiteKingMoved;
  blackKingMoved = snapshot.blackKingMoved;
  whiteKingsideRookMoved = snapshot.whiteKingsideRookMoved;
  whiteQueensideRookMoved = snapshot.whiteQueensideRookMoved;
  blackKingsideRookMoved = snapshot.blackKingsideRookMoved;
  blackQueensideRookMoved = snapshot.blackQueensideRookMoved;
}

void ChessApp::applySearchMove(const Move &move) {
  Piece moving = board[move.from];
  Piece captured = board[move.to];
  if (moving.type == PieceType::Pawn && move.to == enPassantSquare &&
      !captured.present) {
    int capturedSquare = move.to + (moving.color == PieceColor::White ? 8 : -8);
    captured = board[capturedSquare];
  }
  applyMoveUnchecked(move, moving, captured, false);
  turn = opposite(turn);
}

void ChessApp::buildPositionAfterMove(const Move &move, Piece *position) const {
  for (int i = 0; i < 64; ++i) {
    position[i] = board[i];
  }

  Piece moving = board[move.from];
  int oldEnPassant = enPassantSquare;
  if (moving.type == PieceType::Pawn && move.to == oldEnPassant &&
      !position[move.to].present) {
    int capturedSquare = move.to + (moving.color == PieceColor::White ? 8 : -8);
    position[capturedSquare] = {};
  }

  position[move.to] = moving;
  position[move.from] = {};

  int toRow = move.to / 8;
  if (moving.type == PieceType::Pawn && (toRow == 0 || toRow == 7)) {
    position[move.to].type =
        move.hasPromotion ? move.promotion : PieceType::Queen;
  }

  if (moving.type == PieceType::King && move.to - move.from == 2) {
    position[move.from + 1] = position[move.from + 3];
    position[move.from + 3] = {};
  } else if (moving.type == PieceType::King && move.from - move.to == 2) {
    position[move.from - 1] = position[move.from - 4];
    position[move.from - 4] = {};
  }
}

int ChessApp::destinationRiskAfter(const Move &move,
                                   PieceColor perspective) const {
  Piece position[64];
  buildPositionAfterMove(move, position);

  Piece moving = board[move.from];
  PieceColor enemy = opposite(perspective);
  if (!isSquareAttackedOnBoard(position, move.to, enemy)) {
    return 0;
  }

  int penalty = pieceValue(moving.type);
  if (board[move.to].present) {
    penalty -= pieceValue(board[move.to].type);
  }
  return penalty > 0 ? penalty : 0;
}

int ChessApp::moveScore(const Move &move, PieceColor perspective) const {
  int score = 0;
  const Piece &target = board[move.to];
  if (target.present) {
    score += pieceValue(target.type) * 10 - pieceValue(board[move.from].type);
  }
  int centerDistance = abs((move.to / 8) - 3) + abs((move.to % 8) - 3);
  score += 8 - centerDistance;
  if (board[move.from].color != perspective) {
    score = -score;
  }
  return score;
}

int ChessApp::evaluateBoard(PieceColor perspective) const {
  int score = 0;
  for (int i = 0; i < 64; ++i) {
    if (!board[i].present) {
      continue;
    }
    int value = pieceValue(board[i].type);
    score += board[i].color == perspective ? value : -value;
  }
  return score;
}

int ChessApp::pieceValue(PieceType type) const {
  switch (type) {
  case PieceType::Pawn:
    return 100;
  case PieceType::Knight:
  case PieceType::Bishop:
    return 320;
  case PieceType::Rook:
    return 500;
  case PieceType::Queen:
    return 900;
  case PieceType::King:
    return 20000;
  case PieceType::Checker:
    return 0;
  }
  return 0;
}

void ChessApp::appendHistory(const Move &move, Piece moving, Piece captured) {
  if (historyCount >= 96) {
    historyReplayable = false;
    for (int i = 1; i < 96; ++i) {
      strcpy(history[i - 1], history[i]);
      historyPiece[i - 1] = historyPiece[i];
      historyColor[i - 1] = historyColor[i];
      historyCapturedPiece[i - 1] = historyCapturedPiece[i];
      historyCapturedColor[i - 1] = historyCapturedColor[i];
      historyHasCapture[i - 1] = historyHasCapture[i];
      historyFrom[i - 1] = historyFrom[i];
      historyTo[i - 1] = historyTo[i];
    }
    historyCount = 95;
  }
  char from[3];
  char to[3];
  squareName(move.from, from);
  squareName(move.to, to);
  const char letter = moving.type == PieceType::King     ? 'K'
                      : moving.type == PieceType::Queen  ? 'Q'
                      : moving.type == PieceType::Rook   ? 'R'
                      : moving.type == PieceType::Bishop ? 'B'
                      : moving.type == PieceType::Knight ? 'N'
                                                        : 'P';
  int written =
      snprintf(history[historyCount], sizeof(history[historyCount]), "%c%s%c%s",
               letter, from, captured.present ? 'X' : '-', to);
  if (moving.type == PieceType::Pawn && isPromotionMove(move) && written > 0 &&
      written < static_cast<int>(sizeof(history[historyCount]) - 1)) {
    history[historyCount][written] =
        move.promotion == PieceType::Rook     ? 'R'
        : move.promotion == PieceType::Bishop ? 'B'
        : move.promotion == PieceType::Knight ? 'N'
                                              : 'Q';
    history[historyCount][written + 1] = '\0';
  }
  historyPiece[historyCount] = moving.type;
  historyColor[historyCount] = moving.color;
  historyHasCapture[historyCount] = captured.present;
  historyCapturedPiece[historyCount] = captured.type;
  historyCapturedColor[historyCount] = captured.color;
  historyFrom[historyCount] = move.from;
  historyTo[historyCount] = move.to;
  historyCount++;
  historyOffset = historyCount > 7 ? historyCount - 7 : 0;
}

void ChessApp::appendHistorySuffix(char suffix) {
  if (historyCount == 0) {
    return;
  }
  char *entry = history[historyCount - 1];
  size_t len = strlen(entry);
  if (len + 1 >= sizeof(history[historyCount - 1])) {
    return;
  }
  entry[len] = suffix;
  entry[len + 1] = '\0';
}

void ChessApp::squareName(int square, char *out) const {
  out[0] = 'A' + (square % 8);
  out[1] = '8' - (square / 8);
  out[2] = '\0';
}
