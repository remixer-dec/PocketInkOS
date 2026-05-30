#ifndef HANGMAN_APP_H
#define HANGMAN_APP_H

#include "ui/components/menu_button_consumer.h"
#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"
#include "ui/t9_keyboard/t9_keyboard_component.h"
#include "sys/touch_input.h"
#include <Adafruit_GFX.h>

class HangmanApp : public MenuButtonConsumer {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool openKeyboardFromButton();
  bool handleMenuButton();
  bool handleMenuDoubleButton();
  bool handleMenuLongButton();
  bool hasActiveSession() const;

private:
  enum State {
    STATE_INTRO,
    STATE_ENTER_WORD,
    STATE_GUESSING,
    STATE_WON,
    STATE_LOST
  };
  enum KeyboardMode { KEYBOARD_T9, KEYBOARD_QWERTY_ZOOM };

  State state = STATE_INTRO;
  char word[13] = {0};
  String inputText;
  T9KeyboardComponent inputKeyboard;
  QwertyZoomKeyboardComponent zoomKeyboard;
  KeyboardMode keyboardMode = KEYBOARD_T9;
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
  void drawKeyboard(Adafruit_GFX &gfx);
  void submitInput();
  void guessLetter(char letter);
  void copySanitizedWord(char *target, int targetSize) const;
  char firstInputLetter() const;
  bool containsLetter(char letter) const;
  bool allLettersGuessed() const;
};

#endif
