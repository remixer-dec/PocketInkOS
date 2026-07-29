#include "sys/builtin_apps.h"
#include "sys/app_config.h"
#include "sys/system_config.h"
#include "apps/alarm_app.h"
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
#include "sys/services/sd_storage.h"
#include "sys/services/wifi_manager.h"
#if ENABLE_TTS
#include "apps/tts_app.h"
#endif
#if ENABLE_NETWORK_APPS
#include "netapps/ai_app.h"
#include "netapps/currency_exchange_app.h"
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
AlarmApp alarmApp;
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
CurrencyExchangeApp currencyExchangeApp;
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
AppScreen<AlarmApp, true> alarmScreen(alarmApp);
AppScreen<CalendarApp, true> calendarScreen(calendar);
AppScreen<QrApp, true> qrScreen(qrApp);
TouchlessAppScreen<PaintApp> paintScreen(paintApp);
AppScreen<DeghostApp, true> deghostScreen(deghostApp);
AppScreen<GfxApp, true> gfxScreen(gfxApp);
AppScreen<FilesApp, true> filesScreen(filesApp);
AppScreen<ContactLinksApp, true> contactLinksScreen(contactLinks);
AppScreen<SettingsApp, true> settingsScreen(settingsApp);
#if ENABLE_TTS
AppScreen<TtsApp, true> ttsScreen(ttsApp);
#endif
#if ENABLE_NETWORK_APPS
AppScreen<WifiApp, true> wifiScreen(wifiApp);
AppScreen<RedditApp, true> redditScreen(redditApp);
AppScreen<HnApp, true> hnScreen(hnApp);
AppScreen<AiApp, true> aiScreen(aiApp);
AppScreen<WeatherApp, true> weatherScreen(weatherApp);
AppScreen<CurrencyExchangeApp, true>
    currencyExchangeScreen(currencyExchangeApp);
#endif

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
AppEventResult handleHnMenu() {
  if (hnApp.handleMenuButton()) {
    return AppEventResult::Dirty;
  }
  return AppEventResult::GoMenu;
}
#endif

AppDefinition builtInApp(const char *id, const char *label, const char *icon,
                         MenuCategory category, ActiveApp *runtime,
                         AppCallback reset = nullptr,
                         AppBehavior behavior = AppBehavior{},
                         bool useIconFont = false,
                         AppVisibleHandler showCondition = nullptr) {
  AppDefinition app = {id, label, icon, category, runtime};
  app.reset = reset;
  app.behavior = behavior;
  app.iconFont = useIconFont;
  app.visible = showCondition;
  return app;
}

AppDefinition withInactivityDeepSleep(AppDefinition app, uint32_t timeoutMs) {
  app.inactivityDeepSleepMs = timeoutMs;
  return app;
}

const AppDefinition contactLinksApp =
    builtInApp("contact_links", "contact", "C", MENU_APPS, &contactLinksScreen,
               []() { contactLinks.reset(); },
               defaultBehavior()
                   .withPowerBtn([]() { return AppEventResult::GoHome; })
                   .build());

#if ENABLE_NETWORK_APPS
AppEventResult handleWifiMenu() {
  return dirtyIfHandled(wifiApp.handleMenuButton());
}
#endif

} // namespace

const AppDefinition builtInApps[] = {
    builtInApp("tictactoe", "tictac", "T", MENU_GAMES, &ticTacToeScreen,
               []() { ticTacToe.reset(); },
               defaultContextBehavior<TicTacToeApp, ticTacToe>().build()),
    builtInApp("minesweeper", "mines", "M", MENU_GAMES, &minesweeperScreen,
               []() { minesweeper.reset(); },
               defaultContextBehavior<MinesweeperApp, minesweeper>()
                   .withPowerBtn(dirtyIfHandledMethod<
                              MinesweeperApp, minesweeper,
                              &MinesweeperApp::handlePowerButton>)
                   .build()),
    builtInApp("hangman", "hangman", "H", MENU_GAMES, &hangmanScreen,
               []() { hangman.reset(); },
               defaultContextBehavior<HangmanApp, hangman>()
                   .withPowerBtn(dirtyIfHandledMethod<
                              HangmanApp, hangman,
                              &HangmanApp::openKeyboardFromButton>)
                   .withMenuBtn(dirtyIfHandledMethod<
                                 HangmanApp, hangman,
                                 &HangmanApp::handleMenuButton>,
                             dirtyIfHandledMethod<
                                 HangmanApp, hangman,
                                 &HangmanApp::handleMenuDoubleButton>,
                             dirtyIfHandledMethod<
                                 HangmanApp, hangman,
                                 &HangmanApp::handleMenuLongButton>)
                   .build()),
    builtInApp("sudoku", "sudoku", "S", MENU_GAMES, &sudokuScreen,
               []() { sudoku.reset(); },
               defaultContextBehavior<SudokuApp, sudoku>().build()),
    builtInApp("wordle", "wordle", "W", MENU_GAMES, &wordleScreen,
               []() { wordle.reset(); },
               defaultContextBehavior<WordleApp, wordle>()
                   .withPowerBtn(dirtyIfHandledMethod<
                              WordleApp, wordle,
                              &WordleApp::openKeyboardFromButton>)
                   .withMenuBtn(dirtyIfHandledMethod<
                                 WordleApp, wordle,
                                 &WordleApp::handleMenuButton>,
                             dirtyIfHandledMethod<
                                 WordleApp, wordle,
                                 &WordleApp::handleMenuDoubleButton>,
                             dirtyIfHandledMethod<
                                 WordleApp, wordle,
                                 &WordleApp::handleMenuLongButton>)
                   .build()),
    withInactivityDeepSleep(
        builtInApp("chess", "chess", "C", MENU_GAMES, &chessScreen,
                   []() { chess.reset(); },
                   defaultContextBehavior<ChessApp, chess>()
                       .withEnter([]() { chess.start(display); })
                       .withMenuBtn(handleChessMenu, handleChessMenuDouble,
                                 handleChessMenuLong)
                       .withPowerBtn(handleChessPower)
                       .withRelease([]() { chess.release(); })
                       .build()),
        CHESS_INACTIVITY_DEEP_SLEEP_MS),
    builtInApp("gfx", "gfx", "G", MENU_APPS, &gfxScreen,
               []() { gfxApp.reset(); },
               defaultBehavior()
                   .withEnter([]() { gfxApp.start(display); })
                   .withPowerBtn(dirtyIfHandledMethod<
                              GfxApp, gfxApp, &GfxApp::handlePowerButton>)
                   .withRelease([]() { gfxApp.release(); })
                   .build()),
    builtInApp("calculator", "calc", "=", MENU_APPS, &calculatorScreen,
               []() { calculator.reset(); }),
    builtInApp(
        "alarm", "alarm", "F", MENU_APPS, &alarmScreen,
        []() { alarmApp.reset(); },
        defaultBehavior()
            .withEnter([]() { alarmApp.onEnter(); })
            .withMenuBtn(dirtyIfHandledMethod<AlarmApp, alarmApp,
                                               &AlarmApp::handleMenuButton>)
            .withPowerBtn(dirtyIfHandledMethod<AlarmApp, alarmApp,
                                                &AlarmApp::handlePowerButton>)
            .withPeriodic(AlarmApp::onPeriodic, 1,
                          PeriodicWakeLevel::Basic,
                          PeriodicDisplayMode::None)
            .withWidget("Alarm", AlarmApp::drawWidget)
            .withRelease([]() { alarmApp.release(); })
            .build(),
        true),
    builtInApp("calendar", "calendar", "C", MENU_APPS, &calendarScreen,
               []() { calendar.reset(); }),
    builtInApp("qr", "qr", "Q", MENU_APPS, &qrScreen,
               []() { qrApp.reset(); },
               defaultContextBehavior<QrApp, qrApp>()
                   .withMenuBtn(dirtyIfHandledMethod<QrApp, qrApp,
                                                  &QrApp::handleMenuButton>,
                             dirtyIfHandledMethod<
                                 QrApp, qrApp,
                                 &QrApp::handleMenuDoubleButton>,
                             dirtyIfHandledMethod<
                                 QrApp, qrApp,
                                 &QrApp::handleMenuLongButton>)
                   .build()),
    builtInApp("paint", "paint", "P", MENU_APPS, &paintScreen,
               []() { paintApp.reset(); },
               defaultBehavior()
                   .withEnter([]() { paintApp.start(display); })
                   .withExit([]() { paintApp.stop(); })
                   .withPowerBtn([]() {
                     paintApp.clear();
                     return AppEventResult::Handled;
                   })
                   .withRawTouch([](const TouchEvent &event) {
                     paintApp.handleTouchEvent(event);
                   })
                   .build()),
    builtInApp("deghost", "deghost", "D", MENU_APPS, &deghostScreen,
               []() { deghostApp.reset(); },
               defaultBehavior()
                   .withEnter([]() { deghostApp.start(display); })
                   .withMenuBtn([]() { return deghostApp.handleBackButton(); },
                             nullptr,
                             []() { return deghostApp.handleBackButton(); })
                   .withPowerBtn(dirtyIfHandledMethod<
                              DeghostApp, deghostApp,
                              &DeghostApp::handlePowerButton>)
                   .build()),
    builtInApp("files", "files", "N", MENU_APPS, &filesScreen,
                       []() { filesApp.reset(); },
                       defaultContextBehavior<FilesApp, filesApp>()
                           .withMenuBtn(
                               dirtyIfHandledMethod<
                                   FilesApp, filesApp,
                                   &FilesApp::handleMenuButton>,
                               dirtyIfHandledMethod<
                                   FilesApp, filesApp,
                                   &FilesApp::handleMenuDoubleButton>)
                           .withMenuLongBtn(
                               dirtyIfHandledMethod<
                                   FilesApp, filesApp,
                                   &FilesApp::handleMenuLongButton>)
                           .withPowerBtn(dirtyIfHandledMethod<
                              FilesApp, filesApp,
                              &FilesApp::handlePowerButton>,
                              dirtyIfHandledMethod<
                                  FilesApp, filesApp,
                                  &FilesApp::handlePowerDoubleButton>)
                           .withPowerLongBtn(
                              dirtyIfHandledMethod<
                                  FilesApp, filesApp,
                                  &FilesApp::handleMenuLongButton>)
                           .withRelease([]() { filesApp.release(); })
                           .build(),
                       true, sdStorageMounted),
    contactLinksApp,
    builtInApp("settings", "settings", "S", MENU_APPS, &settingsScreen,
                   []() { settingsApp.reset(); },
                   defaultBehavior()
                       .withMenuBtn(dirtyIfHandledMethod<
                                    SettingsApp, settingsApp,
                                    &SettingsApp::handleMenuButton>)
                       .withPowerBtn(dirtyIfHandledMethod<
                                     SettingsApp, settingsApp,
                                     &SettingsApp::handlePowerButton>)
                       .withRelease([]() { settingsApp.release(); })
                       .build())
#if ENABLE_TTS
    ,
    builtInApp("tts", "tts", "S", MENU_APPS, &ttsScreen,
               []() { ttsApp.reset(); },
                   defaultBehavior()
                       .withExit([]() { ttsApp.cancelAndStop(); })
                       .withRelease([]() { ttsApp.release(); })
                       .withMenuBtn(dirtyIfHandledMethod<
                                     TtsApp, ttsApp,
                                     &TtsApp::handleMenuButton>,
                                 dirtyIfHandledMethod<
                                     TtsApp, ttsApp,
                                     &TtsApp::handleMenuDoubleButton>,
                                 dirtyIfHandledMethod<
                                     TtsApp, ttsApp,
                                     &TtsApp::handleMenuLongButton>)
                       .withPowerBtn(dirtyIfHandledMethod<
                                  TtsApp, ttsApp,
                                  &TtsApp::handlePowerButton>)
                       .build(),
               false, sdStorageMounted)
#endif
#if ENABLE_NETWORK_APPS
    ,
    builtInApp("wifi", "wifi", "W", MENU_NETWORK, &wifiScreen,
               []() { wifiApp.reset(); },
               defaultBehavior()
                   .withEnter([]() { wifiApp.open(); })
                   .withMenuBtn(handleWifiMenu)
                   .withRelease([]() { wifiApp.release(); })
                   .build()),
    builtInApp("reddit", "reddit", "R", MENU_NETWORK, &redditScreen,
               []() { redditApp.reset(); },
               defaultBehavior()
                   .withMenuBtn(dirtyIfHandledMethod<
                      RedditApp, redditApp, &RedditApp::handleMenuButton>)
                   .withPowerBtn(dirtyIfHandledMethod<
                      RedditApp, redditApp, &RedditApp::handlePowerButton>)
                   .withRelease([]() { redditApp.release(); })
                   .build()),
    builtInApp("hn", "hn", "H", MENU_NETWORK, &hnScreen,
               []() { hnApp.reset(); },
               defaultBehavior()
                   .withMenuBtn(handleHnMenu)
                   .withPowerBtn(dirtyIfHandledMethod<
                      HnApp, hnApp, &HnApp::handlePowerButton>)
                   .withRelease([]() { hnApp.release(); })
                   .build()),
    builtInApp("ai", "ai", "A", MENU_NETWORK, &aiScreen,
               []() { aiApp.reset(); },
               defaultBehavior()
                   .withExit([]() { aiApp.stopAudio(); })
                   .withMenuBtn(dirtyIfHandledMethod<
                                 AiApp, aiApp,
                                 &AiApp::handleMenuButton>,
                             dirtyIfHandledMethod<
                                 AiApp, aiApp,
                                 &AiApp::handleMenuDoubleButton>,
                             dirtyIfHandledMethod<
                                 AiApp, aiApp,
                                 &AiApp::handleMenuLongButton>)
                   .withPowerBtn(dirtyIfHandledMethod<
                              AiApp, aiApp, &AiApp::handlePowerButton>)
                   .withRelease([]() { aiApp.release(); })
                   .build()),
    builtInApp("weather", "weather", "C", MENU_NETWORK, &weatherScreen,
               []() { weatherApp.reset(); },
               defaultBehavior()
                   .withRelease([]() { weatherApp.release(); })
                   .build()),
    builtInApp("currency", "rates", "$", MENU_NETWORK,
               &currencyExchangeScreen,
               []() { currencyExchangeApp.reset(); },
               defaultBehavior()
                   .withMenuBtn(dirtyIfHandledMethod<
                                CurrencyExchangeApp, currencyExchangeApp,
                                &CurrencyExchangeApp::handleMenuButton>)
                   .withRelease([]() { currencyExchangeApp.release(); })
                   .build())
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

size_t appCatalogAllCount() {
  return builtInAppCount + pinkExecutableAllCount();
}

bool tryGetAppAtIndex(size_t index, AppCatalogEntry &out) {
  if (index < builtInAppCount) {
    copyCatalogEntry(out, builtInApps[index]);
    return true;
  }
  return pinkExecutableDefinitionAllAt(index - builtInAppCount,
                                       &out.definition, out.id,
                                       sizeof(out.id), out.label,
                                       sizeof(out.label), out.icon,
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

size_t builtInAppDefinitionCount() { return builtInAppCount; }

const AppDefinition *builtInAppDefinitionAt(size_t index) {
  return index < builtInAppCount ? &builtInApps[index] : nullptr;
}

void refreshAppCatalog() {}

bool takePendingPinkExecLaunchPath(char *path, size_t pathSize) {
  return filesApp.consumePinkLaunch(path, pathSize);
}

bool takePendingPocketInkJSLaunchPath(char *path, size_t pathSize) {
  return filesApp.consumeJsLaunch(path, pathSize);
}

void resetApps() {
  appConfigRegistryReset();
  calendarAppConfigRegister();
#if ENABLE_NETWORK_APPS
  aiAppConfigRegister();
  weatherAppConfigRegister();
#endif
  for (size_t i = 0; i < builtInAppCount; i++) {
    if (builtInApps[i].reset != nullptr) {
      builtInApps[i].reset();
    }
  }
#if ENABLE_NETWORK_APPS
  wifiManager.reset();
#endif
}
