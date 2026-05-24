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
  int16_t titleX;
  int16_t titleY;
  uint16_t titleW;
  uint16_t titleH;
  display.getTextBounds("APPS", 0, 10, &titleX, &titleY, &titleW, &titleH);
  display.setCursor((EPD_WIDTH - titleW) / 2, 10);
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

bool handleKeyboardTouch(const TouchPoint &point) {
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
    return false;
  }
  return true;
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

  display.begin();
  touch.begin();

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
      if (handleKeyboardTouch(point)) {
        keyboard.drawInput(display, typedText);
        display.flushPartial(4, 4, 192, 34);
      }
      break;
    case SCREEN_MENU:
      handleMenuTouch(point);
      break;
    case SCREEN_TICTACTOE:
      if (ticTacToe.handleTouch(point)) {
        display.fillRect(0, 32, 200, 168, 0);
        ticTacToe.draw(display);
        display.flushPartial(0, 32, 200, 168);
      }
      break;
    case SCREEN_MINESWEEPER:
      if (minesweeper.handleTouch(point)) {
        display.fillRect(0, 16, 200, 184, 0);
        minesweeper.draw(display);
        display.flushPartial(0, 16, 200, 184);
      }
      break;
    }
  }

  if (dirty) {
    render();
  }
  delay(10);
}
