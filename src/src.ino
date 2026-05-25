#include "app_display.h"
#include "calculator_app.h"
#include "chess_app.h"
#include "cube_app.h"
#include "hangman_app.h"
#include "keyboard_component.h"
#include "menu_button_consumer.h"
#include "minesweeper_app.h"
#include "paint_app.h"
#include "qwerty_zoom_keyboard_component.h"
#include "qr_app.h"
#include "smart_button.h"
#include "sudoku_app.h"
#include "tictactoe_app.h"
#include "t9_keyboard_component.h"
#include "touch_input.h"
#include "ui_helpers.h"
#include "wordle_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
  SCREEN_CHESS,
  SCREEN_CUBE,
  SCREEN_CALCULATOR,
  SCREEN_QR,
  SCREEN_PAINT
};

enum MenuCategory { MENU_GAMES, MENU_APPS };

AppDisplay display;
TouchInput touch;
CubeApp cubeApp;
CalculatorApp calculator;
QrApp qrApp;
PaintApp paintApp;
KeyboardComponent keyboard;
QwertyZoomKeyboardComponent qwertyZoomKeyboard;
T9KeyboardComponent t9Keyboard;
TicTacToeApp ticTacToe;
MinesweeperApp minesweeper;
HangmanApp hangman;
SudokuApp sudoku;
WordleApp wordle;
ChessApp chess;
SmartButton mainButton(BOOT_BUTTON_PIN);
SmartButton backButton(PWR_BUTTON_PIN);

class ActiveApp {
public:
  virtual void draw(Adafruit_GFX &gfx) = 0;
  virtual bool handleTouch(const TouchPoint &point) = 0;
  virtual bool update() { return false; }
  virtual bool hasActiveSession() const = 0;
};

template <typename T, bool SupportsUpdate = false>
class AppScreen : public ActiveApp {
public:
  explicit AppScreen(T &target) : app(target) {}

  void draw(Adafruit_GFX &gfx) override { app.draw(gfx); }
  bool handleTouch(const TouchPoint &point) override {
    return app.handleTouch(point);
  }
  bool update() override {
    if constexpr (SupportsUpdate) {
      return app.update();
    }
    return false;
  }
  bool hasActiveSession() const override { return app.hasActiveSession(); }

private:
  T &app;
};

class PaintScreen : public ActiveApp {
public:
  explicit PaintScreen(PaintApp &target) : app(target) {}

  void draw(Adafruit_GFX &gfx) override { app.draw(gfx); }
  bool handleTouch(const TouchPoint &) override { return false; }
  bool hasActiveSession() const override { return app.hasActiveSession(); }

private:
  PaintApp &app;
};

AppScreen<TicTacToeApp> ticTacToeScreen(ticTacToe);
AppScreen<MinesweeperApp> minesweeperScreen(minesweeper);
AppScreen<HangmanApp, true> hangmanScreen(hangman);
AppScreen<SudokuApp> sudokuScreen(sudoku);
AppScreen<WordleApp, true> wordleScreen(wordle);
AppScreen<ChessApp, true> chessScreen(chess);
AppScreen<CubeApp> cubeScreen(cubeApp);
AppScreen<CalculatorApp> calculatorScreen(calculator);
AppScreen<QrApp, true> qrScreen(qrApp);
PaintScreen paintScreen(paintApp);

Screen screen = SCREEN_HOME;
ActiveApp *activeApp = nullptr;
String typedText;
bool dirty = true;
bool confirmQuit = false;
MenuCategory menuCategory = MENU_GAMES;
unsigned long quitDialogOpenedAt = 0;
unsigned long lastHomeSecond = 0;
TaskHandle_t buttonTaskHandle = NULL;
volatile uint8_t pendingMenuClicks = 0;
volatile uint8_t pendingMenuDoubleClicks = 0;
volatile uint8_t pendingMenuLongPresses = 0;
volatile uint8_t pendingPowerClicks = 0;
volatile uint8_t pendingPowerLongPresses = 0;

void queueButtonEvent(volatile uint8_t &counter) {
  if (counter < 255) {
    counter++;
  }
}

bool consumeButtonEvent(volatile uint8_t &counter) {
  if (counter == 0) {
    return false;
  }
  counter--;
  return true;
}

void switchTo(Screen next, ActiveApp *nextApp = nullptr) {
  if (screen == SCREEN_PAINT && next != SCREEN_PAINT) {
    paintApp.stop();
  }
  screen = next;
  activeApp = nextApp;
  if (screen == SCREEN_PAINT) {
    paintApp.start(display);
  }
  confirmQuit = false;
  dirty = true;
}

void switchKeyboardMode();

bool toggleActiveKeyboardCaps() {
  switch (screen) {
  case SCREEN_KEYBOARD:
    keyboard.toggleCaps();
    return true;
  case SCREEN_T9_KEYBOARD:
    t9Keyboard.toggleCaps();
    return true;
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    qwertyZoomKeyboard.toggleCaps();
    return true;
  default:
    return false;
  }
}

bool isKeyboardScreen() {
  return screen == SCREEN_KEYBOARD || screen == SCREEN_T9_KEYBOARD ||
         screen == SCREEN_QWERTY_ZOOM_KEYBOARD;
}

bool isSessionScreen() {
  return activeApp != nullptr && activeApp->hasActiveSession();
}

void openQuitDialog() {
  if (screen == SCREEN_PAINT) {
    paintApp.stop();
  }
  confirmQuit = true;
  quitDialogOpenedAt = millis();
  dirty = true;
}

void handleMenuButton() {
  if (confirmQuit) {
    return;
  }
  if (handleActiveMenuButton()) {
    dirty = true;
    return;
  }
  if (toggleActiveKeyboardCaps()) {
    dirty = true;
    return;
  }
  if (screen != SCREEN_CHESS && isSessionScreen()) {
    openQuitDialog();
    return;
  }
  if (screen == SCREEN_CHESS && chess.handleMenuButton()) {
    dirty = true;
    return;
  }
  switchTo(screen == SCREEN_MENU ? SCREEN_HOME : SCREEN_MENU);
}

void handleMenuDoubleButton() {
  if (confirmQuit) {
    return;
  }
  if (handleActiveMenuDoubleButton()) {
    dirty = true;
    return;
  }
  if (isKeyboardScreen()) {
    switchKeyboardMode();
  }
}

void handleMenuLongButton() {
  if (confirmQuit) {
    return;
  }
  if (handleActiveMenuLongButton()) {
    dirty = true;
    return;
  }
  if (isKeyboardScreen()) {
    return;
  }
  if (screen == SCREEN_CHESS && chess.handleMenuLongPress()) {
    openQuitDialog();
    return;
  }
  if (isSessionScreen()) {
    openQuitDialog();
    return;
  }
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
  if (screen == SCREEN_MINESWEEPER) {
    if (minesweeper.handlePowerButton()) {
      dirty = true;
    }
    return;
  }
  if (screen == SCREEN_PAINT) {
    paintApp.clear();
    return;
  }
  if (screen == SCREEN_TICTACTOE || screen == SCREEN_MINESWEEPER ||
      screen == SCREEN_SUDOKU || screen == SCREEN_CUBE ||
      screen == SCREEN_CALCULATOR || screen == SCREEN_QR) {
    return;
  }
  if (screen == SCREEN_CHESS) {
    if (chess.isGameOver()) {
      switchTo(SCREEN_MENU);
      return;
    }
    if (chess.isHistoryOpen() && chess.hasActiveSession()) {
      openQuitDialog();
      return;
    }
    if (chess.handlePowerButton()) {
      dirty = true;
    }
    return;
  }
  if (screen == SCREEN_HOME || screen == SCREEN_MENU) {
    return;
  }
  switchKeyboardMode();
}

void buttonTask(void *) {
  while (true) {
    mainButton.update();
    backButton.update();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void consumeQueuedButtons() {
  while (consumeButtonEvent(pendingMenuLongPresses)) {
    handleMenuLongButton();
  }
  while (consumeButtonEvent(pendingMenuDoubleClicks)) {
    handleMenuDoubleButton();
  }
  while (consumeButtonEvent(pendingMenuClicks)) {
    handleMenuButton();
  }
  while (consumeButtonEvent(pendingPowerLongPresses)) {
    handleOtherButton();
  }
  while (consumeButtonEvent(pendingPowerClicks)) {
    handleOtherButton();
  }
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
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  display.getTextBounds("Pocket Ink", 0, 0, &titleX, &titleY, &titleW,
                        &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2 - titleX, 72);
  display.print("Pocket Ink");
  display.setTextSize(1);
  int16_t osX;
  int16_t osY;
  uint16_t osW;
  uint16_t osH;
  display.getTextBounds("os", 0, 0, &osX, &osY, &osW, &osH);
  display.setCursor((EPD_WIDTH - titleW) / 2 - titleX + titleW - osW - osX,
                    94);
  display.print("os");

  char text[16];
  unsigned long uptimeSeconds = millis() / 1000;

  display.setCursor(4, 8);
  display.print("TIME --:--");

  formatSeconds(uptimeSeconds, text, sizeof(text));
  int16_t timeX;
  int16_t timeY;
  uint16_t timeW;
  uint16_t timeH;
  display.getTextBounds(text, 0, 0, &timeX, &timeY, &timeW, &timeH);
  display.setCursor(EPD_WIDTH - timeW - 4 - timeX, 8);
  display.print(text);

  display.setCursor(58, 182);
  display.print("BOOT: apps");
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
  if (millis() - quitDialogOpenedAt < 350) {
    return true;
  }
  if (uiContains(QUIT_YES_BUTTON, point)) {
    switchTo(SCREEN_MENU);
    return true;
  }
  if (uiContains(QUIT_NO_BUTTON, point)) {
    confirmQuit = false;
    if (screen == SCREEN_PAINT) {
      paintApp.start(display);
    }
    dirty = true;
    return true;
  }
  return true;
}

void drawMenu() {
  display.setTextColor(1);
  display.setTextSize(2);
  display.setCursor(8, 10);
  display.print("<");
  display.setCursor(184, 10);
  display.print(">");
  display.setTextSize(2);
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  const char *title = menuCategory == MENU_GAMES ? "GAMES" : "APPS";
  display.getTextBounds(title, 0, 10, &titleX, &titleY, &titleW, &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2, 10);
  display.print(title);

  const char *gameIcons[9] = {"T", "M", "H", "S", "W", "C", "", "", ""};
  const char *gameLabels[9] = {"tictac", "mines", "hangman", "sudoku", "wordle",
                               "chess",  "",      "",        ""};
  const char *appIcons[9] = {"3", "=", "Q", "P", "", "", "", "", ""};
  const char *appLabels[9] = {"3dcube", "calc", "qr", "paint", "",
                              "",       "",     "",   ""};
  const char **icons = menuCategory == MENU_GAMES ? gameIcons : appIcons;
  const char **labels = menuCategory == MENU_GAMES ? gameLabels : appLabels;
  int n = 0;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int x = 20 + col * 60;
      int y = 38 + row * 52;
      int labelIndex = row * 3 + col;
      if (labels[labelIndex][0] == '\0') {
        n++;
        continue;
      }
      display.drawRect(x, y, 40, 32, 1);
      display.setTextSize(2);
      display.setCursor(x + 14, y + 8);
      display.print(icons[n++]);
      display.setTextSize(1);
      int16_t labelX;
      int16_t labelY;
      uint16_t labelW;
      uint16_t labelH;
      display.getTextBounds(labels[labelIndex], 0, 0, &labelX, &labelY,
                            &labelW, &labelH);
      display.setCursor(x + (40 - static_cast<int>(labelW)) / 2 - labelX,
                        y + 35);
      display.print(labels[labelIndex]);
    }
  }

  display.setTextSize(1);
}

void handleMenuTouch(const TouchPoint &point) {
  if (point.y < 34) {
    if (point.x < 48 || point.x > 152) {
      menuCategory = menuCategory == MENU_GAMES ? MENU_APPS : MENU_GAMES;
      dirty = true;
    }
    return;
  }
  if (point.x < 20 || point.x >= 200 || point.y < 38 || point.y >= 194) {
    return;
  }
  int col = (point.x - 20) / 60;
  int row = (point.y - 38) / 52;
  if (col < 0 || col > 2 || row < 0 || row > 2) return;
  int app = row * 3 + col;
  if (menuCategory == MENU_APPS) {
    switch (app) {
    case 0:
      cubeApp.reset();
      switchTo(SCREEN_CUBE, &cubeScreen);
      break;
    case 1:
      calculator.reset();
      switchTo(SCREEN_CALCULATOR, &calculatorScreen);
      break;
    case 2:
      qrApp.reset();
      switchTo(SCREEN_QR, &qrScreen);
      break;
    case 3:
      paintApp.reset();
      switchTo(SCREEN_PAINT, &paintScreen);
      break;
    default:
      break;
    }
    return;
  }
  switch (app) {
  case 0:
    ticTacToe.reset();
    switchTo(SCREEN_TICTACTOE, &ticTacToeScreen);
    break;
  case 1:
    minesweeper.reset();
    switchTo(SCREEN_MINESWEEPER, &minesweeperScreen);
    break;
  case 2:
    hangman.reset();
    switchTo(SCREEN_HANGMAN, &hangmanScreen);
    break;
  case 3:
    sudoku.reset();
    switchTo(SCREEN_SUDOKU, &sudokuScreen);
    break;
  case 4:
    wordle.reset();
    switchTo(SCREEN_WORDLE, &wordleScreen);
    break;
  case 5:
    chess.reset();
    switchTo(SCREEN_CHESS, &chessScreen);
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

void drawActiveScreen() {
  if (activeApp != nullptr) {
    activeApp->draw(display);
    return;
  }

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
  default:
    break;
  }
}

void redrawActiveScreenPartial() {
  display.lock();
  display.clear();
  drawActiveScreen();
  display.flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
  display.unlock();
}

bool handleScreenTouch(const TouchPoint &point) {
  if (activeApp != nullptr) {
    return activeApp->handleTouch(point);
  }

  switch (screen) {
  case SCREEN_KEYBOARD:
    return handleKeyboardTouch(point);
  case SCREEN_T9_KEYBOARD:
    return t9Keyboard.hitTest(point, typedText).action != KEY_NONE;
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    return handleQwertyZoomTouch(point);
  case SCREEN_MENU:
    handleMenuTouch(point);
    return false;
  default:
    return false;
  }
}

void render() {
  display.lock();
  display.clear();
  drawActiveScreen();
  if (confirmQuit) {
    drawQuitDialog();
  }
  display.flush();
  display.unlock();
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

  mainButton.attachSingleClick([]() { queueButtonEvent(pendingMenuClicks); });
  mainButton.attachDoubleClick(
      []() { queueButtonEvent(pendingMenuDoubleClicks); });
  mainButton.attachLongPressStart(
      []() { queueButtonEvent(pendingMenuLongPresses); });
  backButton.attachSingleClick([]() { queueButtonEvent(pendingPowerClicks); });
  backButton.attachLongPressStart(
      []() { queueButtonEvent(pendingPowerLongPresses); });

  if (buttonTaskHandle == NULL) {
    xTaskCreatePinnedToCore(buttonTask, "buttons", 2048, NULL, 2,
                            &buttonTaskHandle, 0);
  }

  ticTacToe.reset();
  minesweeper.reset();
  hangman.reset();
  sudoku.reset();
  wordle.reset();
  chess.reset();
  calculator.reset();
  qrApp.reset();
  paintApp.reset();
  dirty = true;
}

void loop() {
  consumeQueuedButtons();

  if (screen == SCREEN_PAINT && !confirmQuit) {
    TouchEvent event;
    while (touch.readEvent(event)) {
      paintApp.handleTouchEvent(event);
    }
  } else {
    TouchPoint point;
    if (touch.read(point)) {
      if (confirmQuit) {
        handleQuitDialogTouch(point);
        delay(1);
        return;
      }
      if (handleScreenTouch(point)) {
        redrawActiveScreenPartial();
      }
    }
  }

  if (activeApp != nullptr && activeApp->update()) {
    redrawActiveScreenPartial();
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
