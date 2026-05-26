#include "app_display.h"
#include "calculator_app.h"
#include "chess_app.h"
#include "contact_links_app.h"
#include "cube_app.h"
#include "hangman_app.h"
#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif
#if ENABLE_NETWORK_APPS
#include "hn_app.h"
#include "ai_app.h"
#endif
#include "keyboard_component.h"
#include "menu_button_consumer.h"
#include "minesweeper_app.h"
#include "paint_app.h"
#include "qwerty_zoom_keyboard_component.h"
#include "qr_app.h"
#if ENABLE_NETWORK_APPS
#include "reddit_app.h"
#endif
#include "smart_button.h"
#include "sudoku_app.h"
#include "tictactoe_app.h"
#include "t9_keyboard_component.h"
#include "touch_input.h"
#include "ui_helpers.h"
#include "device_clock.h"
#if ENABLE_NETWORK_APPS
#include "wifi_app.h"
#include "weather_app.h"
#endif
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
  SCREEN_PAINT,
  SCREEN_CONTACT_LINKS
#if ENABLE_NETWORK_APPS
  ,
  SCREEN_WIFI,
  SCREEN_REDDIT,
  SCREEN_HN,
  SCREEN_AI,
  SCREEN_WEATHER
#endif
};

enum MenuCategory {
  MENU_GAMES,
  MENU_APPS
#if ENABLE_NETWORK_APPS
  ,
  MENU_NETWORK
#endif
};

AppDisplay display;
TouchInput touch;
CubeApp cubeApp;
CalculatorApp calculator;
QrApp qrApp;
PaintApp paintApp;
ContactLinksApp contactLinks;
#if ENABLE_NETWORK_APPS
WifiApp wifiApp;
RedditApp redditApp;
HnApp hnApp;
AiApp aiApp;
WeatherApp weatherApp;
#endif
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
AppScreen<ContactLinksApp> contactLinksScreen(contactLinks);
#if ENABLE_NETWORK_APPS
AppScreen<WifiApp, true> wifiScreen(wifiApp);
AppScreen<RedditApp, true> redditScreen(redditApp);
AppScreen<HnApp, true> hnScreen(hnApp);
AppScreen<AiApp, true> aiScreen(aiApp);
AppScreen<WeatherApp, true> weatherScreen(weatherApp);
#endif

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

MenuCategory previousMenuCategory(MenuCategory category) {
  switch (category) {
  case MENU_GAMES:
#if ENABLE_NETWORK_APPS
    return MENU_NETWORK;
#else
    return MENU_APPS;
#endif
  case MENU_APPS:
    return MENU_GAMES;
#if ENABLE_NETWORK_APPS
  case MENU_NETWORK:
    return MENU_APPS;
#endif
  }
  return MENU_GAMES;
}

MenuCategory nextMenuCategory(MenuCategory category) {
  switch (category) {
  case MENU_GAMES:
    return MENU_APPS;
  case MENU_APPS:
#if ENABLE_NETWORK_APPS
    return MENU_NETWORK;
#else
    return MENU_GAMES;
#endif
#if ENABLE_NETWORK_APPS
  case MENU_NETWORK:
    return MENU_GAMES;
#endif
  }
  return MENU_GAMES;
}

const char *menuCategoryTitle(MenuCategory category) {
  switch (category) {
  case MENU_GAMES:
    return "GAMES";
  case MENU_APPS:
    return "APPS";
#if ENABLE_NETWORK_APPS
  case MENU_NETWORK:
    return "NETWORK";
#endif
  }
  return "GAMES";
}

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
#if ENABLE_NETWORK_APPS
  if (screen == SCREEN_REDDIT && redditApp.handleMenuButton()) {
    dirty = true;
    return;
  }
  if (screen == SCREEN_HN && hnApp.handleMenuButton()) {
    dirty = true;
    return;
  }
  if (screen == SCREEN_HN) {
    switchTo(SCREEN_MENU);
    return;
  }
#endif
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
  if (screen == SCREEN_HOME) {
    contactLinks.reset();
    switchTo(SCREEN_CONTACT_LINKS, &contactLinksScreen);
    return;
  }
  if (screen == SCREEN_CONTACT_LINKS) {
    switchTo(SCREEN_HOME);
    return;
  }
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
#if ENABLE_NETWORK_APPS
  if (screen == SCREEN_WIFI || screen == SCREEN_REDDIT || screen == SCREEN_HN ||
      screen == SCREEN_AI || screen == SCREEN_WEATHER) {
    if (screen == SCREEN_REDDIT && redditApp.handlePowerButton()) {
      dirty = true;
    } else if (screen == SCREEN_HN && hnApp.handlePowerButton()) {
      dirty = true;
    } else if (screen == SCREEN_AI && aiApp.handlePowerButton()) {
      dirty = true;
    }
    return;
  }
#endif
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
  if (screen == SCREEN_MENU) {
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

  unsigned long uptimeSeconds = millis() / 1000;

  char dateText[16];
  char timeText[16];
  if (deviceClock.isSet()) {
    deviceClock.formatDate(dateText, sizeof(dateText));
    deviceClock.formatTime(timeText, sizeof(timeText));
    display.setCursor(4, 8);
    display.print(dateText);
  } else {
    display.setCursor(4, 8);
    display.print("TIME --:--");
    formatSeconds(uptimeSeconds, timeText, sizeof(timeText));
  }

#if ENABLE_NETWORK_APPS
  if (wifiApp.displayState() != WIFI_DISPLAY_OFF) {
    wifiApp.drawStatusIcon(display, 150, 18);
  }
#endif

  int16_t timeX;
  int16_t timeY;
  uint16_t timeW;
  uint16_t timeH;
  display.getTextBounds(timeText, 0, 0, &timeX, &timeY, &timeW, &timeH);
  display.setCursor(EPD_WIDTH - timeW - 4 - timeX, 8);
  display.print(timeText);

  display.setCursor(58, 182);
  display.print("SUN: apps");
  display.setCursor(58, 192);
  display.print("PWR: contact");
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
  const char *title = menuCategoryTitle(menuCategory);
  display.getTextBounds(title, 0, 10, &titleX, &titleY, &titleW, &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2, 10);
  display.print(title);

  const char *gameIcons[9] = {"T", "M", "H", "S", "W", "C", "", "", ""};
  const char *gameLabels[9] = {"tictac", "mines", "hangman", "sudoku", "wordle",
                               "chess",  "",      "",        ""};
  const char *appIcons[9] = {"3", "=", "Q", "P", "", "", "", "", ""};
  const char *appLabels[9] = {"3dcube", "calc", "qr", "paint", "",
                              "",       "",     "",   ""};
  const char **icons = gameIcons;
  const char **labels = gameLabels;
  if (menuCategory == MENU_APPS) {
    icons = appIcons;
    labels = appLabels;
  }
#if ENABLE_NETWORK_APPS
  const char *networkIcons[9] = {"W", "R", "H", "A", "C", "", "", "", ""};
  const char *networkLabels[9] = {"wifi", "reddit", "hn", "ai", "weather",
                                  "",     "",       "",   ""};
  if (menuCategory == MENU_NETWORK) {
    icons = networkIcons;
    labels = networkLabels;
  }
#endif
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
    if (point.x < 48) {
      menuCategory = previousMenuCategory(menuCategory);
      dirty = true;
    } else if (point.x > 152) {
      menuCategory = nextMenuCategory(menuCategory);
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
#if ENABLE_NETWORK_APPS
  if (menuCategory == MENU_NETWORK) {
    switch (app) {
    case 0:
      switchTo(SCREEN_WIFI, &wifiScreen);
      break;
    case 1:
      redditApp.reset();
      switchTo(SCREEN_REDDIT, &redditScreen);
      break;
    case 2:
      hnApp.reset();
      switchTo(SCREEN_HN, &hnScreen);
      break;
    case 3:
      aiApp.reset();
      switchTo(SCREEN_AI, &aiScreen);
      break;
    case 4:
      weatherApp.reset();
      switchTo(SCREEN_WEATHER, &weatherScreen);
      break;
    default:
      break;
    }
    return;
  }
#endif
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
  contactLinks.reset();
#if ENABLE_NETWORK_APPS
  wifiApp.reset();
  redditApp.reset();
  hnApp.reset();
  aiApp.reset();
  weatherApp.reset();
#endif
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

  if (dirty) {
    render();
  }

  if (activeApp != nullptr && activeApp->update()) {
    dirty = true;
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
