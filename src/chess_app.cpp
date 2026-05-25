#include "chess_app.h"
#include "chess_fonts.h"

static const int BOARD_X = 8;
static const int BOARD_Y = 8;
static const int BOARD_SIZE = 184;
static const int CELL = BOARD_SIZE / 8;

void ChessApp::reset() { placeInitialPieces(); }

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

bool ChessApp::hasActiveSession() const { return false; }

bool ChessApp::handleTouch(const TouchPoint &point) {
  (void)point;
  return false;
}

void ChessApp::draw(Adafruit_GFX &gfx) {
  drawBoard(gfx);
}

void ChessApp::drawBoard(Adafruit_GFX &gfx) {
  gfx.drawRect(BOARD_X - 1, BOARD_Y - 1, BOARD_SIZE + 2, BOARD_SIZE + 2, 1);

  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 8; ++col) {
      BoardColor boardColor = ((row + col) % 2 == 0) ? BoardColor::Light
                                                      : BoardColor::Dark;
      int x = BOARD_X + col * CELL;
      int y = BOARD_Y + row * CELL;
      gfx.fillRect(x, y, CELL, CELL,
                   boardColor == BoardColor::Dark ? 1 : 0);
      gfx.drawRect(x, y, CELL, CELL, 1);

      const Piece &piece = board[row * 8 + col];
      if (piece.present) {
        drawPiece(gfx, row, col, piece);
      }
    }
  }
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
  const CaseFontGlyph *glyph =
      findGlyph(piece.type, piece.color, boardColor);
  if (!glyph) {
    return;
  }

  char text[2] = {static_cast<char>(glyph->codepoint), 0};
  gfx.setFont(&CHESS_CASEFONT12pt7b);
  gfx.setTextSize(1);
  gfx.setTextColor(boardColor == BoardColor::Dark ? 0 : 1);

  int x = BOARD_X + col * CELL;
  int y = BOARD_Y + row * CELL;
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
