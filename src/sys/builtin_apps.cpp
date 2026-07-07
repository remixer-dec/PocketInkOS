#include "sys/builtin_apps.h"
#include "sys/system_config.h"
#include "apps/calendar_app.h"
#include "apps/calculator_app.h"
#include "apps/contact_links_app.h"
#include "apps/settings_app.h"
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
#include "sys/wifi_manager.h"
#if ENABLE_TTS
#include "apps/tts_app.h"
#endif
#if ENABLE_NETWORK_APPS
#include "netapps/ai_app.h"
#include "netapps/hn_app.h"
#include "netapps/reddit_app.h"
#include "netapps/weather_app.h"
#include "netapps/wifi_app.h"
#endif
#include <cstring>
#include <stdint.h>

extern AppDisplay display;

namespace {

CalculatorApp calculator;
CalendarApp calendar;
QrApp qrApp;
PaintApp paintApp;
DeghostApp deghostApp;
GfxApp gfxApp;
FilesApp filesApp;
ContactLinksApp contactLinks;
SettingsApp settingsApp;
#if ENABLE_TTS
TtsApp ttsApp;
#endif
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

AppScreen<TicTacToeApp, true> ticTacToeScreen(ticTacToe);
AppScreen<MinesweeperApp> minesweeperScreen(minesweeper);
AppScreen<HangmanApp, true> hangmanScreen(hangman);
AppScreen<SudokuApp, true> sudokuScreen(sudoku);
AppScreen<WordleApp, true> wordleScreen(wordle);
AppScreen<ChessApp, true> chessScreen(chess);
AppScreen<CalculatorApp, true> calculatorScreen(calculator);
AppScreen<CalendarApp, true> calendarScreen(calendar);
AppScreen<QrApp, true> qrScreen(qrApp);
TouchlessAppScreen<PaintApp> paintScreen(paintApp);
AppScreen<DeghostApp, true> deghostScreen(deghostApp);
AppScreen<GfxApp, true> gfxScreen(gfxApp);
AppScreen<FilesApp, true> filesScreen(filesApp);
AppScreen<ContactLinksApp, true> contactLinksScreen(contactLinks);
AppScreen<SettingsApp> settingsScreen(settingsApp);
#if ENABLE_TTS
AppScreen<TtsApp, true> ttsScreen(ttsApp);
#endif
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
void stopGfx() { gfxApp.release(); }

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
AppEventResult handleDeghostMenu() {
  return deghostApp.handleBackButton();
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
void releaseReddit() { redditApp.release(); }
AppEventResult handleHnMenu() {
  if (hnApp.handleMenuButton()) {
    return AppEventResult::Dirty;
  }
  return AppEventResult::GoMenu;
}
AppEventResult handleHnPower() {
  return dirtyIfHandled(hnApp.handlePowerButton());
}
void releaseHn() { hnApp.release(); }
AppEventResult handleAiPower() {
  return dirtyIfHandled(aiApp.handlePowerButton());
}
void stopAiAudio() { aiApp.stopAudio(); }
void releaseAi() { aiApp.release(); }
void releaseWeather() { weatherApp.release(); }

#endif

#if ENABLE_TTS
void stopTtsAudio() { ttsApp.stopAudio(); }
AppEventResult handleTtsMenu() { return dirtyIfHandled(ttsApp.handleMenuButton()); }
AppEventResult handleTtsMenuDouble() {
  return dirtyIfHandled(ttsApp.handleMenuDoubleButton());
}
AppEventResult handleTtsMenuLong() {
  return dirtyIfHandled(ttsApp.handleMenuLongButton());
}
AppEventResult handleTtsPower() { return dirtyIfHandled(ttsApp.handlePowerButton()); }
#endif

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, ActiveApp *runtime) {
  return {id, label, icon, category, runtime};
}

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, ActiveApp *runtime,
                         AppCallback reset) {
  AppDefinition app = builtInApp(id, label, icon, category, runtime);
  app.reset = reset;
  return app;
}

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, ActiveApp *runtime,
                         AppCallback reset, AppBehavior behavior) {
  AppDefinition app = builtInApp(id, label, icon, category, runtime, reset);
  app.behavior = behavior;
  return app;
}

AppDefinition builtInAppWhen(const char *id, const char *label,
                             const char *icon, MenuCategory category,
                             ActiveApp *runtime, AppVisibleHandler visible,
                             AppCallback reset) {
  AppDefinition app = builtInApp(id, label, icon, category, runtime);
  app.visible = visible;
  app.reset = reset;
  return app;
}

AppDefinition builtInAppWhen(const char *id, const char *label,
                             const char *icon, MenuCategory category,
                             ActiveApp *runtime, AppVisibleHandler visible,
                             AppCallback reset, AppBehavior behavior) {
  AppDefinition app =
      builtInAppWhen(id, label, icon, category, runtime, visible, reset);
  app.behavior = behavior;
  return app;
}

AppDefinition builtInIconAppWhen(const char *id, const char *label,
                                 const char *icon, MenuCategory category,
                                 ActiveApp *runtime, AppVisibleHandler visible,
                                 AppCallback reset,
                                 AppBehavior behavior = AppBehavior{}) {
  AppDefinition app =
      builtInAppWhen(id, label, icon, category, runtime, visible, reset);
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
  behavior.onRelease = stopGfx;
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
  behavior.onMenu = handleDeghostMenu;
  behavior.onMenuLong = handleDeghostMenu;
  behavior.onPower = handleDeghostPower;
  return behavior;
}

AppBehavior settingsBehavior() {
  AppBehavior behavior;
  behavior.onMenu = requestQuit;
  return behavior;
}

AppBehavior contactLinksBehavior() {
  AppBehavior behavior;
  behavior.onPower = []() { return AppEventResult::GoHome; };
  return behavior;
}

const AppDefinition contactLinksApp =
    builtInApp("contact_links", "contact", "C", MENU_APPS, &contactLinksScreen,
               []() { contactLinks.reset(); }, contactLinksBehavior());

#if ENABLE_NETWORK_APPS
AppBehavior redditBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handleRedditMenu;
  behavior.onPower = handleRedditPower;
  behavior.onRelease = releaseReddit;
  return behavior;
}

AppBehavior hnBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handleHnMenu;
  behavior.onPower = handleHnPower;
  behavior.onRelease = releaseHn;
  return behavior;
}

AppBehavior aiBehavior() {
  AppBehavior behavior;
  behavior.onExit = stopAiAudio;
  behavior.onPower = handleAiPower;
  behavior.onRelease = releaseAi;
  return behavior;
}

AppBehavior weatherBehavior() {
  AppBehavior behavior;
  behavior.onRelease = releaseWeather;
  return behavior;
}

AppEventResult handleWifiMenu() {
  return dirtyIfHandled(wifiApp.handleMenuButton());
}

void openWifi() { wifiApp.open(); }
void releaseWifi() { wifiApp.release(); }

AppBehavior wifiBehavior() {
  AppBehavior behavior;
  behavior.onEnter = openWifi;
  behavior.onMenu = handleWifiMenu;
  behavior.onRelease = releaseWifi;
  return behavior;
}
#endif

#if ENABLE_TTS
AppBehavior ttsBehavior() {
  AppBehavior behavior;
  behavior.onExit = stopTtsAudio;
  behavior.onMenu = handleTtsMenu;
  behavior.onMenuDouble = handleTtsMenuDouble;
  behavior.onMenuLong = handleTtsMenuLong;
  behavior.onPower = handleTtsPower;
  return behavior;
}
#endif

} // namespace

const AppDefinition builtInApps[] = {
    builtInApp("tictactoe", "tictac", "T", MENU_GAMES, &ticTacToeScreen,
               []() { ticTacToe.reset(); },
               contextBehavior(saveTicTacToeContext, restoreTicTacToeContext)),
    builtInApp("minesweeper", "mines", "M", MENU_GAMES, &minesweeperScreen,
               []() { minesweeper.reset(); },
               powerContextBehavior(handleMinesweeperPower,
                                    saveMinesweeperContext,
                                    restoreMinesweeperContext)),
    builtInApp("hangman", "hangman", "H", MENU_GAMES, &hangmanScreen,
               []() { hangman.reset(); },
               powerContextBehavior(handleHangmanPower, saveHangmanContext,
                                    restoreHangmanContext)),
    builtInApp("sudoku", "sudoku", "S", MENU_GAMES, &sudokuScreen,
               []() { sudoku.reset(); },
               contextBehavior(saveSudokuContext, restoreSudokuContext)),
    builtInApp("wordle", "wordle", "W", MENU_GAMES, &wordleScreen,
               []() { wordle.reset(); },
               powerContextBehavior(handleWordlePower, saveWordleContext,
                                    restoreWordleContext)),
    withInactivityDeepSleep(
        builtInApp("chess", "chess", "C", MENU_GAMES, &chessScreen,
                   []() { chess.reset(); }, chessBehavior()),
        CHESS_INACTIVITY_DEEP_SLEEP_MS),
    builtInApp("gfx", "gfx", "G", MENU_APPS, &gfxScreen,
               []() { gfxApp.reset(); }, gfxBehavior()),
    builtInApp("calculator", "calc", "=", MENU_APPS, &calculatorScreen,
               []() { calculator.reset(); }),
    builtInApp("calendar", "calendar", "C", MENU_APPS, &calendarScreen,
               []() { calendar.reset(); }),
    builtInApp("qr", "qr", "Q", MENU_APPS, &qrScreen,
               []() { qrApp.reset(); },
               contextBehavior(saveQrContext, restoreQrContext)),
    builtInApp("paint", "paint", "P", MENU_APPS, &paintScreen,
               []() { paintApp.reset(); }, paintBehavior()),
    builtInApp("deghost", "deghost", "D", MENU_APPS, &deghostScreen,
               []() { deghostApp.reset(); },
               deghostBehavior()),
    builtInIconAppWhen("files", "files", "N", MENU_APPS, &filesScreen,
                       sdStorageMounted,
                       []() { filesApp.reset(); },
                       filesBehavior()),
    contactLinksApp,
    builtInAppWhen("settings", "settings", "S", MENU_APPS, &settingsScreen,
                   sdStorageMounted,
                   []() { settingsApp.reset(); },
                   settingsBehavior())
#if ENABLE_TTS
    ,
    builtInAppWhen("tts", "tts", "S", MENU_APPS, &ttsScreen, sdStorageMounted,
                   []() { ttsApp.reset(); },
                   ttsBehavior())
#endif
#if ENABLE_NETWORK_APPS
    ,
    builtInApp("wifi", "wifi", "W", MENU_NETWORK, &wifiScreen,
               []() { wifiApp.reset(); }, wifiBehavior()),
    builtInApp("reddit", "reddit", "R", MENU_NETWORK, &redditScreen,
               []() { redditApp.reset(); }, redditBehavior()),
    builtInApp("hn", "hn", "H", MENU_NETWORK, &hnScreen,
               []() { hnApp.reset(); }, hnBehavior()),
    builtInApp("ai", "ai", "A", MENU_NETWORK, &aiScreen,
               []() { aiApp.reset(); }, aiBehavior()),
    builtInApp("weather", "weather", "C", MENU_NETWORK, &weatherScreen,
               []() { weatherApp.reset(); },
               weatherBehavior())
#endif
};

const size_t builtInAppCount = sizeof(builtInApps) / sizeof(builtInApps[0]);

bool appVisible(const AppDefinition &app) {
  if (app.id != nullptr &&
      strcmp(app.id, currentSystemConfig().home.sunBtnApp) == 0) {
    return false;
  }
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

bool tryGetVisibleAppAtIndex(MenuCategory category, size_t index,
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
    if (strcmp(builtInApps[i].id, id) == 0) {
      copyCatalogEntry(out, builtInApps[i]);
      return true;
    }
  }
  return pinkExecutableDefinitionById(id, &out.definition, out.id,
                                      sizeof(out.id), out.label,
                                      sizeof(out.label), out.icon,
                                      sizeof(out.icon));
}

void refreshAppCatalog() {}

bool takePendingPinkExecLaunchPath(char *path, size_t pathSize) {
  return filesApp.consumePinkLaunch(path, pathSize);
}

void resetApps() {
  for (size_t i = 0; i < builtInAppCount; i++) {
    if (builtInApps[i].reset != nullptr) {
      builtInApps[i].reset();
    }
  }
#if ENABLE_NETWORK_APPS
  wifiManager.reset();
#endif
}
