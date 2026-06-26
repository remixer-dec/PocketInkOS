#include "apps/tts_app.h"

#include "sys/inactivity_sleep_guard.h"
#include "sys/sd_storage.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define TTS_APP_HAS_ROM_PRINTF 1
#else
#define TTS_APP_HAS_ROM_PRINTF 0
#endif

namespace {

const UiRect kInputButton = {8, 148, 58, 24};
const UiRect kSpeakButton = {72, 148, 58, 24};
const UiRect kPlaybackButton = {136, 148, 56, 24};
const UiRect kTextBox = {8, 38, 184, 70};
const UiRect kBackendButton = {8, 124, 58, 20};
const UiRect kIterButton = {72, 124, 58, 20};
const uint32_t kTaskStackBytes = 16384;
const UBaseType_t kTaskPriority = 1;
const BaseType_t kTaskCore = 1;
const bool kAutoPlaybackAfterSynthesis = true;

void ttsAppLog(const char *format, ...) {
  char line[192];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
#if TTS_APP_HAS_ROM_PRINTF
  esp_rom_printf("%s", line);
#else
  Serial.print(line);
#endif
}

unsigned currentTaskStackHighWaterMark() {
#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && INCLUDE_uxTaskGetStackHighWaterMark
  return static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr));
#else
  return 0;
#endif
}

const char *stateName(TtsApp::State state) {
  switch (state) {
  case TtsApp::State::Idle:
    return "idle";
  case TtsApp::State::Synthesizing:
    return "synth";
  case TtsApp::State::Ready:
    return "ready";
  case TtsApp::State::Playing:
    return "playing";
  case TtsApp::State::Error:
    return "error";
  }
  return "?";
}

void drawCentered(Adafruit_GFX &gfx, const char *text, int y, uint8_t size) {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.setTextSize(size);
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((200 - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void drawWrappedPreview(Adafruit_GFX &gfx, const String &text) {
  gfx.drawRect(kTextBox.x, kTextBox.y, kTextBox.w, kTextBox.h, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  const char *raw = text.c_str();
  if (raw[0] == '\0') {
    gfx.setCursor(kTextBox.x + 8, kTextBox.y + 28);
    gfx.print("Tap INPUT to enter speech text");
    return;
  }

  char line[28] = {};
  size_t lineLen = 0;
  int y = kTextBox.y + 8;
  for (size_t i = 0; raw[i] != '\0' && y < kTextBox.y + kTextBox.h - 6; i++) {
    char c = raw[i];
    if (c == '\n') {
      line[lineLen] = '\0';
      gfx.setCursor(kTextBox.x + 6, y);
      gfx.print(line);
      lineLen = 0;
      y += 11;
      continue;
    }
    if (lineLen + 1 >= sizeof(line)) {
      line[lineLen] = '\0';
      gfx.setCursor(kTextBox.x + 6, y);
      gfx.print(line);
      lineLen = 0;
      y += 11;
    }
    line[lineLen++] = c;
  }
  if (lineLen > 0 && y < kTextBox.y + kTextBox.h) {
    line[lineLen] = '\0';
    gfx.setCursor(kTextBox.x + 6, y);
    gfx.print(line);
  }
}

} // namespace

void TtsApp::reset() {
  ttsAppLog("[tts-app] reset running=%u state=%s\n", taskRunning ? 1U : 0U,
            stateName(state));
  stopAudio();
  inputText = "hey";
  numberText = "";
  keyboardOpen = false;
  keyboardMode = KEYBOARD_T9;
  state = State::Idle;
  setStatus("Enter text");
  closeKeyboard();
  if (!taskRunning) {
    pcm.clear();
  }
  taskDone = false;
}

void TtsApp::draw(Adafruit_GFX &gfx) {
  if (keyboardOpen) {
    drawKeyboard(gfx);
    return;
  }
  drawMain(gfx);
}

bool TtsApp::update() {
  bool dirty = false;
  if (keyboardOpen && keyboardMode == KEYBOARD_T9 && inputKeyboard.update()) {
    dirty = true;
  }
  if (taskDone) {
    taskDone = false;
    finishSynthesis();
    dirty = true;
  }
  if (state == State::Playing) {
    char error[32];
    if (!renderer.pump(error, sizeof(error)) || renderer.finished()) {
      ttsAppLog("[tts-app] playback finished peak=%u chunks=%lu\n",
                static_cast<unsigned>(renderer.lastPeak()),
                static_cast<unsigned long>(renderer.chunksWritten()));
      renderer.stop();
      state = State::Ready;
      setStatus("Ready");
      dirty = true;
    }
  }
  return dirty;
}

bool TtsApp::handleTouch(const TouchPoint &point) {
  if (keyboardOpen) {
    return handleKeyboardTouch(point);
  }
  if (uiContains(kInputButton, point)) {
    openKeyboard();
    return true;
  }
  if (uiContains(kBackendButton, point)) {
    toggleVocoderBackend();
    return true;
  }
  if (uiContains(kIterButton, point)) {
    openIterationKeyboard();
    return true;
  }
  if (uiContains(kSpeakButton, point)) {
    return startSynthesis();
  }
  if (uiContains(kPlaybackButton, point)) {
    if (state == State::Playing) {
      stopAudio();
      state = State::Ready;
      setStatus("Ready");
      return true;
    }
    if (pcm.frameCount() > 0) {
      return startPlayback();
    }
    return true;
  }
  return false;
}

bool TtsApp::handleMenuButton() {
  if (!keyboardOpen) {
    return false;
  }
  if (keyboardMode == KEYBOARD_NUMBERS) {
    return true;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.toggleCaps();
  } else {
    inputKeyboard.toggleCaps();
  }
  return true;
}

bool TtsApp::handleMenuDoubleButton() {
  if (!keyboardOpen) {
    return false;
  }
  if (keyboardMode == KEYBOARD_NUMBERS) {
    return true;
  }
  keyboardMode = keyboardMode == KEYBOARD_QWERTY_ZOOM ? KEYBOARD_T9
                                                      : KEYBOARD_QWERTY_ZOOM;
  ttsAppLog("[tts-app] keyboard mode=%u\n", static_cast<unsigned>(keyboardMode));
  return true;
}

bool TtsApp::handleMenuLongButton() {
  if (!keyboardOpen) {
    return false;
  }
  closeKeyboard();
  return true;
}

bool TtsApp::handlePowerButton() {
  if (state == State::Playing) {
    stopAudio();
    state = State::Ready;
    setStatus("Ready");
    return true;
  }
  if (taskRunning || state == State::Synthesizing) {
    return true;
  }
  if (state == State::Ready) {
    return startPlayback();
  }
  if (inputText.length() == 0) {
    openKeyboard();
    return true;
  }
  return startSynthesis();
}

bool TtsApp::hasActiveSession() const {
  return keyboardOpen || taskRunning || state == State::Playing ||
         inputText.length() > 0;
}

void TtsApp::stopAudio() {
  if (renderer.active() || state == State::Playing) {
    ttsAppLog("[tts-app] stop audio peak=%u chunks=%lu\n",
              static_cast<unsigned>(renderer.lastPeak()),
              static_cast<unsigned long>(renderer.chunksWritten()));
  }
  renderer.stop();
  playbackCursor = 0;
}

void TtsApp::openKeyboard() {
  if (state == State::Synthesizing) {
    return;
  }
  keyboardOpen = true;
  keyboardMode = KEYBOARD_T9;
  textBeforeKeyboard = inputText;
  inputKeyboard.setLayout(T9KeyboardComponent::Layout::Text);
  setActiveMenuButtonConsumer(this);
  ttsAppLog("[tts-app] keyboard open text_len=%u\n",
            static_cast<unsigned>(inputText.length()));
}

void TtsApp::openIterationKeyboard() {
  if (state == State::Synthesizing) {
    return;
  }
  char value[4];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(griffinLimIterations));
  numberText = value;
  keyboardOpen = true;
  keyboardMode = KEYBOARD_NUMBERS;
  inputKeyboard.setLayout(T9KeyboardComponent::Layout::Numbers);
  setActiveMenuButtonConsumer(this);
  ttsAppLog("[tts-app] gl iters edit value=%u\n",
            static_cast<unsigned>(griffinLimIterations));
}

void TtsApp::closeKeyboard() {
  if (keyboardOpen) {
    ttsAppLog("[tts-app] keyboard close text_len=%u\n",
              static_cast<unsigned>(inputText.length()));
  }
  keyboardOpen = false;
  inputKeyboard.setLayout(T9KeyboardComponent::Layout::Text);
  clearActiveMenuButtonConsumer(this);
}

void TtsApp::applyIterationText() {
  int value = atoi(numberText.c_str());
  if (value < 0) {
    value = 0;
  } else if (value > 16) {
    value = 16;
  }
  griffinLimIterations = static_cast<uint8_t>(value);
  ttsAppLog("[tts-app] gl iters set=%u\n",
            static_cast<unsigned>(griffinLimIterations));
}

void TtsApp::toggleVocoderBackend() {
  if (state == State::Synthesizing) {
    return;
  }
  stopAudio();
  pcm.clear();
  vocoderBackend = vocoderBackend == VocoderBackend::Neural
                       ? VocoderBackend::GriffinLim
                       : VocoderBackend::Neural;
  state = State::Idle;
  setStatus(vocoderBackend == VocoderBackend::GriffinLim ? "Backend: Griffin-Lim"
                                                          : "Backend: Neural");
  ttsAppLog("[tts-app] backend=%s gl_iters=%u\n", vocoderBackendName(),
            static_cast<unsigned>(griffinLimIterations));
}

const char *TtsApp::vocoderBackendName() const {
  return vocoderBackend == VocoderBackend::GriffinLim ? "griffin_lim" : "neural";
}

void TtsApp::drawKeyboard(Adafruit_GFX &gfx) {
  if (keyboardMode == KEYBOARD_NUMBERS) {
    inputKeyboard.draw(gfx, numberText, 2);
    return;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.draw(gfx, inputText, MAX_TEXT_LENGTH);
    return;
  }
  inputKeyboard.draw(gfx, inputText, MAX_TEXT_LENGTH);
}

void TtsApp::drawMain(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  drawCentered(gfx, "TTS", 8, 2);
  gfx.setTextSize(1);
  gfx.setCursor(10, 28);
  gfx.print(status);
  drawWrappedPreview(gfx, inputText);

  char meta[48];
  if (pcm.frameCount() > 0) {
    snprintf(meta, sizeof(meta), "%lu samples @ %luHz",
             static_cast<unsigned long>(pcm.frameCount()),
             static_cast<unsigned long>(pcm.sampleRateHz()));
  } else {
    snprintf(meta, sizeof(meta), "Models: /sdcard/tts");
  }
  gfx.setCursor(10, 110);
  gfx.print(meta);

  if (state == State::Playing) {
    snprintf(meta, sizeof(meta), "PCM %u  VOL %u%%",
             static_cast<unsigned>(renderer.lastPeak()),
             static_cast<unsigned>(volumePercent));
    gfx.setCursor(136, 130);
    gfx.print(meta);
  }

  char iterLabel[8];
  snprintf(iterLabel, sizeof(iterLabel), "ITR %u",
           static_cast<unsigned>(griffinLimIterations));
  uiDrawButton(gfx, kBackendButton,
               vocoderBackend == VocoderBackend::GriffinLim ? "GL" : "NEURAL",
               vocoderBackend == VocoderBackend::GriffinLim);
  uiDrawButton(gfx, kIterButton, iterLabel);

  uiDrawButton(gfx, kInputButton, "INPUT");
  uiDrawButton(gfx, kSpeakButton, "SPEAK", state == State::Synthesizing);
  if (state == State::Playing) {
    uiDrawButton(gfx, kPlaybackButton, "STOP", true);
  } else if (pcm.frameCount() > 0) {
    uiDrawButton(gfx, kPlaybackButton, "PLAY");
  }
}

bool TtsApp::handleKeyboardTouch(const TouchPoint &point) {
  if (keyboardMode == KEYBOARD_NUMBERS) {
    KeyboardEvent event = inputKeyboard.hitTest(point, numberText, 2);
    if (event.action == KEY_NONE) {
      return false;
    }
    if (event.action == KEY_OK) {
      applyIterationText();
      closeKeyboard();
      setStatus("Ready to speak");
    }
    return true;
  }

  KeyboardEvent event =
      keyboardMode == KEYBOARD_QWERTY_ZOOM
          ? zoomKeyboard.hitTest(point, inputText.length(), MAX_TEXT_LENGTH)
          : inputKeyboard.hitTest(point, inputText, MAX_TEXT_LENGTH);
  if (event.action == KEY_NONE) {
    return false;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    if (event.action == KEY_CHAR && inputText.length() < MAX_TEXT_LENGTH) {
      inputText += event.value;
    } else if (event.action == KEY_SPACE &&
               inputText.length() < MAX_TEXT_LENGTH) {
      inputText += ' ';
    } else if (event.action == KEY_BACKSPACE && inputText.length() > 0) {
      inputText.remove(inputText.length() - 1);
    }
  }
  if (event.action == KEY_OK) {
    if (inputText != textBeforeKeyboard) {
      stopAudio();
      pcm.clear();
      if (state != State::Synthesizing) {
        state = State::Idle;
      }
    }
    closeKeyboard();
    setStatus("Ready to speak");
  }
  return true;
}

bool TtsApp::startSynthesis() {
  if (taskRunning || state == State::Synthesizing) {
    ttsAppLog("[tts-app] synth ignored already running\n");
    return true;
  }
  if (!sdStorageMounted()) {
    state = State::Error;
    setStatus("SD card not mounted");
    ttsAppLog("[tts-app] synth blocked sd not mounted\n");
    return true;
  }
  if (inputText.length() == 0) {
    state = State::Error;
    setStatus("No text");
    ttsAppLog("[tts-app] synth blocked empty text\n");
    return true;
  }

  stopAudio();
  strncpy(taskText, inputText.c_str(), sizeof(taskText) - 1);
  taskText[sizeof(taskText) - 1] = '\0';
  lastResult = tts::SynthesisResult();
  taskDone = false;
  taskRunning = true;
  state = State::Synthesizing;
  setStatus("Synthesizing...");
  ttsAppLog("[tts-app] synth task create text_len=%u\n",
            static_cast<unsigned>(strlen(taskText)));

  if (taskHandle == nullptr) {
    TaskHandle_t created = nullptr;
    if (xTaskCreatePinnedToCore(synthesisTaskEntry, "tts-synth",
                                kTaskStackBytes, this, kTaskPriority, &created,
                                kTaskCore) != pdPASS) {
      taskRunning = false;
      taskWorkerIdle = false;
      state = State::Error;
      setStatus("Task create failed");
      ttsAppLog("[tts-app] synth task create failed\n");
      return true;
    }
    taskHandle = created;
    ttsAppLog("[tts-app] synth worker created stack_bytes=%u core=%d\n",
              static_cast<unsigned>(kTaskStackBytes),
              static_cast<int>(kTaskCore));
  } else {
    taskWorkerIdle = false;
    ttsAppLog("[tts-app] synth worker resume\n");
    vTaskResume(static_cast<TaskHandle_t>(taskHandle));
  }
  return true;
}

bool TtsApp::startPlayback() {
  if (taskRunning || pcm.frameCount() == 0) {
    return false;
  }
  stopAudio();
  playbackCursor = 0;

  AudioPlaybackStream stream;
  stream.user = this;
  stream.fillPcm16 = fillPlayback;

  AudioPlaybackConfig config;
  config.sampleRate = pcm.sampleRateHz();
  config.chunkFrames = 256;
  config.volumePercent = volumePercent;

  char error[32];
  ttsAppLog("[tts-app] playback begin frames=%u rate=%u\n",
            static_cast<unsigned>(pcm.frameCount()),
            static_cast<unsigned>(pcm.sampleRateHz()));
  if (!renderer.begin(stream, config, error, sizeof(error))) {
    state = State::Error;
    setStatus(error);
    ttsAppLog("[tts-app] playback begin failed error=\"%s\"\n", error);
    return true;
  }
  state = State::Playing;
  setStatus("Playing");
  return true;
}

void TtsApp::finishSynthesis() {
  if (lastResult.ok) {
    state = State::Ready;
    setStatus("Ready");
    ttsAppLog("[tts-app] synth ok frames=%u rate=%u elapsed_ms=%u\n",
              static_cast<unsigned>(lastResult.frames),
              static_cast<unsigned>(lastResult.sampleRateHz),
              static_cast<unsigned>(lastResult.elapsedMs));
    if (kAutoPlaybackAfterSynthesis) {
      startPlayback();
    }
    return;
  }
  state = State::Error;
  setStatus(lastResult.error[0] != '\0' ? lastResult.error : "Synthesis failed");
  ttsAppLog("[tts-app] synth failed error=\"%s\"\n", status);
}

void TtsApp::setStatus(const char *nextStatus) {
  strncpy(status, nextStatus != nullptr ? nextStatus : "", sizeof(status) - 1);
  status[sizeof(status) - 1] = '\0';
}

void TtsApp::synthesisTaskEntry(void *user) {
  TtsApp *app = static_cast<TtsApp *>(user);
  while (app != nullptr) {
    app->synthesisTask();
    app->taskWorkerIdle = true;
    ttsAppLog("[tts-app] synth worker suspend\n");
    vTaskSuspend(nullptr);
  }
  vTaskDelete(nullptr);
}

void TtsApp::synthesisTask() {
  ScopedInactivitySleepGuard sleepGuard;
  ttsAppLog("[tts-app] synth task stack_hwm_start=%u\n",
            currentTaskStackHighWaterMark());
  tts::ModelPaths paths;
  tts::defaultModelPaths(paths);
  tts::SynthesisOptions options;
  options.vocoderBackend = vocoderBackendName();
  options.griffinLimIterations = griffinLimIterations;
  lastResult = tts::synthesizeToPcm(taskText, paths, options, pcm);
  ttsAppLog("[tts-app] synth task stack_hwm_end=%u\n",
            currentTaskStackHighWaterMark());
  taskRunning = false;
  taskDone = true;
}

size_t TtsApp::fillPlayback(void *user, int16_t *stereoFrames,
                            size_t frameCount) {
  TtsApp *app = static_cast<TtsApp *>(user);
  if (app == nullptr || stereoFrames == nullptr || app->pcm.data() == nullptr) {
    return 0;
  }
  if (app->playbackCursor >= app->pcm.frameCount()) {
    return 0;
  }
  const size_t available = app->pcm.frameCount() - app->playbackCursor;
  const size_t frames = available < frameCount ? available : frameCount;
  const int16_t *src = app->pcm.data() + app->playbackCursor;
  for (size_t i = 0; i < frames; i++) {
    const int16_t sample = src[i];
    stereoFrames[i * 2] = sample;
    stereoFrames[i * 2 + 1] = sample;
  }
  app->playbackCursor += frames;
  return frames;
}
