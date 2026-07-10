#include "apps/alarm_app.h"

#include "fs/providers/resonatto/remidi_synth.h"
#include "sys/audio_playback_renderer.h"
#include "sys/device_clock.h"
#include "sys/global.h"
#include "ui/components/select_button_component.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr uint8_t kTabTimer = 0;
constexpr uint8_t kTabAlarm = 1;
constexpr uint16_t kTimerMinimumMinutes = 1;
constexpr uint16_t kTimerMaximumMinutes = 240;
constexpr unsigned long kTimerRedrawMs = 1000;

enum ControlId : int8_t {
  CONTROL_TAB_TIMER = 0,
  CONTROL_TAB_ALARM,
  CONTROL_PRESET_ONE,
  CONTROL_PRESET_TEN,
  CONTROL_TIMER_MINUS_TEN,
  CONTROL_TIMER_MINUS_ONE,
  CONTROL_TIMER_PLUS_ONE,
  CONTROL_TIMER_PLUS_TEN,
  CONTROL_TIMER_START,
  CONTROL_TIMER_STOP,
  CONTROL_TIMER_RESET,
  CONTROL_ALARM_HOUR_DOWN,
  CONTROL_ALARM_HOUR_UP,
  CONTROL_ALARM_MINUTE_DOWN,
  CONTROL_ALARM_MINUTE_UP,
  CONTROL_ALARM_TOGGLE,
};

const SelectButtonOption kTabOptions[] = {
    {{4, 4, 94, 20}, "TIMER", kTabTimer},
    {{102, 4, 94, 20}, "ALARM", kTabAlarm},
};

const UiRect kPresetOneButton = {4, 36, 66, 22};
const UiRect kPresetTenButton = {130, 36, 66, 22};
const UiRect kTimerMinusTenButton = {4, 70, 36, 24};
const UiRect kTimerMinusOneButton = {44, 70, 36, 24};
const UiRect kTimerPlusOneButton = {120, 70, 36, 24};
const UiRect kTimerPlusTenButton = {160, 70, 36, 24};
const UiRect kTimerStartButton = {48, 142, 104, 30};
const UiRect kTimerStopButton = {28, 142, 68, 28};
const UiRect kTimerResetButton = {104, 142, 68, 28};
const UiRect kAlarmHourDownButton = {16, 112, 36, 24};
const UiRect kAlarmHourUpButton = {56, 112, 36, 24};
const UiRect kAlarmMinuteDownButton = {108, 112, 36, 24};
const UiRect kAlarmMinuteUpButton = {148, 112, 36, 24};
const UiRect kAlarmToggleButton = {48, 150, 104, 30};

void drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                      uint8_t textSize = 1) {
  gfx.setTextSize(textSize);
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(textW)) / 2 - textX, y);
  gfx.print(text);
}

bool deadlineReached(unsigned long now, unsigned long deadline) {
  return static_cast<long>(now - deadline) >= 0;
}

uint16_t remainingSeconds(unsigned long now, unsigned long deadline) {
  if (deadlineReached(now, deadline)) {
    return 0;
  }
  const unsigned long remainingMs = deadline - now;
  return static_cast<uint16_t>((remainingMs + 999U) / 1000U);
}

void setChimeEvent(remidi_event &event, uint32_t timeMs, uint8_t note,
                   uint8_t velocity, bool on) {
  event = remidi_event{};
  event.timeMs = timeMs;
  event.channel = 0;
  event.note = note;
  event.velocity = velocity;
  event.on = on;
}

} // namespace

struct AlarmApp::AudioState {
  remidi_event events[6] = {};
  remidi_sequence sequence = {};
  RemidiSynth synth;
  AudioPlaybackRenderer renderer;
};

struct AlarmApp::State {
  Tab tab = Tab::Timer;
  uint16_t timerMinutes = 10;
  unsigned long timerDeadline = 0;
  unsigned long nextTimerRedrawAt = 0;
  bool timerRunning = false;
  bool timerFinished = false;
  uint8_t alarmHour = 7;
  uint8_t alarmMinute = 0;
  int64_t lastAlarmMinute = -1;
  bool alarmArmed = false;
  bool ringing = false;
  bool audioFailed = false;
  UiPressFeedback press;
};

bool AlarmApp::ensureState() {
  if (state != nullptr) {
    return true;
  }
  void *memory = heap_caps_malloc(sizeof(State),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) {
    memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_8BIT);
  }
  if (memory == nullptr) {
    return false;
  }
  state = new (memory) State();
  return true;
}

void AlarmApp::releaseState() {
  if (state == nullptr) {
    return;
  }
  state->~State();
  heap_caps_free(state);
  state = nullptr;
}

void AlarmApp::reset() {
  stopRinging();
  releaseState();
  ensureState();
}

void AlarmApp::onEnter() {
  if (ensureState()) {
    ensureAudio();
  }
}

void AlarmApp::release() {
  if (state != nullptr) {
    state->timerRunning = false;
    state->alarmArmed = false;
  }
  stopRinging();
  releaseAudio();
  releaseState();
}

bool AlarmApp::ensureAudio() {
  if (audio != nullptr) {
    return true;
  }
  void *memory = heap_caps_malloc(sizeof(AudioState),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) {
    memory = heap_caps_malloc(sizeof(AudioState), MALLOC_CAP_8BIT);
  }
  if (memory == nullptr) {
    if (state != nullptr) {
      state->audioFailed = true;
    }
    return false;
  }
  audio = new (memory) AudioState();
  if (state != nullptr) {
    state->audioFailed = false;
  }
  return true;
}

void AlarmApp::releaseAudio() {
  if (audio == nullptr) {
    return;
  }
  audio->renderer.stop();
  audio->~AudioState();
  heap_caps_free(audio);
  audio = nullptr;
}

bool AlarmApp::startChime() {
  if (!ensureState() || !ensureAudio()) {
    return false;
  }

  audio->renderer.stop();
  setChimeEvent(audio->events[0], 0, 72, 108, true);
  setChimeEvent(audio->events[1], 180, 72, 0, false);
  setChimeEvent(audio->events[2], 260, 76, 108, true);
  setChimeEvent(audio->events[3], 440, 76, 0, false);
  setChimeEvent(audio->events[4], 520, 79, 118, true);
  setChimeEvent(audio->events[5], 980, 79, 0, false);

  audio->sequence = remidi_sequence{};
  audio->sequence.events = audio->events;
  audio->sequence.eventCount = sizeof(audio->events) / sizeof(audio->events[0]);
  audio->sequence.info.durationMs = 1150;
  audio->sequence.info.eventCount = audio->sequence.eventCount;
  audio->sequence.info.noteOnCount = 3;
  if (!audio->synth.begin(&audio->sequence, kSampleRate)) {
    state->audioFailed = true;
    return false;
  }

  AudioPlaybackStream stream;
  stream.user = &audio->synth;
  stream.fillPcm16 = remidiSynthFillPcm16;
  AudioPlaybackConfig config;
  config.sampleRate = kSampleRate;
  config.chunkFrames = 256;
  config.volumePercent = 100;
  config.playWhenMutedAtMinimumVolume = true;
  char error[32] = {};
  if (!audio->renderer.begin(stream, config, error, sizeof(error))) {
    state->audioFailed = true;
    return false;
  }
  state->ringing = true;
  state->audioFailed = false;
  return true;
}

void AlarmApp::stopRinging() {
  if (audio != nullptr) {
    audio->renderer.stop();
  }
  if (state != nullptr) {
    state->ringing = false;
  }
}

void AlarmApp::adjustTimer(int16_t delta) {
  int32_t next = static_cast<int32_t>(state->timerMinutes) + delta;
  if (next < kTimerMinimumMinutes) {
    next = kTimerMinimumMinutes;
  }
  if (next > kTimerMaximumMinutes) {
    next = kTimerMaximumMinutes;
  }
  state->timerMinutes = static_cast<uint16_t>(next);
  state->timerFinished = false;
}

void AlarmApp::adjustAlarmHour(int8_t delta) {
  int next = static_cast<int>(state->alarmHour) + delta;
  if (next < 0) {
    next += 24;
  }
  if (next >= 24) {
    next -= 24;
  }
  state->alarmHour = static_cast<uint8_t>(next);
  state->lastAlarmMinute = -1;
}

void AlarmApp::adjustAlarmMinute(int8_t delta) {
  int next = static_cast<int>(state->alarmMinute) + delta;
  if (next < 0) {
    next += 60;
  }
  if (next >= 60) {
    next -= 60;
  }
  state->alarmMinute = static_cast<uint8_t>(next);
  state->lastAlarmMinute = -1;
}

void AlarmApp::startTimer() {
  state->timerDeadline =
      millis() + static_cast<unsigned long>(state->timerMinutes) * 60000UL;
  state->nextTimerRedrawAt = 0;
  state->timerRunning = true;
  state->timerFinished = false;
}

void AlarmApp::stopTimer() {
  state->timerRunning = false;
  state->nextTimerRedrawAt = 0;
}

void AlarmApp::draw(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  if (state == nullptr) {
    drawCenteredText(gfx, "NO MEMORY", 100, 2);
    return;
  }
  SelectButtonGroup(kTabOptions,
                    sizeof(kTabOptions) / sizeof(kTabOptions[0]))
      .draw(gfx, state->tab == Tab::Timer ? kTabTimer : kTabAlarm,
            &state->press);
  if (state->tab == Tab::Timer) {
    drawTimer(gfx);
  } else {
    drawAlarm(gfx);
  }
}

void AlarmApp::drawTimer(Adafruit_GFX &gfx) {
  if (!state->timerRunning) {
    uiDrawButton(gfx, kPresetOneButton, "1 min",
                 state->timerMinutes == 1 &&
                     state->press.isPressed(CONTROL_PRESET_ONE));
    uiDrawButton(gfx, kPresetTenButton, "10 min",
                 state->timerMinutes == 10 &&
                     state->press.isPressed(CONTROL_PRESET_TEN));
    uiDrawButton(gfx, kTimerMinusTenButton, "-10",
                 state->press.isPressed(CONTROL_TIMER_MINUS_TEN));
    uiDrawButton(gfx, kTimerMinusOneButton, "-1",
                 state->press.isPressed(CONTROL_TIMER_MINUS_ONE));
    uiDrawButton(gfx, kTimerPlusOneButton, "+1",
                 state->press.isPressed(CONTROL_TIMER_PLUS_ONE));
    uiDrawButton(gfx, kTimerPlusTenButton, "+10",
                 state->press.isPressed(CONTROL_TIMER_PLUS_TEN));
    char minutes[16];
    snprintf(minutes, sizeof(minutes), "%u min",
             static_cast<unsigned>(state->timerMinutes));
    drawCenteredText(gfx, minutes, 120, 2);
    uiDrawButton(gfx, kTimerStartButton,
                 state->timerFinished ? "START AGAIN" : "START",
                 state->press.isPressed(CONTROL_TIMER_START));
  } else {
    const uint16_t seconds = remainingSeconds(millis(), state->timerDeadline);
    char timeText[8];
    snprintf(timeText, sizeof(timeText), "%02u:%02u",
             static_cast<unsigned>(seconds / 60),
             static_cast<unsigned>(seconds % 60));
    drawCenteredText(gfx, timeText, 88, 4);
    uiDrawButton(gfx, kTimerStopButton, "STOP",
                 state->press.isPressed(CONTROL_TIMER_STOP));
    uiDrawButton(gfx, kTimerResetButton, "RESET",
                 state->press.isPressed(CONTROL_TIMER_RESET));
  }
  if (state->ringing) {
    drawCenteredText(gfx, "RINGING", 190);
  } else if (state->audioFailed) {
    drawCenteredText(gfx, "AUDIO ERROR", 190);
  }
}

void AlarmApp::drawAlarm(Adafruit_GFX &gfx) {
  char alarmText[8];
  snprintf(alarmText, sizeof(alarmText), "%02u:%02u",
           static_cast<unsigned>(state->alarmHour),
           static_cast<unsigned>(state->alarmMinute));
  drawCenteredText(gfx, alarmText, 40, 4);
  uiDrawButton(gfx, kAlarmHourDownButton, "-H",
               state->press.isPressed(CONTROL_ALARM_HOUR_DOWN));
  uiDrawButton(gfx, kAlarmHourUpButton, "+H",
               state->press.isPressed(CONTROL_ALARM_HOUR_UP));
  uiDrawButton(gfx, kAlarmMinuteDownButton, "-M",
               state->press.isPressed(CONTROL_ALARM_MINUTE_DOWN));
  uiDrawButton(gfx, kAlarmMinuteUpButton, "+M",
               state->press.isPressed(CONTROL_ALARM_MINUTE_UP));
  uiDrawButton(gfx, kAlarmToggleButton,
               state->ringing ? "STOP"
                               : (state->alarmArmed ? "OFF" : "ON"),
               state->press.isPressed(CONTROL_ALARM_TOGGLE));
  if (!deviceClock.isSet()) {
    drawCenteredText(gfx, "CLOCK NOT SET", 188);
  } else if (state->ringing) {
    drawCenteredText(gfx, "RINGING", 188);
  } else if (state->audioFailed) {
    drawCenteredText(gfx, "AUDIO ERROR", 188);
  }
}

bool AlarmApp::handleTouch(const TouchPoint &point) {
  if (!ensureState()) {
    return false;
  }
  SelectButtonGroup tabs(kTabOptions, sizeof(kTabOptions) / sizeof(kTabOptions[0]));
  int8_t nextTab = -1;
  if (tabs.hitTest(point, nextTab)) {
    state->tab = nextTab == kTabAlarm ? Tab::Alarm : Tab::Timer;
    state->press.press(nextTab == kTabAlarm ? CONTROL_TAB_ALARM
                                             : CONTROL_TAB_TIMER);
    return true;
  }

  if (state->tab == Tab::Timer) {
    if (state->timerRunning) {
      if (uiTouchContains(kTimerStopButton, point)) {
        state->press.press(CONTROL_TIMER_STOP);
        stopTimer();
        stopRinging();
        return true;
      }
      if (uiTouchContains(kTimerResetButton, point)) {
        state->press.press(CONTROL_TIMER_RESET);
        stopTimer();
        state->timerFinished = false;
        return true;
      }
      return false;
    }
    if (uiTouchContains(kPresetOneButton, point)) {
      state->press.press(CONTROL_PRESET_ONE);
      state->timerMinutes = 1;
      state->timerFinished = false;
      return true;
    }
    if (uiTouchContains(kPresetTenButton, point)) {
      state->press.press(CONTROL_PRESET_TEN);
      state->timerMinutes = 10;
      state->timerFinished = false;
      return true;
    }
    if (uiTouchContains(kTimerMinusTenButton, point)) {
      state->press.press(CONTROL_TIMER_MINUS_TEN);
      adjustTimer(-10);
      return true;
    }
    if (uiTouchContains(kTimerMinusOneButton, point)) {
      state->press.press(CONTROL_TIMER_MINUS_ONE);
      adjustTimer(-1);
      return true;
    }
    if (uiTouchContains(kTimerPlusOneButton, point)) {
      state->press.press(CONTROL_TIMER_PLUS_ONE);
      adjustTimer(1);
      return true;
    }
    if (uiTouchContains(kTimerPlusTenButton, point)) {
      state->press.press(CONTROL_TIMER_PLUS_TEN);
      adjustTimer(10);
      return true;
    }
    if (uiTouchContains(kTimerStartButton, point)) {
      state->press.press(CONTROL_TIMER_START);
      startTimer();
      return true;
    }
    return false;
  }

  if (uiTouchContains(kAlarmHourDownButton, point)) {
    state->press.press(CONTROL_ALARM_HOUR_DOWN);
    adjustAlarmHour(-1);
    return true;
  }
  if (uiTouchContains(kAlarmHourUpButton, point)) {
    state->press.press(CONTROL_ALARM_HOUR_UP);
    adjustAlarmHour(1);
    return true;
  }
  if (uiTouchContains(kAlarmMinuteDownButton, point)) {
    state->press.press(CONTROL_ALARM_MINUTE_DOWN);
    adjustAlarmMinute(-1);
    return true;
  }
  if (uiTouchContains(kAlarmMinuteUpButton, point)) {
    state->press.press(CONTROL_ALARM_MINUTE_UP);
    adjustAlarmMinute(1);
    return true;
  }
  if (uiTouchContains(kAlarmToggleButton, point)) {
    state->press.press(CONTROL_ALARM_TOGGLE);
    if (state->ringing) {
      stopRinging();
    } else {
      state->alarmArmed = !state->alarmArmed;
      state->lastAlarmMinute = -1;
    }
    return true;
  }
  return false;
}

bool AlarmApp::handleMenuButton() {
  if (state == nullptr || !state->ringing) {
    return false;
  }
  stopRinging();
  return true;
}

bool AlarmApp::handlePowerButton() {
  if (!ensureState()) {
    return false;
  }
  if (state->ringing) {
    stopRinging();
  } else {
    state->tab = state->tab == Tab::Timer ? Tab::Alarm : Tab::Timer;
  }
  return true;
}

bool AlarmApp::update() {
  if (state == nullptr) {
    return false;
  }
  bool changed = state->press.update();
  const unsigned long now = millis();
  if (state->timerRunning) {
    if (deadlineReached(now, state->timerDeadline)) {
      state->timerRunning = false;
      state->timerFinished = true;
      startChime();
      changed = true;
    } else if (state->nextTimerRedrawAt == 0 ||
               deadlineReached(now, state->nextTimerRedrawAt)) {
      state->nextTimerRedrawAt = now + kTimerRedrawMs;
      changed = true;
    }
  }

  if (state->alarmArmed) {
    int64_t localUnix = 0;
    if (deviceClock.snapshotLocalUnix(localUnix)) {
      int64_t daySeconds = localUnix % 86400;
      if (daySeconds < 0) {
        daySeconds += 86400;
      }
      const uint8_t hour = static_cast<uint8_t>(daySeconds / 3600);
      const uint8_t minute = static_cast<uint8_t>((daySeconds / 60) % 60);
      const int64_t minuteIndex = localUnix / 60;
      if (hour == state->alarmHour && minute == state->alarmMinute &&
          minuteIndex != state->lastAlarmMinute) {
        state->lastAlarmMinute = minuteIndex;
        startChime();
        changed = true;
      }
    }
  }

  if (state->ringing && audio != nullptr) {
    char error[32] = {};
    const bool pumpOk = audio->renderer.pump(error, sizeof(error));
    if (!pumpOk) {
      state->audioFailed = true;
      stopRinging();
      changed = true;
    } else if (!audio->renderer.active() && audio->renderer.finished()) {
      audio->renderer.stop();
      state->ringing = false;
      changed = true;
    }
  }
  return changed;
}

bool AlarmApp::hasActiveSession() const {
  return state != nullptr &&
         (state->timerRunning || state->alarmArmed || state->ringing);
}
