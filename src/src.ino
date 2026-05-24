#include "app_display.h"
#include "keyboard_component.h"
#include "minesweeper_app.h"
#include "smart_button.h"
#include "tictactoe_app.h"
#include "touch_input.h"

enum Screen {
  SCREEN_KEYBOARD,
  SCREEN_MENU,
  SCREEN_TICTACTOE,
  SCREEN_MINESWEEPER
};

AppDisplay display;
TouchInput touch;
KeyboardComponent keyboard;
TicTacToeApp ticTacToe;
MinesweeperApp minesweeper;
SmartButton mainButton(BOOT_BUTTON_PIN);
SmartButton backButton(PWR_BUTTON_PIN);

Screen screen = SCREEN_KEYBOARD;
String typedText;
bool dirty = true;

void switchTo(Screen next) {
  screen = next;
  dirty = true;
}

void drawMenu() {
  display.setTextColor(1);
  display.setTextSize(2);
  display.setCursor(60, 10);
  display.print("APPS");

  const char icons[9] = {'T', 'M', 'A', 'B', 'C', 'D', 'E', 'F', 'G'};
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
  default:
    break;
  }
}

void handleKeyboardTouch(const TouchPoint &point) {
  KeyboardEvent event = keyboard.hitTest(point);
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
  case KEY_CLEAR:
    typedText = "";
    break;
  case KEY_NONE:
    return;
  }
  dirty = true;
}

void render() {
  display.clear();
  switch (screen) {
  case SCREEN_KEYBOARD:
    keyboard.draw(display, typedText);
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
  }
  display.flush();
  dirty = false;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  touch.begin();
  display.begin();

  mainButton.begin();
  backButton.begin();
  mainButton.setLongPressMs(1200);
  backButton.setLongPressMs(1200);

  mainButton.attachSingleClick([]() { switchTo(SCREEN_MENU); });
  mainButton.attachLongPressStart([]() { switchTo(SCREEN_MENU); });
  backButton.attachSingleClick([]() { switchTo(SCREEN_KEYBOARD); });
  backButton.attachLongPressStart([]() { switchTo(SCREEN_KEYBOARD); });

  ticTacToe.reset();
  minesweeper.reset();
  dirty = true;
}

void loop() {
  mainButton.update();
  backButton.update();

  TouchPoint point;
  if (touch.read(point)) {
    switch (screen) {
    case SCREEN_KEYBOARD:
      handleKeyboardTouch(point);
      break;
    case SCREEN_MENU:
      handleMenuTouch(point);
      break;
    case SCREEN_TICTACTOE:
      dirty = ticTacToe.handleTouch(point) || dirty;
      break;
    case SCREEN_MINESWEEPER:
      dirty = minesweeper.handleTouch(point) || dirty;
      break;
    }
  }

  if (dirty) {
    render();
  }
  delay(10);
}
