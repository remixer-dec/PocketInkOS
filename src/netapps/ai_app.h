#ifndef AI_APP_H
#define AI_APP_H

#include "sys/audio/audio_capture.h"
#include "sys/agentfiesta/openai_transport.h"
#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"
#include "ui/components/t9_keyboard_component.h"
#include "sys/services/touch_input.h"
#include <Adafruit_GFX.h>

class AiApp {
public:
  void reset();
  void release();
  void draw(Adafruit_GFX &gfx);
  bool update();
  bool handleTouch(const TouchPoint &point);
  bool openKeyboardFromButton();
  bool handlePowerButton();
  bool handleMenuButton();
  bool handleMenuDoubleButton();
  bool handleMenuLongButton();
  void stopAudio();
  bool hasActiveSession() const;

private:
  friend class AiStreamListener;

  enum State { STATE_IDLE, STATE_COMPOSING, STATE_RECORDING,
               STATE_CONNECTING, STATE_STREAMING, STATE_READY, STATE_FAILED };
  enum KeyboardMode { KEYBOARD_T9, KEYBOARD_QWERTY_ZOOM };
  enum HistoryMode { HISTORY_SINGLE, HISTORY_CONVERSATION };
  enum VoiceMode { VOICE_CHAT, VOICE_TRANSCRIBE };
  enum MessageRole { MESSAGE_USER, MESSAGE_ASSISTANT, MESSAGE_NOTE };

  struct ChatMessage {
    MessageRole role = MESSAGE_USER;
    char *text = nullptr;
  };

  struct MarkdownLine {
    char text[48] = {};
    const char *svg = nullptr;
    size_t svgBytes = 0;
    uint32_t boldMask = 0;
    uint32_t italicMask = 0;
    uint8_t rows = 1;
    bool heading = false;
  };

  static const int INPUT_MAX = 96;
  static const int TRANSCRIPT_MAX = 4096;
  static const int RESPONSE_MAX = 2048;
  static const int RAW_RESPONSE_MAX = 2 * 1024 * 1024;
  static const int RX_LINE_MAX = 768;
  static const int HISTORY_MAX_MESSAGES = 12;
  static const int HISTORY_MAX_TEXT = 2048;
  static const int STATUS_MAX = 64;
  static const int LINE_WIDTH = 30;
  static const int VISIBLE_LINES = 15;
  static const int TRANSCRIPT_X = 4;
  static const int TRANSCRIPT_Y = 4;
  static const int TRANSCRIPT_W = 192;
  static const int TRANSCRIPT_H = 156;
  static const int CHAT_BUTTON_X = 4;
  static const int CHAT_BUTTON_Y = 166;
  static const int CHAT_BUTTON_W = 28;
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
  bool pendingAudio = false;
  bool responseTruncated = false;
  bool settingsOpen = false;
  bool transcriptionTurn = false;
  bool audioInputTurn = false;
  bool voiceOutput = false;
  bool voiceOutputTurn = false;
  bool voicePlaybackFailed = false;
  bool turnStored = false;
  AudioCaptureResult capturedAudio;
  AudioCapture audioCapture;
  int modelIndex = 0;
  int responseLen = 0;
  int rawResponseLen = 0;
  int rawResponseCapacity = 0;
  int settingsSelection = 0;
  int scrollOffset = 0;
  int maxScrollOffset = 0;
  int turnStartLine = 0;
  char *transcript = nullptr;
  char *response = nullptr;
  char *rxLine = nullptr;
  char *rawResponse = nullptr;
  char *inputTranscript = nullptr;
  ChatMessage *history = nullptr;
  int transcriptLen = 0;
  char status[STATUS_MAX] = {0};
  int rxLineLen = 0;
  String inputText;
  unsigned long requestStartedAt = 0;
  agentfiesta::OpenAiTransport transport;
  T9KeyboardComponent inputKeyboard;
  QwertyZoomKeyboardComponent zoomKeyboard;
  HistoryMode historyMode = HISTORY_SINGLE;
  VoiceMode voiceMode = VOICE_CHAT;
  int historyCount = 0;

  void openKeyboard();
  void submitPrompt();
  void submitVoicePrompt();
  bool startVoiceRecording();
  bool stopVoiceRecording();
  bool queueTurn(const char *prompt, bool voice);
  bool startRequest();
  bool pumpStream();
  bool sendRequest(const char *endpoint);
  bool writeAll(const char *text);
  bool writeAll(const uint8_t *data, size_t length);
  bool writeBase64(const uint8_t *data, size_t length);
  bool parseSseEvent(const char *payload);
  bool appendTranscript(const char *text);
  bool appendReasoning(const char *text);
  bool appendResponse(const char *text);
  bool appendResponseBuffer(const char *text);
  bool appendRawResponse(char value);
  bool parseCompleteResponse();
  bool captureInputTranscript(const char *text);
  void updateResponseScroll();
  bool appendTranscriptChar(char c);
  void setStatus(const char *text);
  const char *currentModel() const;
  bool hasConfig() const;
  bool handleKeyboardTouch(const TouchPoint &point);
  void drawMain(Adafruit_GFX &gfx);
  void drawKeyboard(Adafruit_GFX &gfx);
  void drawSettings(Adafruit_GFX &gfx);
  void drawNetworkFailure(Adafruit_GFX &gfx);
  void drawTranscript(Adafruit_GFX &gfx);
  void drawFooterButtons(Adafruit_GFX &gfx);
  void drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                        uint8_t size = 1) const;
  void drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y) const;
  void toggleModel();
  void toggleHistoryMode();
  void toggleVoiceMode();
  void toggleVoiceOutput();
  void loadSettings();
  void saveSettings();
  bool handleSettingsTouch(const TouchPoint &point);
  void resetTurnResponse();
  bool finishTurn();
  void rebuildTranscript(bool anchorLatestTurn = false,
                         bool preserveManualScroll = false);
  bool addHistoryMessage(MessageRole role, const char *text);
  void clearHistory();
  void pruneHistory();
  bool replaceLastVoiceMessage(const char *text);
  int historyMessageCount() const;
  const char *systemPrompt() const;
  bool writeJsonString(const char *text);
  int jsonStringLength(const char *text) const;
  bool writeMessages();
  int messagesLength() const;
  void releaseCapturedAudio();
  bool ensureTextStorage();
  bool ensureResponseStorage();
  bool ensureRxStorage();
  bool ensureRawResponseStorage(int needed = 1);
  bool ensureHistoryStorage();
  void releaseTextStorage();
  void releaseResponseStorage();
  void releaseRxStorage();
  void releaseRawResponseStorage();
  void releaseInputTranscript();
  void releaseHistoryStorage();
  void updateScrollMetrics();
  static int base64Length(size_t rawLength);
  static int measureWrappedLines(const char *text, int maxChars);
  static int drawWrappedText(Adafruit_GFX &gfx, const char *text, int16_t x,
                             int16_t y, int maxChars, int maxLines,
                             int skipLines, bool draw);
  static bool nextMarkdownLine(const char *&cursor, MarkdownLine &line,
                               int maxChars, bool &inBold, bool &inItalic);
  static void copyText(char *out, int outSize, const char *value);
  static bool startsWith(const char *text, const char *prefix);
  static void trimTrailingNewline(char *text);
};

/// Registers the AI app's SD-backed settings with the app config manager.
void aiAppConfigRegister();

#endif
