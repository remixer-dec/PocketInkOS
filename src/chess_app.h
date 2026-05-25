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
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handlePowerButton();
  bool handleMenuButton();
  bool handleMenuLongPress();
  bool hasActiveSession() const;
  bool isGameOver() const;
  bool loadPosition(const char *fen, PieceColor sideToMove);

private:
  struct Piece {
    PieceType type;
    PieceColor color;
    bool present;
  };

  enum class Mode { Setup, Playing, History };
  enum class PendingSelection { Source, Target };

  struct Move {
    int from;
    int to;
    PieceType promotion;
    bool hasPromotion;
  };

  Piece board[64] = {};
  Mode mode = Mode::Setup;
  PendingSelection pendingSelection = PendingSelection::Source;
  PieceColor turn = PieceColor::White;
  PieceColor humanColor = PieceColor::White;
  bool vsAi = false;
  uint8_t aiLevel = 1;
  bool sourceConfirmed = false;
  bool targetConfirmed = false;
  bool awaitingPromotion = false;
  int selectedFrom = -1;
  int selectedTo = -1;
  Move promotionMove = {-1, -1, PieceType::Queen, true};
  int enPassantSquare = -1;
  bool whiteKingMoved = false;
  bool blackKingMoved = false;
  bool whiteKingsideRookMoved = false;
  bool whiteQueensideRookMoved = false;
  bool blackKingsideRookMoved = false;
  bool blackQueensideRookMoved = false;
  bool gameOver = false;
  bool started = false;
  bool alertVisible = false;
  char alertText[32] = {};
  char hintText[24] = {};
  char history[96][10] = {};
  PieceType historyPiece[96] = {};
  PieceColor historyColor[96] = {};
  PieceType historyCapturedPiece[96] = {};
  PieceColor historyCapturedColor[96] = {};
  bool historyHasCapture[96] = {};
  uint8_t historyCount = 0;
  uint8_t historyOffset = 0;
  unsigned long lastAnimationMs = 0;
  bool selectionBorderPhase = false;
  bool hasLastMove = false;
  int lastMoveFrom = -1;
  int lastMoveTo = -1;
  PieceColor lastMoveColor = PieceColor::White;

  void placeInitialPieces();
  void resetGameState();
  void startGame();
  void clearSelection();
  void setAlert(const char *text);
  void setHint(const char *text);
  void maybeRunAi();
  bool isHumanTurn() const;
  bool isInsideBoardPoint(const TouchPoint &point) const;
  int pointToSquare(const TouchPoint &point) const;
  int resolveSourceSquare(int square) const;
  int displayRowToBoardRow(int displayRow) const;
  int boardRowToDisplayRow(int boardRow) const;
  void drawBoard(Adafruit_GFX &gfx);
  void drawExperimentalCoordinates(Adafruit_GFX &gfx);
  void drawSetup(Adafruit_GFX &gfx);
  void drawHistory(Adafruit_GFX &gfx);
  void drawStatus(Adafruit_GFX &gfx);
  void drawMoveOutline(Adafruit_GFX &gfx, int square);
  void drawSelection(Adafruit_GFX &gfx, int square);
  void drawPromotionChooser(Adafruit_GFX &gfx);
  void drawAlert(Adafruit_GFX &gfx);
  void drawPiece(Adafruit_GFX &gfx, int row, int col, const Piece &piece);
  const CaseFontGlyph *findGlyph(PieceType type, PieceColor color,
                                 BoardColor boardColor) const;
  bool tryMoveSelection();
  bool isPromotionMove(const Move &move) const;
  bool finishPromotion(PieceType promotion);
  bool makeLegalMove(const Move &move, bool recordHistory);
  void applyMoveUnchecked(const Move &move, Piece moving, Piece captured,
                          bool simulation);
  bool isLegalMove(const Move &move) const;
  bool isPseudoLegalMove(const Move &move) const;
  bool wouldLeaveKingInCheck(const Move &move) const;
  bool pathClear(int from, int to, int rowStep, int colStep) const;
  bool isSquareAttacked(int square, PieceColor byColor) const;
  bool isSquareAttackedOnBoard(const Piece *position, int square,
                               PieceColor byColor) const;
  bool isInCheck(PieceColor color) const;
  int findKingOnBoard(const Piece *position, PieceColor color) const;
  bool hasAnyLegalMove(PieceColor color) const;
  int findKing(PieceColor color) const;
  bool collectLegalMoves(PieceColor color, Move *moves, int maxMoves,
                         int &count) const;
  Move chooseAiMove() const;
  int evaluateBoard(PieceColor perspective) const;
  int moveScore(const Move &move, PieceColor perspective) const;
  int pieceValue(PieceType type) const;
  void appendHistory(const Move &move, Piece moving, Piece captured);
  void appendHistorySuffix(char suffix);
  void squareName(int square, char *out) const;
  PieceColor opposite(PieceColor color) const;
};

#endif
