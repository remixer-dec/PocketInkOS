#ifndef AI_APP_H
#define AI_APP_H

#include "audio_capture.h"
#include "menu_button_consumer.h"
#include "qwerty_zoom_keyboard_component.h"
#include "t9_keyboard_component.h"
#include "touch_input.h"
#include <Adafruit_GFX.h>
#include <WiFiClientSecure.h>

class AiApp : public MenuButtonConsumer {
public:
  void reset();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool openKeyboardFromButton();
  bool handlePowerButton();
  bool handleMenuButton();
  bool handleMenuDoubleButton();
  bool handleMenuLongButton();
  bool hasActiveSession() const;

private:
  friend class AiStreamListener;

  enum State { STATE_IDLE, STATE_COMPOSING, STATE_RECORDING,
               STATE_CONNECTING, STATE_STREAMING, STATE_READY, STATE_FAILED };
  enum KeyboardMode { KEYBOARD_T9, KEYBOARD_QWERTY_ZOOM };
  enum AudioFormat { AUDIO_WAV, AUDIO_PCM16 };

  static const int INPUT_MAX = 96;
  static const int TRANSCRIPT_MAX = 4096;
  static const int STATUS_MAX = 64;
  static const int LINE_WIDTH = 30;
  static const int VISIBLE_LINES = 12;
  static const int MODEL_SELECTOR_X = 4;
  static const int MODEL_SELECTOR_Y = 4;
  static const int MODEL_SELECTOR_W = 192;
  static const int MODEL_SELECTOR_H = 20;
  static const int TRANSCRIPT_X = 4;
  static const int TRANSCRIPT_Y = 30;
  static const int TRANSCRIPT_W = 192;
  static const int TRANSCRIPT_H = 130;
  static const int CHAT_BUTTON_X = 28;
  static const int CHAT_BUTTON_Y = 166;
  static const int CHAT_BUTTON_W = 84;
  static const int CHAT_BUTTON_H = 24;
  static const uint32_t AUDIO_CAPTURE_MS = 10000;
  static const unsigned long STREAM_TIMEOUT_MS = 30000;

  State state = STATE_IDLE;
  KeyboardMode keyboardMode = KEYBOARD_T9;
  bool keyboardOpen = false;
  bool followTail = true;
  bool responseHeaderWritten = false;
  bool thinkingHeaderWritten = false;
  bool requestSent = false;
  bool requestComplete = false;
  bool responseChunked = false;
  bool pendingAudio = false;
  AudioFormat pendingAudioFormat = AUDIO_WAV;
  AudioCaptureResult capturedAudio;
  AudioCapture audioCapture;
  int modelIndex = 0;
  uint16_t requestPort = 443;
  int scrollOffset = 0;
  int maxScrollOffset = 0;
  int chunkRemaining = 0;
  char transcript[TRANSCRIPT_MAX] = {0};
  int transcriptLen = 0;
  char status[STATUS_MAX] = {0};
  char requestHost[96] = {0};
  char requestPath[128] = {0};
  char rxLine[768] = {0};
  char chunkLine[16] = {0};
  char pendingPrompt[INPUT_MAX] = {0};
  int rxLineLen = 0;
  int chunkLineLen = 0;
  String inputText;
  unsigned long requestStartedAt = 0;
  WiFiClientSecure client;
  T9KeyboardComponent inputKeyboard;
  QwertyZoomKeyboardComponent zoomKeyboard;

  void openKeyboard();
  void submitPrompt();
  void submitVoicePrompt();
  bool startVoiceRecording();
  bool stopVoiceRecording();
  bool startRequest();
  bool pumpStream();
  bool readLine(char *out, int outSize, unsigned long timeoutMs);
  bool readBodyByte(char &out);
  bool buildRequestBody(char *out, int outSize, const char *escapedSystem,
                        const char *escapedPrompt,
                        const char *audioBase64) const;
  bool sendRequest(const char *endpoint, const char *escapedSystem,
                   const char *escapedPrompt);
  bool writeRequestHeader(const char *endpoint, int bodyLen);
  bool writeAll(const char *text);
  bool writeAll(const uint8_t *data, size_t length);
  bool writeBase64(const uint8_t *data, size_t length);
  int requestBodyLength(const char *escapedSystem,
                        const char *escapedPrompt) const;
  bool parseSseEvent(const char *payload);
  bool appendTranscript(const char *text);
  bool appendReasoning(const char *text);
  bool appendResponse(const char *text);
  bool appendTranscriptChar(char c);
  void setStatus(const char *text);
  const char *currentModel() const;
  bool hasConfig() const;
  bool handleKeyboardTouch(const TouchPoint &point);
  void drawMain(Adafruit_GFX &gfx);
  void drawKeyboard(Adafruit_GFX &gfx);
  void drawSelector(Adafruit_GFX &gfx);
  void drawTranscript(Adafruit_GFX &gfx);
  void drawFooterButtons(Adafruit_GFX &gfx);
  void drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                        uint8_t size = 1) const;
  void drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y) const;
  void toggleModel();
  void beginVoiceCapture();
  const char *audioFormatLabel() const;
  void releaseCapturedAudio();
  void updateScrollMetrics();
  static int base64Length(size_t rawLength);
  static int measureWrappedLines(const char *text, int maxChars);
  static int drawWrappedText(Adafruit_GFX &gfx, const char *text, int16_t x,
                             int16_t y, int maxChars, int maxLines,
                             int skipLines, bool draw);
  static const char *skipSpaces(const char *text);
  static void copyText(char *out, int outSize, const char *value);
  static bool startsWith(const char *text, const char *prefix);
  static bool containsIgnoreCase(const char *text, const char *needle);
  static int parseChunkSize(const char *text);
  static bool appendEscapedJson(char *out, int outSize, int &length,
                                const char *value);
  static bool appendLiteral(char *out, int outSize, int &length,
                            const char *value);
  static bool appendChar(char *out, int outSize, int &length, char value);
  static void trimTrailingNewline(char *text);
};

#endif
