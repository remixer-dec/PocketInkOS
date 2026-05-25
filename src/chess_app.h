#ifndef CHESS_APP_H
#define CHESS_APP_H

#include "touch_input.h"
#include <Adafruit_GFX.h>
#include <cstdint>

enum class PieceType {
  King,
  Queen,
  Rook,
  Bishop,
  Knight,
  Pawn,
  Checker
};

enum class PieceColor { White, Black };

enum class BoardColor { Light, Dark };

struct CaseFontGlyph {
  char32_t glyph;
  uint32_t codepoint;
  PieceType piece;
  PieceColor pieceColor;
  BoardColor boardColor;
};

static constexpr CaseFontGlyph CASEFONT_PIECES[] = {
    {U'V', 86, PieceType::Bishop, PieceColor::White, BoardColor::Dark},
    {U'L', 76, PieceType::King, PieceColor::White, BoardColor::Dark},
    {U'K', 75, PieceType::King, PieceColor::Black, BoardColor::Dark},
    {U'N', 78, PieceType::Knight, PieceColor::Black, BoardColor::Dark},
    {U'M', 77, PieceType::Knight, PieceColor::White, BoardColor::Dark},
    {U'P', 80, PieceType::Pawn, PieceColor::Black, BoardColor::Dark},
    {U'O', 79, PieceType::Pawn, PieceColor::White, BoardColor::Dark},
    {U'W', 87, PieceType::Queen, PieceColor::White, BoardColor::Dark},
    {U'T', 84, PieceType::Rook, PieceColor::White, BoardColor::Dark},
    {U'R', 82, PieceType::Rook, PieceColor::Black, BoardColor::Dark},
    {U'B', 66, PieceType::Bishop, PieceColor::Black, BoardColor::Dark},
    {U'Q', 81, PieceType::Queen, PieceColor::Black, BoardColor::Dark},
    {U'b', 98, PieceType::Bishop, PieceColor::White, BoardColor::Light},
    {U'k', 107, PieceType::King, PieceColor::White, BoardColor::Light},
    {U'l', 108, PieceType::King, PieceColor::Black, BoardColor::Light},
    {U'm', 109, PieceType::Knight, PieceColor::Black, BoardColor::Light},
    {U'n', 110, PieceType::Knight, PieceColor::White, BoardColor::Light},
    {U'o', 111, PieceType::Pawn, PieceColor::Black, BoardColor::Light},
    {U'p', 112, PieceType::Pawn, PieceColor::White, BoardColor::Light},
    {U'q', 113, PieceType::Queen, PieceColor::White, BoardColor::Light},
    {U'r', 114, PieceType::Rook, PieceColor::White, BoardColor::Light},
    {U't', 116, PieceType::Rook, PieceColor::Black, BoardColor::Light},
    {U'v', 118, PieceType::Bishop, PieceColor::Black, BoardColor::Light},
    {U'w', 119, PieceType::Queen, PieceColor::Black, BoardColor::Light},
    {U'.', 46, PieceType::Checker, PieceColor::Black, BoardColor::Light},
    {U':', 58, PieceType::Checker, PieceColor::Black, BoardColor::Dark},
};

class ChessApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool hasActiveSession() const;

private:
  struct Piece {
    PieceType type;
    PieceColor color;
    bool present;
  };

  Piece board[64] = {};

  void placeInitialPieces();
  void drawBoard(Adafruit_GFX &gfx);
  void drawPiece(Adafruit_GFX &gfx, int row, int col, const Piece &piece);
  const CaseFontGlyph *findGlyph(PieceType type, PieceColor color,
                                 BoardColor boardColor) const;
};

#endif
