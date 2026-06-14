#include "sys/power_control.h"
#include "sys/global.h"

#include <Arduino.h>

#if __has_include(<esp_sleep.h>)
#include <esp_sleep.h>
#define POCKET_INK_HAS_ESP_SLEEP 1
#else
#define POCKET_INK_HAS_ESP_SLEEP 0
#endif

#if __has_include(<driver/gpio.h>)
#include <driver/gpio.h>
#define POCKET_INK_HAS_GPIO_DRIVER 1
#else
#define POCKET_INK_HAS_GPIO_DRIVER 0
#endif

#if __has_include(<driver/rtc_io.h>)
#include <driver/rtc_io.h>
#define POCKET_INK_HAS_RTC_IO 1
#else
#define POCKET_INK_HAS_RTC_IO 0
#endif

namespace {

void releaseDigitalHold(int pin) {
#if POCKET_INK_HAS_ESP_SLEEP && POCKET_INK_HAS_GPIO_DRIVER
  gpio_hold_dis(static_cast<gpio_num_t>(pin));
#else
  (void)pin;
#endif
}

void releaseRtcHold(int pin) {
#if POCKET_INK_HAS_ESP_SLEEP && POCKET_INK_HAS_RTC_IO
  rtc_gpio_hold_dis(static_cast<gpio_num_t>(pin));
  rtc_gpio_deinit(static_cast<gpio_num_t>(pin));
#else
  (void)pin;
#endif
}

void holdDigitalOutput(int pin, int level) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
#if POCKET_INK_HAS_ESP_SLEEP && POCKET_INK_HAS_GPIO_DRIVER
  gpio_hold_en(static_cast<gpio_num_t>(pin));
#endif
}

void holdRtcOutput(int pin, int level) {
  holdDigitalOutput(pin, level);
#if POCKET_INK_HAS_ESP_SLEEP && POCKET_INK_HAS_RTC_IO
  rtc_gpio_init(static_cast<gpio_num_t>(pin));
  rtc_gpio_set_direction(static_cast<gpio_num_t>(pin),
                         RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level(static_cast<gpio_num_t>(pin), level);
  rtc_gpio_hold_en(static_cast<gpio_num_t>(pin));
#endif
}

void holdSleepRails(bool keepEpdPowerOn) {
  holdRtcOutput(EPD_PWR_PIN, keepEpdPowerOn ? LOW : HIGH);
  holdRtcOutput(EPD_TP_RST_PIN, LOW);
  holdDigitalOutput(PA_CTRL_PIN, LOW);
  holdDigitalOutput(AUDIO_PWR_PIN, HIGH);
#if POCKET_INK_HAS_ESP_SLEEP && POCKET_INK_HAS_GPIO_DRIVER
  gpio_deep_sleep_hold_en();
#endif
}

void holdDeepSleepPowerLatch() { holdRtcOutput(BAT_CTRL_PIN, HIGH); }

void releasePowerLatch() {
  digitalWrite(BAT_CTRL_PIN, LOW);
  pinMode(BAT_CTRL_PIN, INPUT);
}

void startDeepSleepWakeOnPowerButton(uint64_t timerWakeupUs = 0) {
  pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
#if POCKET_INK_HAS_ESP_SLEEP
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PWR_BUTTON_PIN), 0);
  if (timerWakeupUs > 0) {
    esp_sleep_enable_timer_wakeup(timerWakeupUs);
  }
  esp_deep_sleep_start();
#else
  (void)timerWakeupUs;
  ESP.restart();
#endif
}

} // namespace

void releasePowerHolds() {
#if POCKET_INK_HAS_ESP_SLEEP && POCKET_INK_HAS_GPIO_DRIVER
  gpio_deep_sleep_hold_dis();
#endif
  releaseRtcHold(EPD_PWR_PIN);
  releaseRtcHold(EPD_TP_RST_PIN);
  releaseRtcHold(BAT_CTRL_PIN);
  releaseDigitalHold(EPD_PWR_PIN);
  releaseDigitalHold(EPD_TP_RST_PIN);
  releaseDigitalHold(BAT_CTRL_PIN);
  releaseDigitalHold(PA_CTRL_PIN);
  releaseDigitalHold(AUDIO_PWR_PIN);
}

void keepPowerLatchOn() {
  pinMode(BAT_CTRL_PIN, OUTPUT);
  digitalWrite(BAT_CTRL_PIN, HIGH);
}

void rebootDevice() { ESP.restart(); }

void powerOffDevice() {
  holdSleepRails(false);
  releasePowerLatch();
  delay(250);
  startDeepSleepWakeOnPowerButton();
}

void enterDeepSleep(uint64_t timerWakeupUs, bool keepEpdPowerOn) {
  keepPowerLatchOn();
  holdSleepRails(keepEpdPowerOn);
  holdDeepSleepPowerLatch();
  startDeepSleepWakeOnPowerButton(timerWakeupUs);
}

bool deepSleepWokeFromTimer() {
#if POCKET_INK_HAS_ESP_SLEEP
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
#else
  return false;
#endif
}
