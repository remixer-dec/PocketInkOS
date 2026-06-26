#ifndef TTS_APP_H
#define TTS_APP_H

#include "sys/audio_playback_renderer.h"
#include "sys/touch_input.h"
#include "tts/inflect_tts_wrapper.h"
#include "ui/components/menu_button_consumer.h"
#include "ui/components/t9_keyboard_component.h"
#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

class TtsApp : public MenuButtonConsumer {
public:
  enum class State : uint8_t { Idle, Synthesizing, Ready, Playing, Error };

  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool handleMenuButton();
  bool handleMenuDoubleButton();
  bool handleMenuLongButton();
  bool handlePowerButton();
  bool hasActiveSession() const;
  void stopAudio();

private:
  enum KeyboardMode : uint8_t {
    KEYBOARD_T9,
    KEYBOARD_QWERTY_ZOOM,
    KEYBOARD_NUMBERS
  };
  enum class VocoderBackend : uint8_t { Neural, GriffinLim };

  static const int MAX_TEXT_LENGTH = 120;

  String inputText = "hey";
  String textBeforeKeyboard;
  String numberText;
  State state = State::Idle;
  bool keyboardOpen = false;
  KeyboardMode keyboardMode = KEYBOARD_T9;
  VocoderBackend vocoderBackend = VocoderBackend::Neural;
  uint8_t griffinLimIterations = 4;
  char status[96] = "Enter text";
  char taskText[MAX_TEXT_LENGTH + 1] = {};
  volatile bool taskRunning = false;
  volatile bool taskDone = false;
  volatile bool taskWorkerIdle = false;
  void *taskHandle = nullptr;

  tts::PcmBuffer pcm;
  tts::SynthesisResult lastResult;
  AudioPlaybackRenderer renderer;
  size_t playbackCursor = 0;
  uint8_t volumePercent = 85;

  T9KeyboardComponent inputKeyboard;
  QwertyZoomKeyboardComponent zoomKeyboard;

  void openKeyboard();
  void openIterationKeyboard();
  void closeKeyboard();
  void applyIterationText();
  void toggleVocoderBackend();
  const char *vocoderBackendName() const;
  void drawKeyboard(Adafruit_GFX &gfx);
  void drawMain(Adafruit_GFX &gfx);
  bool handleKeyboardTouch(const TouchPoint &point);
  bool startSynthesis();
  bool startPlayback();
  void finishSynthesis();
  void setStatus(const char *nextStatus);
  static void synthesisTaskEntry(void *user);
  void synthesisTask();
  static size_t fillPlayback(void *user, int16_t *stereoFrames,
                             size_t frameCount);
};

#endif
