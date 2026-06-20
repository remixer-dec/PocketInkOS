#include "sys/builtin_apps.h"
#include "apps/calculator_app.h"
#include "apps/contact_links_app.h"
#include "apps/deghost_app.h"
#include "apps/files_app.h"
#include "apps/gfx_app.h"
#include "apps/paint_app.h"
#include "apps/qr_app.h"
#include "games/chess_app.h"
#include "games/hangman_app.h"
#include "games/minesweeper_app.h"
#include "games/sudoku_app.h"
#include "games/tictactoe_app.h"
#include "games/wordle_app.h"
#include "sys/app_display.h"
#include "sys/pink_executable.h"
#include "sys/sd_storage.h"
#if ENABLE_NETWORK_APPS
#include "netapps/ai_app.h"
#include "netapps/hn_app.h"
#include "netapps/reddit_app.h"
#include "netapps/weather_app.h"
#include "netapps/wifi_app.h"
#endif
#include <cstring>
#include <stdint.h>
#if ENABLE_NETWORK_APPS
#include <WiFi.h>
#endif

extern AppDisplay display;

namespace {

CalculatorApp calculator;
QrApp qrApp;
PaintApp paintApp;
DeghostApp deghostApp;
GfxApp gfxApp;
FilesApp filesApp;
ContactLinksApp contactLinks;
#if ENABLE_NETWORK_APPS
WifiApp wifiApp;
RedditApp redditApp;
HnApp hnApp;
AiApp aiApp;
WeatherApp weatherApp;
#endif
TicTacToeApp ticTacToe;
MinesweeperApp minesweeper;
HangmanApp hangman;
SudokuApp sudoku;
WordleApp wordle;
ChessApp chess;

constexpr uint32_t CHESS_INACTIVITY_DEEP_SLEEP_MS = 5UL * 60UL * 1000UL;

AppScreen<TicTacToeApp> ticTacToeScreen(ticTacToe);
AppScreen<MinesweeperApp> minesweeperScreen(minesweeper);
AppScreen<HangmanApp, true> hangmanScreen(hangman);
AppScreen<SudokuApp> sudokuScreen(sudoku);
AppScreen<WordleApp, true> wordleScreen(wordle);
AppScreen<ChessApp, true> chessScreen(chess);
AppScreen<CalculatorApp, true> calculatorScreen(calculator);
AppScreen<QrApp, true> qrScreen(qrApp);
TouchlessAppScreen<PaintApp> paintScreen(paintApp);
AppScreen<DeghostApp, true> deghostScreen(deghostApp);
AppScreen<GfxApp, true> gfxScreen(gfxApp);
AppScreen<FilesApp, true> filesScreen(filesApp);
AppScreen<ContactLinksApp, true> contactLinksScreen(contactLinks);
#if ENABLE_NETWORK_APPS
AppScreen<WifiApp, true> wifiScreen(wifiApp);
AppScreen<RedditApp, true> redditScreen(redditApp);
AppScreen<HnApp, true> hnScreen(hnApp);
AppScreen<AiApp, true> aiScreen(aiApp);
AppScreen<WeatherApp, true> weatherScreen(weatherApp);
#endif

AppEventResult dirtyIfHandled(bool handled) {
  return handled ? AppEventResult::Dirty : AppEventResult::Unhandled;
}

AppEventResult dirtyIfUnhandled(bool handled) {
  return handled ? AppEventResult::Dirty : AppEventResult::Unhandled;
}

void startPaint() { paintApp.start(display); }
void stopPaint() { paintApp.stop(); }
void handlePaintRawTouch(const TouchEvent &event) {
  paintApp.handleTouchEvent(event);
}
void startDeghost() { deghostApp.start(display); }
void startGfx() { gfxApp.start(display); }

AppEventResult requestQuit() { return AppEventResult::QuitRequested; }
size_t saveTicTacToeContext(uint8_t *buffer, size_t capacity) {
  return ticTacToe.saveContext(buffer, capacity);
}
void restoreTicTacToeContext(const uint8_t *buffer, size_t length) {
  ticTacToe.restoreContext(buffer, length);
}
size_t saveMinesweeperContext(uint8_t *buffer, size_t capacity) {
  return minesweeper.saveContext(buffer, capacity);
}
void restoreMinesweeperContext(const uint8_t *buffer, size_t length) {
  minesweeper.restoreContext(buffer, length);
}
size_t saveHangmanContext(uint8_t *buffer, size_t capacity) {
  return hangman.saveContext(buffer, capacity);
}
void restoreHangmanContext(const uint8_t *buffer, size_t length) {
  hangman.restoreContext(buffer, length);
}
size_t saveSudokuContext(uint8_t *buffer, size_t capacity) {
  return sudoku.saveContext(buffer, capacity);
}
void restoreSudokuContext(const uint8_t *buffer, size_t length) {
  sudoku.restoreContext(buffer, length);
}
size_t saveWordleContext(uint8_t *buffer, size_t capacity) {
  return wordle.saveContext(buffer, capacity);
}
void restoreWordleContext(const uint8_t *buffer, size_t length) {
  wordle.restoreContext(buffer, length);
}
size_t saveChessContext(uint8_t *buffer, size_t capacity) {
  return chess.saveContext(buffer, capacity);
}
void restoreChessContext(const uint8_t *buffer, size_t length) {
  chess.restoreContext(buffer, length);
}
size_t saveQrContext(uint8_t *buffer, size_t capacity) {
  return qrApp.saveContext(buffer, capacity);
}
void restoreQrContext(const uint8_t *buffer, size_t length) {
  qrApp.restoreContext(buffer, length);
}
size_t saveFilesContext(uint8_t *buffer, size_t capacity) {
  return filesApp.saveContext(buffer, capacity);
}
void restoreFilesContext(const uint8_t *buffer, size_t length) {
  filesApp.restoreContext(buffer, length);
}

AppEventResult handleHangmanPower() {
  return dirtyIfHandled(hangman.openKeyboardFromButton());
}
AppEventResult handleWordlePower() {
  return dirtyIfHandled(wordle.openKeyboardFromButton());
}
AppEventResult handleMinesweeperPower() {
  return dirtyIfHandled(minesweeper.handlePowerButton());
}
AppEventResult handlePaintPower() {
  paintApp.clear();
  return AppEventResult::Handled;
}
AppEventResult handleDeghostPower() {
  return dirtyIfHandled(deghostApp.handlePowerButton());
}
AppEventResult handleGfxPower() {
  return dirtyIfHandled(gfxApp.handlePowerButton());
}
AppEventResult handleFilesMenu() {
  if (filesApp.handleMenuButton()) {
    return AppEventResult::Dirty;
  }
  return AppEventResult::Unhandled;
}
AppEventResult handleFilesPower() {
  return dirtyIfHandled(filesApp.handlePowerButton());
}
AppEventResult handleChessMenu() {
  if (chess.handleMenuButton()) {
    return AppEventResult::Dirty;
  }
  return AppEventResult::GoMenu;
}
AppEventResult handleChessMenuLong() {
  if (chess.cancelTargetSelection()) {
    return AppEventResult::Dirty;
  }
  if (chess.handleMenuLongPress()) {
    return AppEventResult::QuitRequested;
  }
  return AppEventResult::Unhandled;
}
AppEventResult handleChessMenuDouble() {
  if (chess.cancelTargetSelection()) {
    return AppEventResult::Dirty;
  }
  if (chess.handleMenuLongPress()) {
    return AppEventResult::QuitRequested;
  }
  return AppEventResult::Unhandled;
}
AppEventResult handleChessPower() {
  if (chess.isGameOver()) {
    return AppEventResult::GoMenu;
  }
  if (chess.isHistoryOpen() && chess.hasActiveSession()) {
    return AppEventResult::QuitRequested;
  }
  return dirtyIfHandled(chess.handlePowerButton());
}

#if ENABLE_NETWORK_APPS
AppEventResult handleRedditMenu() {
  return dirtyIfUnhandled(redditApp.handleMenuButton());
}
AppEventResult handleRedditPower() {
  return dirtyIfHandled(redditApp.handlePowerButton());
}
AppEventResult handleHnMenu() {
  if (hnApp.handleMenuButton()) {
    return AppEventResult::Dirty;
  }
  return AppEventResult::GoMenu;
}
AppEventResult handleHnPower() {
  return dirtyIfHandled(hnApp.handlePowerButton());
}
AppEventResult handleAiPower() {
  return dirtyIfHandled(aiApp.handlePowerButton());
}
void stopAiAudio() { aiApp.stopAudio(); }

char wifiIconCharForState(WifiDisplayState state) {
  if (state == WIFI_DISPLAY_CONNECTED) {
    return 'g';
  }
  if (state == WIFI_DISPLAY_ON) {
    return 'f';
  }
  return '\0';
}
#endif

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, Screen screen,
                         ActiveApp *runtime) {
  return {id, label, icon, category, screen, runtime};
}

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, Screen screen,
                         ActiveApp *runtime, AppCallback reset) {
  AppDefinition app = builtInApp(id, label, icon, category, screen, runtime);
  app.reset = reset;
  return app;
}

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, Screen screen,
                         ActiveApp *runtime, AppCallback reset,
                         AppBehavior behavior) {
  AppDefinition app = builtInApp(id, label, icon, category, screen, runtime,
                                reset);
  app.behavior = behavior;
  return app;
}

AppDefinition builtInAppWhen(const char *id, const char *label,
                             const char *icon, MenuCategory category,
                             Screen screen, ActiveApp *runtime,
                             AppVisibleHandler visible, AppCallback reset) {
  AppDefinition app = builtInApp(id, label, icon, category, screen, runtime);
  app.visible = visible;
  app.reset = reset;
  return app;
}

AppDefinition builtInIconAppWhen(const char *id, const char *label,
                                 const char *icon, MenuCategory category,
                                 Screen screen, ActiveApp *runtime,
                                 AppVisibleHandler visible,
                                 AppCallback reset,
                                 AppBehavior behavior = AppBehavior{}) {
  AppDefinition app =
      builtInAppWhen(id, label, icon, category, screen, runtime, visible, reset);
  app.iconFont = true;
  app.behavior = behavior;
  return app;
}

AppDefinition withInactivityDeepSleep(AppDefinition app, uint32_t timeoutMs) {
  app.inactivityDeepSleepMs = timeoutMs;
  return app;
}

AppBehavior powerBehavior(AppEventHandler handler) {
  AppBehavior behavior;
  behavior.onPower = handler;
  return behavior;
}

AppBehavior contextBehavior(AppSaveContextHandler save,
                            AppRestoreContextHandler restore) {
  AppBehavior behavior;
  behavior.onSaveContext = save;
  behavior.onRestoreContext = restore;
  return behavior;
}

AppBehavior powerContextBehavior(AppEventHandler handler,
                                 AppSaveContextHandler save,
                                 AppRestoreContextHandler restore) {
  AppBehavior behavior = powerBehavior(handler);
  behavior.onSaveContext = save;
  behavior.onRestoreContext = restore;
  return behavior;
}

AppBehavior chessBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handleChessMenu;
  behavior.onMenuDouble = handleChessMenuDouble;
  behavior.onMenuLong = handleChessMenuLong;
  behavior.onPower = handleChessPower;
  behavior.onSaveContext = saveChessContext;
  behavior.onRestoreContext = restoreChessContext;
  return behavior;
}

AppBehavior gfxBehavior() {
  AppBehavior behavior;
  behavior.onEnter = startGfx;
  behavior.onPower = handleGfxPower;
  return behavior;
}

AppBehavior filesBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handleFilesMenu;
  behavior.onPower = handleFilesPower;
  behavior.onSaveContext = saveFilesContext;
  behavior.onRestoreContext = restoreFilesContext;
  return behavior;
}

AppBehavior paintBehavior() {
  AppBehavior behavior;
  behavior.onEnter = startPaint;
  behavior.onExit = stopPaint;
  behavior.onPower = handlePaintPower;
  behavior.onRawTouch = handlePaintRawTouch;
  return behavior;
}

AppBehavior deghostBehavior() {
  AppBehavior behavior;
  behavior.onEnter = startDeghost;
  behavior.onMenu = requestQuit;
  behavior.onMenuLong = requestQuit;
  behavior.onPower = handleDeghostPower;
  return behavior;
}

#if ENABLE_NETWORK_APPS
AppBehavior redditBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handleRedditMenu;
  behavior.onPower = handleRedditPower;
  return behavior;
}

AppBehavior hnBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handleHnMenu;
  behavior.onPower = handleHnPower;
  return behavior;
}

AppBehavior aiBehavior() {
  AppBehavior behavior;
  behavior.onExit = stopAiAudio;
  behavior.onPower = handleAiPower;
  return behavior;
}
#endif

const AppDefinition contactLinksApp =
    builtInApp("contact_links", "contact", "C", MENU_APPS,
               SCREEN_CONTACT_LINKS, &contactLinksScreen,
               []() { contactLinks.reset(); });

} // namespace

const AppDefinition builtInApps[] = {
    builtInApp("tictactoe", "tictac", "T", MENU_GAMES, SCREEN_TICTACTOE,
               &ticTacToeScreen, []() { ticTacToe.reset(); },
               contextBehavior(saveTicTacToeContext, restoreTicTacToeContext)),
    builtInApp("minesweeper", "mines", "M", MENU_GAMES, SCREEN_MINESWEEPER,
               &minesweeperScreen, []() { minesweeper.reset(); },
               powerContextBehavior(handleMinesweeperPower,
                                    saveMinesweeperContext,
                                    restoreMinesweeperContext)),
    builtInApp("hangman", "hangman", "H", MENU_GAMES, SCREEN_HANGMAN,
               &hangmanScreen, []() { hangman.reset(); },
               powerContextBehavior(handleHangmanPower, saveHangmanContext,
                                    restoreHangmanContext)),
    builtInApp("sudoku", "sudoku", "S", MENU_GAMES, SCREEN_SUDOKU,
               &sudokuScreen, []() { sudoku.reset(); },
               contextBehavior(saveSudokuContext, restoreSudokuContext)),
    builtInApp("wordle", "wordle", "W", MENU_GAMES, SCREEN_WORDLE,
               &wordleScreen, []() { wordle.reset(); },
               powerContextBehavior(handleWordlePower, saveWordleContext,
                                    restoreWordleContext)),
    withInactivityDeepSleep(
        builtInApp("chess", "chess", "C", MENU_GAMES, SCREEN_CHESS,
                   &chessScreen, []() { chess.reset(); }, chessBehavior()),
        CHESS_INACTIVITY_DEEP_SLEEP_MS),
    builtInApp("gfx", "gfx", "G", MENU_APPS, SCREEN_GFX, &gfxScreen,
               []() { gfxApp.reset(); }, gfxBehavior()),
    builtInApp("calculator", "calc", "=", MENU_APPS, SCREEN_CALCULATOR,
               &calculatorScreen, []() { calculator.reset(); }),
    builtInApp("qr", "qr", "Q", MENU_APPS, SCREEN_QR, &qrScreen,
               []() { qrApp.reset(); },
               contextBehavior(saveQrContext, restoreQrContext)),
    builtInApp("paint", "paint", "P", MENU_APPS, SCREEN_PAINT, &paintScreen,
               []() { paintApp.reset(); }, paintBehavior()),
    builtInApp("deghost", "deghost", "D", MENU_APPS, SCREEN_DEGHOST,
               &deghostScreen, []() { deghostApp.reset(); },
               deghostBehavior()),
    builtInIconAppWhen("files", "files", "N", MENU_APPS, SCREEN_FILES,
                       &filesScreen, sdStorageMounted,
                       []() { filesApp.reset(); },
                       filesBehavior())
#if ENABLE_NETWORK_APPS
    ,
    builtInApp("wifi", "wifi", "W", MENU_NETWORK, SCREEN_WIFI, &wifiScreen),
    builtInApp("reddit", "reddit", "R", MENU_NETWORK, SCREEN_REDDIT,
               &redditScreen, []() { redditApp.reset(); }, redditBehavior()),
    builtInApp("hn", "hn", "H", MENU_NETWORK, SCREEN_HN, &hnScreen,
               []() { hnApp.reset(); }, hnBehavior()),
    builtInApp("ai", "ai", "A", MENU_NETWORK, SCREEN_AI, &aiScreen,
               []() { aiApp.reset(); }, aiBehavior()),
    builtInApp("weather", "weather", "C", MENU_NETWORK, SCREEN_WEATHER,
               &weatherScreen, []() { weatherApp.reset(); })
#endif
};

const size_t builtInAppCount = sizeof(builtInApps) / sizeof(builtInApps[0]);

ActiveApp *contactLinksRuntime() { return &contactLinksScreen; }

const AppDefinition *contactLinksDefinition() { return &contactLinksApp; }

bool appVisible(const AppDefinition &app) {
  return app.visible == nullptr || app.visible();
}

void copyCatalogEntry(AppCatalogEntry &out, const AppDefinition &app) {
  out.definition = app;
  out.id[0] = '\0';
  out.label[0] = '\0';
  out.icon[0] = '\0';
}

size_t appCatalogCount(MenuCategory category) {
  size_t count = 0;
  for (size_t i = 0; i < builtInAppCount; i++) {
    if (builtInApps[i].category == category && appVisible(builtInApps[i])) {
      count++;
    }
  }
  return count + pinkExecutableCount(category);
}

bool appCatalogAtVisibleIndex(MenuCategory category, size_t index,
                              AppCatalogEntry &out) {
  size_t visibleIndex = 0;
  for (size_t i = 0; i < builtInAppCount; i++) {
    if (builtInApps[i].category != category || !appVisible(builtInApps[i])) {
      continue;
    }
    if (visibleIndex == index) {
      copyCatalogEntry(out, builtInApps[i]);
      return true;
    }
    visibleIndex++;
  }

  return pinkExecutableDefinitionAt(category, index - visibleIndex,
                                    &out.definition, out.id, sizeof(out.id),
                                    out.label, sizeof(out.label), out.icon,
                                    sizeof(out.icon));
}

bool findAppById(const char *id, AppCatalogEntry &out) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < builtInAppCount; i++) {
    if (appVisible(builtInApps[i]) && strcmp(builtInApps[i].id, id) == 0) {
      copyCatalogEntry(out, builtInApps[i]);
      return true;
    }
  }
  if (strcmp(contactLinksApp.id, id) == 0) {
    copyCatalogEntry(out, contactLinksApp);
    return true;
  }

  const MenuCategory categories[] = {
      MENU_GAMES,
      MENU_APPS
#if ENABLE_NETWORK_APPS
      ,
      MENU_NETWORK
#endif
  };
  for (MenuCategory category : categories) {
    const size_t count = pinkExecutableCount(category);
    for (size_t i = 0; i < count; i++) {
      if (pinkExecutableDefinitionAt(category, i, &out.definition, out.id,
                                     sizeof(out.id), out.label,
                                     sizeof(out.label), out.icon,
                                     sizeof(out.icon)) &&
          strcmp(out.definition.id, id) == 0) {
        return true;
      }
    }
  }
  return false;
}

void refreshAppCatalog() {}

bool consumeFilesPinkLaunch(char *path, size_t pathSize) {
  return filesApp.consumePinkLaunch(path, pathSize);
}

void resetContactLinks() { contactLinks.reset(); }

void resetApps() {
  for (size_t i = 0; i < builtInAppCount; i++) {
    if (builtInApps[i].reset != nullptr) {
      builtInApps[i].reset();
    }
  }
  resetContactLinks();
#if ENABLE_NETWORK_APPS
  wifiApp.reset();
#endif
}

char wifiStatusIcon() {
#if ENABLE_NETWORK_APPS
  return wifiIconCharForState(wifiApp.displayState());
#else
  return '\0';
#endif
}

bool wifiIsOn() {
#if ENABLE_NETWORK_APPS
  return wifiApp.displayState() != WIFI_DISPLAY_OFF;
#else
  return false;
#endif
}

bool wifiIsConnected() {
#if ENABLE_NETWORK_APPS
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

void wifiTurnOn() {
#if ENABLE_NETWORK_APPS
  if (wifiApp.displayState() == WIFI_DISPLAY_OFF) {
    wifiApp.connect();
  }
#endif
}

void wifiTurnOff() {
#if ENABLE_NETWORK_APPS
  wifiApp.disconnect();
#endif
}

void wifiToggle() {
#if ENABLE_NETWORK_APPS
  if (wifiApp.displayState() == WIFI_DISPLAY_OFF) {
    wifiApp.connect();
  } else {
    wifiApp.disconnect();
  }
#endif
}

bool wifiUpdate() {
#if ENABLE_NETWORK_APPS
  return wifiApp.update();
#else
  return false;
#endif
}

void restoreWifiOn(bool enabled) {
#if ENABLE_NETWORK_APPS
  if (enabled) {
    wifiTurnOn();
  }
#else
  (void)enabled;
#endif
}
