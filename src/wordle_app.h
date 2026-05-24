#ifndef WORDLE_APP_H
#define WORDLE_APP_H

#include "t9_keyboard_component.h"
#include "touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

class WordleApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool handleTouch(const TouchPoint &point);
  bool openKeyboardFromButton();
  bool hasActiveSession() const;

private:
  enum State { STATE_INTRO, STATE_ENTER_WORD, STATE_GUESSING, STATE_WON, STATE_LOST };
  enum Mark { MARK_NONE, MARK_ABSENT, MARK_PRESENT, MARK_CORRECT };

  State state = STATE_INTRO;
  char target[6] = {0};
  char guesses[6][6] = {{0}};
  uint8_t marks[6][5] = {{0}};
  uint8_t keyMarks[26] = {0};
  String inputText;
  int row = 0;
  bool keyboardOpen = false;

  void startCpuGame();
  void startPlayerGame();
  void beginGuessing(const char *word);
  void drawIntro(Adafruit_GFX &gfx);
  void drawEntry(Adafruit_GFX &gfx);
  void drawGame(Adafruit_GFX &gfx);
  void drawBoard(Adafruit_GFX &gfx);
  void drawTile(Adafruit_GFX &gfx, int x, int y, char letter, uint8_t mark,
                bool active);
  bool handleKeyboardTouch(const TouchPoint &point);
  void openKeyboard();
  void submit();
  void copyCurrentWord(char *word) const;
  void evaluateGuess();
  void setKeyMark(char letter, uint8_t mark);
  T9KeyboardComponent inputKeyboard;
};

#endif
