#include "app_display.h"
#include "cube_app.h"
#include "hangman_app.h"
#include "keyboard_component.h"
#include "minesweeper_app.h"
#include "qwerty_zoom_keyboard_component.h"
#include "smart_button.h"
#include "sudoku_app.h"
#include "tictactoe_app.h"
#include "t9_keyboard_component.h"
#include "touch_input.h"
#include "ui_helpers.h"
#include "wordle_app.h"
#include <stdio.h>

enum Screen {
  SCREEN_HOME,
  SCREEN_KEYBOARD,
  SCREEN_T9_KEYBOARD,
  SCREEN_QWERTY_ZOOM_KEYBOARD,
  SCREEN_MENU,
  SCREEN_TICTACTOE,
  SCREEN_MINESWEEPER,
  SCREEN_HANGMAN,
  SCREEN_SUDOKU,
  SCREEN_WORDLE,
  SCREEN_CUBE
};

AppDisplay display;
TouchInput touch;
CubeApp cubeApp;
KeyboardComponent keyboard;
QwertyZoomKeyboardComponent qwertyZoomKeyboard;
T9KeyboardComponent t9Keyboard;
TicTacToeApp ticTacToe;
MinesweeperApp minesweeper;
HangmanApp hangman;
SudokuApp sudoku;
WordleApp wordle;
SmartButton mainButton(BOOT_BUTTON_PIN);
SmartButton backButton(PWR_BUTTON_PIN);

Screen screen = SCREEN_HOME;
String typedText;
bool dirty = true;
bool confirmQuit = false;
unsigned long lastHomeSecond = 0;

void switchTo(Screen next) {
  screen = next;
  confirmQuit = false;
  dirty = true;
}

bool isSessionScreen() {
  switch (screen) {
  case SCREEN_TICTACTOE:
    return ticTacToe.hasActiveSession();
  case SCREEN_MINESWEEPER:
    return minesweeper.hasActiveSession();
  case SCREEN_HANGMAN:
    return hangman.hasActiveSession();
  case SCREEN_SUDOKU:
    return sudoku.hasActiveSession();
  case SCREEN_WORDLE:
    return wordle.hasActiveSession();
  case SCREEN_CUBE:
    return cubeApp.hasActiveSession();
  default:
    return false;
  }
}

void handleMenuButton() {
  if (confirmQuit) {
    return;
  }
  if (isSessionScreen()) {
    confirmQuit = true;
    dirty = true;
    return;
  }
  switchTo(screen == SCREEN_MENU ? SCREEN_HOME : SCREEN_MENU);
}

void switchKeyboardMode() {
  if (screen == SCREEN_T9_KEYBOARD) {
    switchTo(SCREEN_QWERTY_ZOOM_KEYBOARD);
    return;
  }
  switchTo(SCREEN_T9_KEYBOARD);
}

void handleOtherButton() {
  if (screen == SCREEN_HANGMAN) {
    if (hangman.openKeyboardFromButton()) {
      dirty = true;
    }
    return;
  }
  if (screen == SCREEN_WORDLE) {
    if (wordle.openKeyboardFromButton()) {
      dirty = true;
    }
    return;
  }
  if (screen == SCREEN_TICTACTOE || screen == SCREEN_MINESWEEPER ||
      screen == SCREEN_SUDOKU || screen == SCREEN_CUBE) {
    return;
  }
  switchKeyboardMode();
}

void formatSeconds(unsigned long totalSeconds, char *buffer, size_t size) {
  unsigned long hours = totalSeconds / 3600;
  unsigned long minutes = (totalSeconds / 60) % 60;
  unsigned long seconds = totalSeconds % 60;
  snprintf(buffer, size, "%02lu:%02lu:%02lu", hours, minutes, seconds);
}

void drawHome() {
  display.setTextColor(1);
  display.setTextSize(2);
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  display.getTextBounds("Pocket Ink", 0, 0, &x, &y, &w, &h);
  display.setCursor((EPD_WIDTH - w) / 2, 78);
  display.print("Pocket Ink");

  char text[16];
  unsigned long uptimeSeconds = millis() / 1000;

  display.setTextSize(1);
  display.setCursor(4, 8);
  display.print("TIME --:--");

  formatSeconds(uptimeSeconds, text, sizeof(text));
  display.getTextBounds(text, 0, 0, &x, &y, &w, &h);
  display.setCursor(EPD_WIDTH - w - 4, 8);
  display.print(text);

  display.setCursor(34, 182);
  display.print("BOOT: apps  PWR: keyboard");
}

static const UiRect QUIT_YES_BUTTON = {44, 112, 48, 24};
static const UiRect QUIT_NO_BUTTON = {108, 112, 48, 24};

void drawQuitDialog() {
  display.fillRect(24, 62, 152, 86, 0);
  display.drawRect(24, 62, 152, 86, 1);
  display.setTextColor(1);
  display.setTextSize(1);
  display.setCursor(38, 78);
  display.print("Do you really");
  display.setCursor(50, 92);
  display.print("want to quit?");
  uiDrawButton(display, QUIT_YES_BUTTON, "YES");
  uiDrawButton(display, QUIT_NO_BUTTON, "NO");
}

bool handleQuitDialogTouch(const TouchPoint &point) {
  if (uiContains(QUIT_YES_BUTTON, point)) {
    switchTo(SCREEN_MENU);
    return true;
  }
  if (uiContains(QUIT_NO_BUTTON, point)) {
    confirmQuit = false;
    dirty = true;
    return true;
  }
  return true;
}

void drawMenu() {
  display.setTextColor(1);
  display.setTextSize(2);
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  display.getTextBounds("APPS", 0, 10, &titleX, &titleY, &titleW, &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2, 10);
  display.print("APPS");

  const char *icons[9] = {"T", "M", "H", "S", "W", "C", "", "", ""};
  int n = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int x = 20 + col * 60;
      int y = 42 + row * 48;
      display.drawRect(x, y, 40, 36, 1);
      display.setTextSize(2);
      display.setCursor(x + 14, y + 10);
      display.print(icons[n++]);
    }
  }

  display.setTextSize(1);
  display.setCursor(36, 188);
  display.print("PWR: keyboard");
}

void handleMenuTouch(const TouchPoint &point) {
  if (point.x < 20 || point.x >= 200 || point.y < 42 || point.y >= 186) {
    return;
  }
  int col = (point.x - 20) / 60;
  int row = (point.y - 42) / 48;
  if (col < 0 || col > 2 || row < 0 || row > 2) return;
  int app = row * 3 + col;
  switch (app) {
  case 0:
    ticTacToe.reset();
    switchTo(SCREEN_TICTACTOE);
    break;
  case 1:
    minesweeper.reset();
    switchTo(SCREEN_MINESWEEPER);
    break;
  case 2:
    hangman.reset();
    switchTo(SCREEN_HANGMAN);
    break;
  case 3:
    sudoku.reset();
    switchTo(SCREEN_SUDOKU);
    break;
  case 4:
    wordle.reset();
    switchTo(SCREEN_WORDLE);
    break;
  case 5:
    cubeApp.reset();
    switchTo(SCREEN_CUBE);
    break;
  default:
    break;
  }
}

bool applyKeyboardEvent(const KeyboardEvent &event) {
  switch (event.action) {
  case KEY_CHAR:
    if (typedText.length() < 64) typedText += event.value;
    break;
  case KEY_SPACE:
    if (typedText.length() < 64) typedText += ' ';
    break;
  case KEY_BACKSPACE:
    if (typedText.length() > 0) typedText.remove(typedText.length() - 1);
    break;
  case KEY_SHIFT:
    break;
  case KEY_NAV:
    break;
  case KEY_OK:
    break;
  case KEY_NONE:
    return false;
  }
  return true;
}

bool handleKeyboardTouch(const TouchPoint &point) {
  return applyKeyboardEvent(keyboard.hitTest(point));
}

bool handleQwertyZoomTouch(const TouchPoint &point) {
  return applyKeyboardEvent(qwertyZoomKeyboard.hitTest(point));
}

void render() {
  display.clear();
  switch (screen) {
  case SCREEN_HOME:
    drawHome();
    break;
  case SCREEN_KEYBOARD:
    keyboard.draw(display, typedText);
    break;
  case SCREEN_T9_KEYBOARD:
    t9Keyboard.draw(display, typedText);
    break;
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    qwertyZoomKeyboard.draw(display, typedText);
    break;
  case SCREEN_MENU:
    drawMenu();
    break;
  case SCREEN_TICTACTOE:
    ticTacToe.draw(display);
    break;
  case SCREEN_MINESWEEPER:
    minesweeper.draw(display);
    break;
  case SCREEN_HANGMAN:
    hangman.draw(display);
    break;
  case SCREEN_SUDOKU:
    sudoku.draw(display);
    break;
  case SCREEN_WORDLE:
    wordle.draw(display);
    break;
  case SCREEN_CUBE:
    cubeApp.draw(display);
    break;
  }
  if (confirmQuit) {
    drawQuitDialog();
  }
  display.flush();
  dirty = false;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  display.begin();
  touch.begin();

  mainButton.begin();
  backButton.begin();
  mainButton.setLongPressMs(1200);
  backButton.setLongPressMs(1200);

  mainButton.attachSingleClick([]() { handleMenuButton(); });
  mainButton.attachLongPressStart([]() { handleMenuButton(); });
  backButton.attachSingleClick([]() { handleOtherButton(); });
  backButton.attachLongPressStart([]() { handleOtherButton(); });

  ticTacToe.reset();
  minesweeper.reset();
  hangman.reset();
  sudoku.reset();
  wordle.reset();
  dirty = true;
}

void loop() {
  mainButton.update();
  backButton.update();

  TouchPoint point;
  if (touch.read(point)) {
    if (confirmQuit) {
      handleQuitDialogTouch(point);
      delay(1);
      return;
    }
    switch (screen) {
    case SCREEN_HOME:
      break;
    case SCREEN_KEYBOARD:
      if (handleKeyboardTouch(point)) {
        display.clear();
        keyboard.draw(display, typedText);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_T9_KEYBOARD:
      if (t9Keyboard.hitTest(point, typedText).action != KEY_NONE) {
        display.clear();
        t9Keyboard.draw(display, typedText);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_QWERTY_ZOOM_KEYBOARD:
      if (handleQwertyZoomTouch(point)) {
        display.clear();
        qwertyZoomKeyboard.draw(display, typedText);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_MENU:
      handleMenuTouch(point);
      break;
    case SCREEN_TICTACTOE:
      if (ticTacToe.handleTouch(point)) {
        display.clear();
        ticTacToe.draw(display);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_MINESWEEPER:
      if (minesweeper.handleTouch(point)) {
        display.clear();
        minesweeper.draw(display);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_HANGMAN:
      if (hangman.handleTouch(point)) {
        display.clear();
        hangman.draw(display);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_SUDOKU:
      if (sudoku.handleTouch(point)) {
        display.clear();
        sudoku.draw(display);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_WORDLE:
      if (wordle.handleTouch(point)) {
        display.clear();
        wordle.draw(display);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    case SCREEN_CUBE:
      if (cubeApp.handleTouch(point)) {
        display.clear();
        cubeApp.draw(display);
        display.flushPartial(0, 0, 200, 200);
      }
      break;
    }
  }

  if (screen == SCREEN_T9_KEYBOARD && t9Keyboard.update()) {
    display.clear();
    t9Keyboard.draw(display, typedText);
    display.flushPartial(0, 0, 200, 200);
  }

  unsigned long currentSecond = millis() / 1000;
  if (screen == SCREEN_HOME && currentSecond != lastHomeSecond) {
    lastHomeSecond = currentSecond;
    dirty = true;
  }

  if (dirty) {
    render();
  }
  delay(1);
}
