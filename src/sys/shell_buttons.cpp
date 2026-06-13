#include "sys/shell_buttons.h"
#include "sys/global.h"
#include "ui/components/smart_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

namespace {

SmartButton mainButton(BOOT_BUTTON_PIN);
SmartButton backButton(PWR_BUTTON_PIN);
TaskHandle_t buttonTaskHandle = NULL;
ShellButtonHandlers activeHandlers;

volatile uint8_t pendingMenuClicks = 0;
volatile uint8_t pendingMenuDoubleClicks = 0;
volatile uint8_t pendingMenuLongPresses = 0;
volatile uint8_t pendingPowerClicks = 0;
volatile uint8_t pendingPowerDoubleClicks = 0;
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

void call(ShellButtonCallback callback) {
  if (callback != nullptr) {
    callback();
  }
}

void buttonTask(void *) {
  while (true) {
    mainButton.update();
    backButton.update();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

} // namespace

void shellButtonsBegin(const ShellButtonHandlers &handlers) {
  activeHandlers = handlers;

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
  backButton.attachDoubleClick(
      []() { queueButtonEvent(pendingPowerDoubleClicks); });
  backButton.attachLongPressStart(
      []() { queueButtonEvent(pendingPowerLongPresses); });

  if (buttonTaskHandle == NULL) {
    xTaskCreatePinnedToCore(buttonTask, "buttons", 2048, NULL, 2,
                            &buttonTaskHandle, 0);
  }
}

void shellButtonsDispatch() {
  while (consumeButtonEvent(pendingMenuLongPresses)) {
    call(activeHandlers.onMenuLong);
  }
  while (consumeButtonEvent(pendingMenuDoubleClicks)) {
    call(activeHandlers.onMenuDouble);
  }
  while (consumeButtonEvent(pendingMenuClicks)) {
    call(activeHandlers.onMenu);
  }
  while (consumeButtonEvent(pendingPowerLongPresses)) {
    call(activeHandlers.onPowerLong);
  }
  while (consumeButtonEvent(pendingPowerDoubleClicks)) {
    call(activeHandlers.onPowerDouble);
  }
  while (consumeButtonEvent(pendingPowerClicks)) {
    call(activeHandlers.onPower);
  }
}
