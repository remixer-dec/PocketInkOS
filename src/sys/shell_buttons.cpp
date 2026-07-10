#include "sys/shell_buttons.h"
#include "sys/global.h"
#include "sys/hw_button.h"
#include "sys/power_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <stdint.h>

namespace {

HwButton mainButton(BOOT_BUTTON_PIN);
HwButton backButton(PWR_BUTTON_PIN);
TaskHandle_t buttonTaskHandle = NULL;
ShellButtonHandlers activeHandlers;
static const uint16_t kEmergencyPowerOffHoldMs = 10000;

using EventCounter = std::atomic<uint8_t>;

EventCounter pendingMenuClicks{0};
EventCounter pendingMenuDoubleClicks{0};
EventCounter pendingMenuLongPresses{0};
EventCounter pendingPowerClicks{0};
EventCounter pendingPowerDoubleClicks{0};
EventCounter pendingPowerLongPresses{0};
EventCounter pendingButtonActivity{0};
std::atomic<bool> emergencyPowerOffStarted{false};

void queueButtonEvent(EventCounter &counter) {
  uint8_t current = counter.load(std::memory_order_relaxed);
  while (current != UINT8_MAX &&
         !counter.compare_exchange_weak(current, current + 1,
                                        std::memory_order_relaxed)) {
  }
}

bool consumeButtonEvent(EventCounter &counter) {
  uint8_t current = counter.load(std::memory_order_relaxed);
  while (current != 0) {
    if (counter.compare_exchange_weak(current, current - 1,
                                      std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

void call(ShellButtonCallback callback) {
  if (callback != nullptr) {
    callback();
  }
}

void emergencyPowerOff() {
  if (emergencyPowerOffStarted.exchange(true)) {
    return;
  }
  emergencyPowerOffDevice();
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
  backButton.setDoubleClickMs(300);

  mainButton.attachSingleClick([]() { queueButtonEvent(pendingMenuClicks); });
  mainButton.attachDoubleClick(
      []() { queueButtonEvent(pendingMenuDoubleClicks); });
  mainButton.attachLongPressStart(
      []() { queueButtonEvent(pendingMenuLongPresses); });
  mainButton.attachActivity([]() { queueButtonEvent(pendingButtonActivity); });
  backButton.attachSingleClick([]() { queueButtonEvent(pendingPowerClicks); });
  backButton.attachDoubleClick(
      []() { queueButtonEvent(pendingPowerDoubleClicks); });
  backButton.attachLongPressStart(
      []() { queueButtonEvent(pendingPowerLongPresses); });
  backButton.attachHold(emergencyPowerOff, kEmergencyPowerOffHoldMs);
  backButton.attachActivity([]() { queueButtonEvent(pendingButtonActivity); });

  if (buttonTaskHandle == NULL) {
    xTaskCreatePinnedToCore(buttonTask, "buttons", 2048, NULL, 2,
                            &buttonTaskHandle, 0);
  }
}

bool shellButtonsDispatch() {
  bool dispatched = consumeButtonEvent(pendingButtonActivity);
  while (consumeButtonEvent(pendingButtonActivity)) {
    dispatched = true;
  }
  while (consumeButtonEvent(pendingMenuLongPresses)) {
    dispatched = true;
    call(activeHandlers.onMenuLong);
  }
  while (consumeButtonEvent(pendingMenuDoubleClicks)) {
    dispatched = true;
    call(activeHandlers.onMenuDouble);
  }
  while (consumeButtonEvent(pendingMenuClicks)) {
    dispatched = true;
    call(activeHandlers.onMenu);
  }
  while (consumeButtonEvent(pendingPowerLongPresses)) {
    dispatched = true;
    call(activeHandlers.onPowerLong);
  }
  while (consumeButtonEvent(pendingPowerDoubleClicks)) {
    dispatched = true;
    call(activeHandlers.onPowerDouble);
  }
  while (consumeButtonEvent(pendingPowerClicks)) {
    dispatched = true;
    call(activeHandlers.onPower);
  }
  return dispatched;
}
