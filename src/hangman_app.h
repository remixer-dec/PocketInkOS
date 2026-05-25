#ifndef HANGMAN_APP_H
#define HANGMAN_APP_H

#include "t9_keyboard_component.h"
#include "touch_input.h"
#include <Adafruit_GFX.h>

class HangmanApp {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool openKeyboardFromButton();
  bool hasActiveSession() const;

private:
  enum State {
    STATE_INTRO,
    STATE_ENTER_WORD,
    STATE_GUESSING,
    STATE_WON,
    STATE_LOST
  };

  State state = STATE_INTRO;
  char word[13] = {0};
  String inputText;
  T9KeyboardComponent inputKeyboard;
  bool guessed[26] = {false};
  bool keyboardOpen = false;
  int misses = 0;

  void startCpuGame();
  void startPlayerGame();
  void beginGuessing(const char *secret);
  void drawIntro(Adafruit_GFX &gfx);
  void drawEntry(Adafruit_GFX &gfx);
  void drawGame(Adafruit_GFX &gfx);
  void drawGallows(Adafruit_GFX &gfx);
  bool handleKeyboardTouch(const TouchPoint &point);
  void openKeyboard();
  void submitInput();
  void guessLetter(char letter);
  void copySanitizedWord(char *target, int targetSize) const;
  char firstInputLetter() const;
  bool containsLetter(char letter) const;
  bool allLettersGuessed() const;
};

#endif
