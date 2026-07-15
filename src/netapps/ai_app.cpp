#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "netapps/ai_app.h"
#include "netapps/lightweight_json_parser.h"
#include "fs/providers/svg_file_viewer.h"
#include "sys/agentfiesta/agentfiesta.h"
#include "sys/agentfiesta/chat_completion.h"
#include "sys/app_config.h"
#include "sys/services/connectivity_service.h"
#include "secrets_config.h"
#include "sys/psram_allocator.h"
#include "sys/audio/wav_encoder.h"
#include "ui/icon_ascii_font.h"
#include "ui/components/select_button_component.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>

#ifndef SECRET_AI_API_BASE_URL
#define SECRET_AI_API_BASE_URL ""
#endif

#ifndef SECRET_AI_API_KEY
#define SECRET_AI_API_KEY ""
#endif

#ifndef SECRET_AI_MODEL_PRIMARY
#define SECRET_AI_MODEL_PRIMARY "model-a"
#endif

#ifndef SECRET_AI_MODEL_SECONDARY
#define SECRET_AI_MODEL_SECONDARY "model-b"
#endif

namespace {
constexpr int16_t kScreenWidth = 200;
constexpr int16_t kLineHeight = 10;

static const char *kTranscriptionSystemPrompt =
    "You are a concise assistant. Keep answers clear and structured. "
    "For audio input: first transcribe the audio, then write --- on its own "
    "line, then respond.";

struct BufferStream : public Stream {
  BufferStream(const char *source, int sourceLen)
      : data(source), length(sourceLen) {}

  int available() override { return position < length ? length - position : 0; }
  int read() override {
    if (position >= length) {
      return -1;
    }
    return static_cast<unsigned char>(data[position++]);
  }
  int peek() override {
    if (position >= length) {
      return -1;
    }
    return static_cast<unsigned char>(data[position]);
  }
  size_t write(uint8_t) override { return 0; }

  const char *data;
  int length;
  int position = 0;
};
} // namespace

class AiStreamListener : public JsonStreamListener {
public:
  explicit AiStreamListener(AiApp &target) : app(target) {}

  void onArrayStart(int depth, const char *key) override {
    if (strcmp(key, "input_audio_transcripts") == 0) {
      inInputTranscripts = true;
      inputTranscriptsDepth = depth;
      return;
    }
    if (strcmp(key, "choices") == 0) {
      inChoices = true;
      choicesDepth = depth;
    }
  }

  void onArrayEnd(int depth) override {
    if (depth == inputTranscriptsDepth) {
      inInputTranscripts = false;
      inputTranscriptsDepth = -1;
    }
    if (depth == choicesDepth) {
      inChoices = false;
      choicesDepth = -1;
    }
  }

  void onObjectStart(int depth, const char *key) override {
    if (inChoices && !inChoice && key[0] == '\0') {
      inChoice = true;
      choiceDepth = depth;
      return;
    }
    if (inChoice && strcmp(key, "delta") == 0) {
      inDelta = true;
      deltaDepth = depth;
      return;
    }
    if (inChoice && strcmp(key, "message") == 0) {
      inMessage = true;
      messageDepth = depth;
    }
  }

  void onObjectEnd(int depth) override {
    if (depth == deltaDepth) {
      inDelta = false;
      deltaDepth = -1;
    }
    if (depth == messageDepth) {
      inMessage = false;
      messageDepth = -1;
    }
    if (depth == choiceDepth) {
      inChoice = false;
      choiceDepth = -1;
    }
  }

  void onStringValue(int, const char *key, const char *value) override {
    if (inInputTranscripts && key[0] == '\0') {
      app.captureInputTranscript(value);
      return;
    }
    if (!inChoice) {
      if (strcmp(key, "message") == 0) {
        app.setStatus(value);
      }
      return;
    }
    if ((inDelta || inMessage) && strcmp(key, "content") == 0) {
      app.appendResponse(value);
      return;
    }
    if ((inDelta || inMessage) &&
        (strcmp(key, "reasoning") == 0 ||
         strcmp(key, "reasoning_content") == 0 ||
         strcmp(key, "thinking") == 0)) {
      app.appendReasoning(value);
    }
  }

private:
  AiApp &app;
  bool inChoices = false;
  bool inChoice = false;
  bool inDelta = false;
  bool inMessage = false;
  bool inInputTranscripts = false;
  int choicesDepth = -1;
  int choiceDepth = -1;
  int deltaDepth = -1;
  int messageDepth = -1;
  int inputTranscriptsDepth = -1;
};

namespace {
static const UiRect TRANSCRIPT = {4, 4, 192, 156};
static const UiRect CHAT_BUTTON = {4, 166, 28, 24};
static const UiRect SETTINGS_BUTTON = {88, 166, 24, 24};
static const UiRect RECORD_BUTTON = {168, 166, 28, 24};

static const SelectButtonOption MODEL_OPTIONS[] = {
    {{54, 22, 68, 26}, "PRIMARY", 0},
    {{126, 22, 70, 26}, "SECOND", 1},
};
static const SelectButtonOption HISTORY_OPTIONS[] = {
    {{54, 58, 68, 26}, "SINGLE", 0},
    {{126, 58, 70, 26}, "HISTORY", 1},
};
static const SelectButtonOption INPUT_OPTIONS[] = {
    {{54, 94, 68, 26}, "CHAT", 0},
    {{126, 94, 70, 26}, "TRANSCR.", 1},
};
static const SelectButtonOption OUTPUT_OPTIONS[] = {
    {{54, 130, 68, 26}, "TEXT", 0},
    {{126, 130, 70, 26}, "VOICE", 1},
};
static const UiRect SAVE_BUTTON = {4, 170, 192, 26};

enum AiSettingIndex : uint8_t {
  AI_SETTING_MODEL,
  AI_SETTING_HISTORY,
  AI_SETTING_INPUT,
  AI_SETTING_OUTPUT,
  AI_SETTING_COUNT,
};

const AppConfigOption kModelOptions[] = {
    {"Primary", "primary"},
    {"Secondary", "secondary"},
};
const AppConfigOption kHistoryOptions[] = {
    {"Single", "single"},
    {"Conversation", "conversation"},
};
const AppConfigOption kInputOptions[] = {
    {"Chat", "chat"},
    {"Transcribe", "transcribe"},
};
const AppConfigOption kOutputOptions[] = {
    {"Text", "text"},
    {"Voice", "voice"},
};

const AppConfigSetting kAiSettings[] = {
    {"model", "Model", AppConfigType::OneOf, "primary", kModelOptions, 2,
     0},
    {"history", "History", AppConfigType::OneOf, "single", kHistoryOptions,
     2, 0},
    {"input", "Input", AppConfigType::OneOf, "chat", kInputOptions, 2, 0},
    {"output", "Output", AppConfigType::OneOf, "text", kOutputOptions, 2,
     0},
};

const AppConfigRegistration kAiConfig = {
    "ai", "AI", kAiSettings, AI_SETTING_COUNT};

static const char *kWelcomeText =
    "AI ready.\n"
    "Tap the keyboard to compose a prompt.\n"
    "Tap the microphone or press PWR to record.\n";

static const char *kSystemPrompt =
    "You are a concise assistant. Keep answers clear and structured. For "
    "audio input, answer the user's request instead of only transcribing it.";

} // namespace

void aiAppConfigRegister() { appConfigRegister(kAiConfig); }

void AiApp::reset() {
  state = STATE_IDLE;
  keyboardMode = KEYBOARD_T9;
  keyboardOpen = false;
  settingsOpen = false;
  followTail = true;
  responseHeaderWritten = false;
  thinkingHeaderWritten = false;
  requestSent = false;
  requestComplete = false;
  pendingAudio = false;
  responseTruncated = false;
  transcriptionTurn = false;
  audioInputTurn = false;
  voiceOutput = false;
  voiceOutputTurn = false;
  voicePlaybackFailed = false;
  turnStored = false;
  releaseCapturedAudio();
  releaseRawResponseStorage();
  releaseInputTranscript();
  modelIndex = 0;
  historyMode = HISTORY_SINGLE;
  voiceMode = VOICE_CHAT;
  responseLen = 0;
  rawResponseLen = 0;
  rawResponseCapacity = 0;
  settingsSelection = 0;
  scrollOffset = 0;
  maxScrollOffset = 0;
  turnStartLine = 0;
  transcriptLen = 0;
  rxLineLen = 0;
  requestStartedAt = 0;
  status[0] = '\0';
  if (rxLine != nullptr) {
    rxLine[0] = '\0';
  }
  if (response != nullptr) {
    response[0] = '\0';
  }
  inputText = String();
  if (transcript != nullptr) {
    copyText(transcript, TRANSCRIPT_MAX, kWelcomeText);
    transcriptLen = strlen(transcript);
  }
  loadSettings();
  if (WiFi.status() != WL_CONNECTED) {
    state = STATE_FAILED;
    setStatus("WiFi not connected");
  }
}

void AiApp::release() {
  stopAudio();
  transport.stop();
  releaseHistoryStorage();
  releaseRxStorage();
  releaseRawResponseStorage();
  releaseInputTranscript();
  releaseResponseStorage();
  releaseTextStorage();
  reset();
}

bool AiApp::hasActiveSession() const {
  return state == STATE_RECORDING || state == STATE_CONNECTING ||
         state == STATE_STREAMING || state == STATE_READY ||
         state == STATE_FAILED;
}

void AiApp::draw(Adafruit_GFX &gfx) {
  if (keyboardOpen) {
    drawKeyboard(gfx);
    return;
  }
  if (settingsOpen) {
    drawSettings(gfx);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    drawNetworkFailure(gfx);
    return;
  }
  drawMain(gfx);
}

bool AiApp::update() {
  agentfiesta::updateVoiceOutput();
  if (keyboardOpen && keyboardMode == KEYBOARD_T9) {
    return inputKeyboard.update();
  }

  if (state == STATE_RECORDING) {
    char error[STATUS_MAX] = "";
    if (!audioCapture.pumpPcm16(capturedAudio, error, sizeof(error))) {
      pendingAudio = false;
      state = STATE_FAILED;
      setStatus(error[0] ? error : "Audio failed");
      return true;
    }
    if (capturedAudio.length >= wav::kPcmHeaderSize +
        static_cast<size_t>(capturedAudio.sampleRate) * AUDIO_CAPTURE_MS /
            1000U * sizeof(int16_t)) {
      return stopVoiceRecording();
    }
    setStatus("Recording PWR stop");
    return true;
  }

  if (state == STATE_CONNECTING || state == STATE_STREAMING) {
    return pumpStream();
  }
  return false;
}

bool AiApp::handleTouch(const TouchPoint &point) {
  if (keyboardOpen) {
    return handleKeyboardTouch(point);
  }
  if (settingsOpen) {
    return handleSettingsTouch(point);
  }

  if (WiFi.status() != WL_CONNECTED) {
    wifiTurnOff();
    setStatus("WiFi off");
    return true;
  }

  if (uiContains(CHAT_BUTTON, point)) {
    if (state == STATE_CONNECTING || state == STATE_STREAMING) {
      setStatus("Wait for the current response");
      return true;
    }
    openKeyboard();
    return true;
  }
  if (uiContains(SETTINGS_BUTTON, point)) {
    if (state == STATE_CONNECTING || state == STATE_STREAMING ||
        state == STATE_RECORDING) {
      setStatus("Wait for the current request");
      return true;
    }
    settingsOpen = true;
    return true;
  }
  if (uiContains(RECORD_BUTTON, point)) {
    return handlePowerButton();
  }
  if (uiContains(TRANSCRIPT, point)) {
    followTail = false;
    if (point.y < TRANSCRIPT.y + TRANSCRIPT.h / 2) {
      if (scrollOffset < maxScrollOffset) {
        scrollOffset++;
      }
    } else if (scrollOffset > 0) {
      scrollOffset--;
    }
    return true;
  }
  return false;
}

bool AiApp::openKeyboardFromButton() {
  if (keyboardOpen) {
    keyboardOpen = false;
    return true;
  }
  if (settingsOpen) {
    settingsOpen = false;
    return true;
  }
  if (state == STATE_CONNECTING || state == STATE_STREAMING) {
    setStatus("Wait for the current response");
    return true;
  }
  openKeyboard();
  return true;
}

bool AiApp::handlePowerButton() {
  if (settingsOpen) {
    settingsOpen = false;
    return true;
  }
  if (keyboardOpen) {
    keyboardOpen = false;
    return true;
  }
  if (state == STATE_RECORDING) {
    return stopVoiceRecording();
  }
  if (WiFi.status() != WL_CONNECTED) {
    wifiTurnOff();
    setStatus("WiFi off");
    return true;
  }
  if (state == STATE_CONNECTING || state == STATE_STREAMING) {
    setStatus("Wait for the current response");
    return true;
  }
  return startVoiceRecording();
}

bool AiApp::handleMenuButton() {
  if (!keyboardOpen) {
    if (settingsOpen) {
      settingsSelection = (settingsSelection + 1) % 5;
      return true;
    }
    return false;
  }
  if (inputText.length() == 0) {
    keyboardOpen = false;
    return true;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.toggleCaps();
  } else {
    inputKeyboard.toggleCaps();
  }
  return true;
}

bool AiApp::handleMenuDoubleButton() {
  if (settingsOpen) {
    if (settingsSelection == 0) {
      toggleModel();
    } else if (settingsSelection == 1) {
      toggleHistoryMode();
    } else if (settingsSelection == 2) {
      toggleVoiceMode();
    } else if (settingsSelection == 3) {
      toggleVoiceOutput();
    } else {
      saveSettings();
    }
    return true;
  }
  if (!keyboardOpen) {
    return false;
  }
  keyboardMode = keyboardMode == KEYBOARD_QWERTY_ZOOM ? KEYBOARD_T9
                                                       : KEYBOARD_QWERTY_ZOOM;
  return true;
}

bool AiApp::handleMenuLongButton() {
  if (settingsOpen) {
    settingsOpen = false;
    return true;
  }
  if (!keyboardOpen) {
    return false;
  }
  keyboardOpen = false;
  return true;
}

void AiApp::stopAudio() {
  agentfiesta::stopVoiceOutput();
  if (state == STATE_RECORDING) {
    state = STATE_FAILED;
    setStatus("Recording stopped");
  }
  pendingAudio = false;
  releaseCapturedAudio();
}

void AiApp::openKeyboard() {
  pendingAudio = false;
  releaseCapturedAudio();
  inputText = "";
  keyboardMode = KEYBOARD_T9;
  keyboardOpen = true;
}

void AiApp::submitPrompt() {
  if (state == STATE_CONNECTING || state == STATE_STREAMING) {
    setStatus("Wait for the current response");
    return;
  }
  char prompt[INPUT_MAX];
  copyText(prompt, sizeof(prompt), inputText.c_str());
  trimTrailingNewline(prompt);
  if (prompt[0] == '\0') {
    setStatus("Enter a prompt first");
    return;
  }
  releaseCapturedAudio();
  pendingAudio = false;
  queueTurn(prompt, false);
}

void AiApp::submitVoicePrompt() {
  if (capturedAudio.data == nullptr || capturedAudio.length == 0) {
    pendingAudio = false;
    state = STATE_FAILED;
    setStatus("No audio captured");
    return;
  }
  if (!queueTurn("[voice]", true)) {
    state = STATE_FAILED;
  }
}

bool AiApp::queueTurn(const char *prompt, bool voice) {
  if (prompt == nullptr || prompt[0] == '\0' || !ensureTextStorage()) {
    setStatus("No AI mem");
    return false;
  }
  if (historyMode == HISTORY_SINGLE) {
    clearHistory();
  }
  if (!addHistoryMessage(MESSAGE_USER, prompt)) {
    setStatus("No AI history mem");
    return false;
  }
  agentfiesta::stopVoiceOutput();

  if (historyMode == HISTORY_SINGLE) {
    transcriptLen = 0;
    transcript[0] = '\0';
  } else if (transcriptLen > 0 && transcript[transcriptLen - 1] != '\n') {
    appendTranscript("\n");
  }
  turnStartLine = measureWrappedLines(transcript, LINE_WIDTH);
  appendTranscript("YOU: ");
  appendTranscript(prompt);
  appendTranscript("\n");

  transcriptionTurn = voice && voiceMode == VOICE_TRANSCRIBE;
  audioInputTurn = voice;
  voiceOutputTurn = voiceOutput;
  resetTurnResponse();
  pendingAudio = voice;
  followTail = true;
  scrollOffset = 0;
  responseHeaderWritten = false;
  thinkingHeaderWritten = false;
  requestComplete = false;
  requestSent = false;
  requestStartedAt = millis();
  keyboardOpen = false;
  state = STATE_CONNECTING;
  setStatus("Connecting...");
  return true;
}

bool AiApp::startVoiceRecording() {
  char error[STATUS_MAX] = "";
  releaseCapturedAudio();
  pendingAudio = false;
  if (!audioCapture.beginWav(capturedAudio, AUDIO_CAPTURE_MS, error,
                             sizeof(error))) {
    state = STATE_FAILED;
    setStatus(error[0] ? error : "Audio start failed");
    return true;
  }
  state = STATE_RECORDING;
  setStatus("Recording PWR stop");
  return true;
}

bool AiApp::stopVoiceRecording() {
  audioCapture.finishPcm16(capturedAudio);
  if (capturedAudio.length <= wav::kPcmHeaderSize) {
    pendingAudio = false;
    state = STATE_FAILED;
    setStatus("No audio captured");
    return true;
  }
  submitVoicePrompt();
  return true;
}

bool AiApp::startRequest() {
  if (!hasConfig()) {
    setStatus("Set AI_API_BASE_URL and AI_API_KEY");
    state = STATE_FAILED;
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected");
    state = STATE_FAILED;
    return false;
  }
  if (pendingAudio &&
      (capturedAudio.data == nullptr || capturedAudio.length == 0)) {
    setStatus("No audio captured");
    state = STATE_FAILED;
    return false;
  }
  if ((!voiceOutputTurn && !ensureRxStorage()) ||
      (voiceOutputTurn && !ensureRawResponseStorage())) {
    setStatus("No AI stream mem");
    state = STATE_FAILED;
    return false;
  }

  const char *endpoint = "/chat/completions";

  if (!sendRequest(endpoint)) {
    setStatus("Request write failed");
    state = STATE_FAILED;
    transport.stop();
    releaseRxStorage();
    releaseRawResponseStorage();
    return false;
  }

  if (transport.finishRequest() != agentfiesta::TransportStatus::Ok) {
    setStatus(transport.error());
    state = STATE_FAILED;
    transport.stop();
    releaseRxStorage();
    releaseRawResponseStorage();
    return false;
  }

  requestSent = true;
  requestStartedAt = millis();
  inputText = "";
  pendingAudio = false;
  releaseCapturedAudio();
  state = STATE_STREAMING;
  setStatus("Streaming...");
  return true;
}

bool AiApp::pumpStream() {
  if (!requestSent) {
    return startRequest();
  }

  bool changed = false;
  char c = 0;
  if (voiceOutputTurn) {
    size_t readThisUpdate = 0;
    while (readThisUpdate < 4096U && transport.readBodyByte(c)) {
      requestStartedAt = millis();
      if (!appendRawResponse(c)) {
        transport.stop();
        releaseRawResponseStorage();
        state = STATE_FAILED;
        setStatus("AI voice response too large");
        return true;
      }
      readThisUpdate++;
    }
  } else {
    while (transport.readBodyByte(c)) {
      requestStartedAt = millis();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        rxLine[rxLineLen] = '\0';
        if (rxLineLen > 0) {
          if (startsWith(rxLine, "data:")) {
            const char *payload = rxLine + 5;
            while (*payload == ' ') {
              payload++;
            }
            changed = parseSseEvent(payload) || changed;
          }
        }
        rxLineLen = 0;
        rxLine[0] = '\0';
        continue;
      }
      if (rxLineLen + 1 < RX_LINE_MAX) {
        rxLine[rxLineLen++] = c;
        rxLine[rxLineLen] = '\0';
      } else {
        transport.stop();
        releaseRxStorage();
        state = STATE_FAILED;
        setStatus("AI event too large");
        return true;
      }
    }
  }

  const agentfiesta::TransportStatus responseStatus =
      transport.responseStatus();
  if (responseStatus == agentfiesta::TransportStatus::Error) {
    setStatus(transport.error());
    transport.stop();
    releaseRxStorage();
    releaseRawResponseStorage();
    state = STATE_FAILED;
    return true;
  }
  if (requestComplete || responseStatus == agentfiesta::TransportStatus::End) {
    transport.stop();
    releaseRxStorage();
    const bool parsed = !voiceOutputTurn || parseCompleteResponse();
    releaseRawResponseStorage();
    if (state != STATE_FAILED && parsed && finishTurn()) {
      state = STATE_READY;
      if (voicePlaybackFailed) {
        setStatus("Ready (audio failed)");
      } else {
        setStatus(responseTruncated ? "Ready (trimmed)" : "Ready");
      }
    } else {
      state = STATE_FAILED;
    }
    return true;
  }

  if (millis() - requestStartedAt > STREAM_TIMEOUT_MS) {
    transport.stop();
    releaseRxStorage();
    releaseRawResponseStorage();
    state = STATE_FAILED;
    setStatus("Stream timeout");
    changed = true;
  }
  return changed;
}

bool AiApp::sendRequest(const char *endpoint) {
  const int messageLength = messagesLength();
  if (messageLength <= 0) {
    return false;
  }
  const char *options =
      voiceOutputTurn
          ? ",\"stream\":true,\"modalities\":[\"text\",\"audio\"],"
            "\"audio\":{\"voice\":\"alloy\",\"format\":\"pcm16\"},"
            "\"messages\":["
          : ",\"stream\":true,\"messages\":[";
  const int bodyLen = static_cast<int>(strlen("{\"model\":") +
                                       jsonStringLength(currentModel()) +
                                       strlen(options) + messageLength +
                                       strlen("]}"));
  if (bodyLen <= 0) {
    return false;
  }
  if (transport.begin(SECRET_AI_API_BASE_URL, SECRET_AI_API_KEY, endpoint,
                      static_cast<size_t>(bodyLen), "PocketInk/AI") !=
      agentfiesta::TransportStatus::Ok) {
    return false;
  }
  return writeAll("{\"model\":") && writeJsonString(currentModel()) &&
         writeAll(options) && writeMessages() && writeAll("]}");
}

bool AiApp::writeAll(const char *text) {
  if (text == nullptr) {
    return false;
  }
  return writeAll(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

bool AiApp::writeAll(const uint8_t *data, size_t length) {
  return transport.writeAll(data, length);
}

bool AiApp::writeBase64(const uint8_t *data, size_t length) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char encoded[65];
  size_t offset = 0;
  while (offset < length) {
    int outLen = 0;
    while (offset < length && outLen + 4 < static_cast<int>(sizeof(encoded))) {
      uint32_t a = data[offset++];
      bool haveB = offset < length;
      uint32_t b = haveB ? data[offset++] : 0;
      bool haveC = offset < length;
      uint32_t c = haveC ? data[offset++] : 0;
      uint32_t packed = (a << 16) | (b << 8) | c;
      encoded[outLen++] = alphabet[(packed >> 18) & 0x3f];
      encoded[outLen++] = alphabet[(packed >> 12) & 0x3f];
      encoded[outLen++] = haveB ? alphabet[(packed >> 6) & 0x3f] : '=';
      encoded[outLen++] = haveC ? alphabet[packed & 0x3f] : '=';
    }
    if (!writeAll(reinterpret_cast<const uint8_t *>(encoded),
                  static_cast<size_t>(outLen))) {
      return false;
    }
  }
  return true;
}

int AiApp::jsonStringLength(const char *text) const {
  if (text == nullptr) {
    return 2;
  }
  int length = 2;
  for (int i = 0; text[i] != '\0'; i++) {
    length += (text[i] == '\\' || text[i] == '"' || text[i] == '\n' ||
               text[i] == '\r' || text[i] == '\t')
                  ? 2
                  : 1;
  }
  return length;
}

bool AiApp::writeJsonString(const char *text) {
  if (!writeAll("\"")) {
    return false;
  }
  char buffer[64];
  int used = 0;
  const char *source = text != nullptr ? text : "";
  for (int i = 0; source[i] != '\0'; i++) {
    const char value = source[i];
    const char escaped = value == '\n' ? 'n' : value == '\r' ? 'r'
                              : value == '\t' ? 't'
                              : value;
    const bool needsEscape = value == '\\' || value == '"' ||
                             value == '\n' || value == '\r' || value == '\t';
    if (used + (needsEscape ? 2 : 1) >= static_cast<int>(sizeof(buffer))) {
      if (!writeAll(reinterpret_cast<const uint8_t *>(buffer), used)) {
        return false;
      }
      used = 0;
    }
    if (needsEscape) {
      buffer[used++] = '\\';
    }
    buffer[used++] = escaped;
  }
  return (used == 0 || writeAll(reinterpret_cast<const uint8_t *>(buffer), used)) &&
         writeAll("\"");
}

int AiApp::messagesLength() const {
  const int systemLength = static_cast<int>(strlen("{\"role\":\"system\",\"content\":") +
                                             jsonStringLength(systemPrompt()) + 1);
  int length = systemLength;
  const int first = historyMode == HISTORY_CONVERSATION ? 0 : historyCount - 1;
  for (int i = first < 0 ? 0 : first; i < historyCount; i++) {
    const ChatMessage &message = history[i];
    const bool audio = pendingAudio && i == historyCount - 1 &&
                       message.role == MESSAGE_USER;
    length += 1;
    if (audio) {
      const int audioLength = base64Length(capturedAudio.length);
      if (audioLength < 0) {
        return -1;
      }
      length += static_cast<int>(strlen("{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":") +
                                 jsonStringLength(message.text) +
                                 strlen("},{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"") +
                                 audioLength + strlen("\",\"format\":\"wav\"}}]}"));
      continue;
    }
    const char *role = message.role == MESSAGE_USER ? "user"
                       : message.role == MESSAGE_ASSISTANT ? "assistant"
                                                           : "system";
    length += static_cast<int>(strlen("{\"role\":") + jsonStringLength(role) +
                               strlen(",\"content\":") +
                               jsonStringLength(message.text) + 1);
  }
  return length;
}

bool AiApp::writeMessages() {
  if (!writeAll("{\"role\":\"system\",\"content\":") ||
      !writeJsonString(systemPrompt()) || !writeAll("}")) {
    return false;
  }
  const int first = historyMode == HISTORY_CONVERSATION ? 0 : historyCount - 1;
  for (int i = first < 0 ? 0 : first; i < historyCount; i++) {
    const ChatMessage &message = history[i];
    const bool audio = pendingAudio && i == historyCount - 1 &&
                       message.role == MESSAGE_USER;
    if (!writeAll(",")) {
      return false;
    }
    if (audio) {
      if (capturedAudio.data == nullptr || capturedAudio.length == 0 ||
          !writeAll("{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":") ||
          !writeJsonString(message.text) ||
          !writeAll("},{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"") ||
          !writeBase64(capturedAudio.data, capturedAudio.length) ||
          !writeAll("\",\"format\":\"wav\"}}]}")) {
        return false;
      }
      continue;
    }
    const char *role = message.role == MESSAGE_USER ? "user"
                       : message.role == MESSAGE_ASSISTANT ? "assistant"
                                                           : "system";
    if (!writeAll("{\"role\":") || !writeJsonString(role) ||
        !writeAll(",\"content\":") || !writeJsonString(message.text) ||
        !writeAll("}")) {
      return false;
    }
  }
  return true;
}

bool AiApp::parseSseEvent(const char *payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }
  if (strcmp(payload, "[DONE]") == 0) {
    requestComplete = true;
    return true;
  }

  BufferStream stream(payload, static_cast<int>(strlen(payload)));
  char error[32] = "";
  LightweightJsonParser parser;
  AiStreamListener listener(*this);
  return parser.parse(&stream, static_cast<int>(strlen(payload)),
                      STREAM_TIMEOUT_MS, 1536, listener, error,
                      sizeof(error));
}

bool AiApp::appendTranscript(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  bool changed = false;
  for (int i = 0; text[i] != '\0'; i++) {
    changed = appendTranscriptChar(text[i]) || changed;
  }
  return changed;
}

bool AiApp::appendReasoning(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  bool changed = false;
  if (!thinkingHeaderWritten) {
    changed = appendTranscript("THINKING:\n") || changed;
    thinkingHeaderWritten = true;
  }
  changed = appendTranscript(text) || changed;
  updateResponseScroll();
  return changed;
}

bool AiApp::appendResponse(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  bool changed = false;
  if (!responseHeaderWritten) {
    if (thinkingHeaderWritten &&
        transcriptLen > 0 && transcript[transcriptLen - 1] != '\n') {
      changed = appendTranscript("\n") || changed;
    }
    changed = appendTranscript("AI:\n") || changed;
    responseHeaderWritten = true;
  }
  changed = appendTranscript(text) || changed;
  changed = appendResponseBuffer(text) || changed;
  updateResponseScroll();
  return changed;
}

bool AiApp::appendResponseBuffer(const char *text) {
  if (text == nullptr || text[0] == '\0' || !ensureResponseStorage()) {
    return false;
  }
  bool changed = false;
  int index = 0;
  for (; text[index] != '\0' && responseLen + 1 < RESPONSE_MAX; index++) {
    response[responseLen++] = text[index];
    changed = true;
  }
  response[responseLen] = '\0';
  if (text[index] != '\0') {
    responseTruncated = true;
    setStatus("Response too long");
  }
  return changed;
}

bool AiApp::appendRawResponse(char value) {
  if (!ensureRawResponseStorage(rawResponseLen + 2)) {
    return false;
  }
  rawResponse[rawResponseLen++] = value;
  rawResponse[rawResponseLen] = '\0';
  return true;
}

bool AiApp::captureInputTranscript(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  const size_t sourceLength = strlen(text);
  const size_t length = sourceLength < HISTORY_MAX_TEXT
                            ? sourceLength
                            : HISTORY_MAX_TEXT - 1U;
  char *copy = static_cast<char *>(psram::allocate(length + 1U));
  if (copy == nullptr) {
    return false;
  }
  memcpy(copy, text, length);
  copy[length] = '\0';
  releaseInputTranscript();
  inputTranscript = copy;
  return true;
}

bool AiApp::parseCompleteResponse() {
  if (rawResponse == nullptr || rawResponseLen == 0) {
    setStatus("Empty AI response");
    return false;
  }
  agentfiesta::ChatCompletion completion;
  if (!agentfiesta::parseStreamingChatCompletion(
          rawResponse, static_cast<size_t>(rawResponseLen), completion)) {
    setStatus(completion.errorMessage != nullptr ? completion.errorMessage
                                                 : "Malformed AI response");
    agentfiesta::releaseChatCompletion(completion);
    return false;
  }
  if (completion.inputTranscript != nullptr &&
      !captureInputTranscript(completion.inputTranscript)) {
    setStatus("No transcription memory");
    agentfiesta::releaseChatCompletion(completion);
    return false;
  }
  const char *answer =
      completion.content != nullptr && completion.content[0] != '\0'
          ? completion.content
          : completion.audioTranscript;
  if (answer != nullptr && answer[0] != '\0') {
    appendResponse(answer);
  }
  if (completion.audioData != nullptr && completion.audioBytes > 0 &&
      !agentfiesta::playPcm16VoiceOutput(completion.audioData,
                                        completion.audioBytes)) {
    voicePlaybackFailed = true;
  }
  agentfiesta::releaseChatCompletion(completion);
  return true;
}

void AiApp::resetTurnResponse() {
  responseLen = 0;
  responseTruncated = false;
  voicePlaybackFailed = false;
  turnStored = false;
  releaseRawResponseStorage();
  releaseInputTranscript();
  if (response != nullptr) {
    response[0] = '\0';
  }
}

bool AiApp::addHistoryMessage(MessageRole role, const char *text) {
  if (historyCount >= HISTORY_MAX_MESSAGES || text == nullptr ||
      !ensureHistoryStorage()) {
    return false;
  }
  const size_t sourceLength = strlen(text);
  const size_t length = sourceLength < HISTORY_MAX_TEXT ? sourceLength
                                                        : HISTORY_MAX_TEXT - 1;
  char *copy = static_cast<char *>(psram::allocate(length + 1));
  if (copy == nullptr) {
    return false;
  }
  memcpy(copy, text, length);
  copy[length] = '\0';
  history[historyCount++] = {role, copy};
  return true;
}

void AiApp::clearHistory() {
  if (history == nullptr) {
    historyCount = 0;
    return;
  }
  for (int i = 0; i < historyCount; i++) {
    psram::release(history[i].text);
    history[i] = {};
  }
  historyCount = 0;
}

int AiApp::historyMessageCount() const {
  int count = 0;
  for (int i = 0; i < historyCount; i++) {
    if (history[i].role != MESSAGE_NOTE) {
      count++;
    }
  }
  return count;
}

bool AiApp::replaceLastVoiceMessage(const char *text) {
  if (history == nullptr || text == nullptr || text[0] == '\0') {
    return false;
  }
  for (int i = historyCount - 1; i >= 0; i--) {
    if (history[i].role != MESSAGE_USER) {
      continue;
    }
    const size_t length = strlen(text) < HISTORY_MAX_TEXT ? strlen(text)
                                                           : HISTORY_MAX_TEXT - 1;
    char *copy = static_cast<char *>(psram::allocate(length + 1));
    if (copy == nullptr) {
      return false;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    psram::release(history[i].text);
    history[i].text = copy;
    return true;
  }
  return false;
}

void AiApp::pruneHistory() {
  if (historyMessageCount() < 10) {
    return;
  }
  int real[10];
  int realCount = 0;
  for (int i = 0; i < historyCount && realCount < 10; i++) {
    if (history[i].role != MESSAGE_NOTE) {
      real[realCount++] = i;
    }
  }
  if (realCount < 10) {
    return;
  }
  ChatMessage kept[4] = {history[real[0]], history[real[1]],
                          history[real[8]], history[real[9]]};
  for (int i = 0; i < historyCount; i++) {
    bool preserve = false;
    for (int j = 0; j < 4; j++) {
      preserve = preserve || history[i].text == kept[j].text;
    }
    if (!preserve) {
      psram::release(history[i].text);
    }
    history[i] = {};
  }
  historyCount = 0;
  history[historyCount++] = kept[0];
  history[historyCount++] = kept[1];
  addHistoryMessage(MESSAGE_NOTE, "6 messages before this skipped. ");
  history[historyCount++] = kept[2];
  history[historyCount++] = kept[3];
}

bool AiApp::finishTurn() {
  if (turnStored) {
    return true;
  }
  if (response == nullptr || response[0] == '\0') {
    if (audioInputTurn && inputTranscript != nullptr &&
        replaceLastVoiceMessage(inputTranscript)) {
      turnStored = true;
      rebuildTranscript(true, true);
      transcriptionTurn = false;
      audioInputTurn = false;
      releaseInputTranscript();
      releaseResponseStorage();
      return true;
    }
    setStatus("Empty AI response");
    transcriptionTurn = false;
    audioInputTurn = false;
    releaseInputTranscript();
    releaseResponseStorage();
    return false;
  }

  char *assistantText = response;
  const char *transcribedText = audioInputTurn ? inputTranscript : nullptr;
  if (transcriptionTurn) {
    char *divider = strstr(response, "---");
    while (divider != nullptr) {
      char *lineStart = divider;
      while (lineStart > response && lineStart[-1] != '\n') {
        lineStart--;
      }
      bool onlyWhitespaceBefore = true;
      for (char *cursor = lineStart; cursor < divider; cursor++) {
        if (*cursor != ' ' && *cursor != '\t' && *cursor != '\r') {
          onlyWhitespaceBefore = false;
          break;
        }
      }
      char *after = divider + 3;
      while (*after == ' ' || *after == '\t' || *after == '\r') {
        after++;
      }
      if (onlyWhitespaceBefore && *after == '\n') {
        *lineStart = '\0';
        trimTrailingNewline(response);
        if (transcribedText == nullptr) {
          transcribedText = response;
        }
        assistantText = after + 1;
        while (*assistantText == '\r' || *assistantText == '\n') {
          assistantText++;
        }
        break;
      }
      divider = strstr(divider + 3, "---");
    }
  }

  if (transcribedText != nullptr && transcribedText[0] != '\0' &&
      !replaceLastVoiceMessage(transcribedText)) {
    setStatus("No transcription memory");
    transcriptionTurn = false;
    audioInputTurn = false;
    releaseInputTranscript();
    releaseResponseStorage();
    return false;
  }

  if (assistantText[0] == '\0' ||
      !addHistoryMessage(MESSAGE_ASSISTANT, assistantText)) {
    setStatus(assistantText[0] == '\0' ? "Empty AI answer"
                                        : "No AI history mem");
    transcriptionTurn = false;
    audioInputTurn = false;
    releaseInputTranscript();
    releaseResponseStorage();
    return false;
  }
  turnStored = true;
  pruneHistory();
  rebuildTranscript(true, true);
  transcriptionTurn = false;
  audioInputTurn = false;
  releaseInputTranscript();
  releaseResponseStorage();
  return true;
}

void AiApp::rebuildTranscript(bool anchorLatestTurn,
                              bool preserveManualScroll) {
  if (!ensureTextStorage()) {
    return;
  }
  updateScrollMetrics();
  const bool preserve = preserveManualScroll && !followTail;
  int previousSkip = maxScrollOffset - scrollOffset;
  if (previousSkip < 0) {
    previousSkip = 0;
  }
  transcriptLen = 0;
  transcript[0] = '\0';
  const int first = historyMode == HISTORY_CONVERSATION ? 0 : historyCount - 2;
  int latestUser = -1;
  if (anchorLatestTurn) {
    for (int i = historyCount - 1; i >= (first < 0 ? 0 : first); i--) {
      if (history[i].role == MESSAGE_USER) {
        latestUser = i;
        break;
      }
    }
  }
  for (int i = first < 0 ? 0 : first; i < historyCount; i++) {
    const ChatMessage &message = history[i];
    if (i == latestUser) {
      turnStartLine = measureWrappedLines(transcript, LINE_WIDTH);
    }
    if (message.role == MESSAGE_USER) {
      appendTranscript("YOU: ");
    } else if (message.role == MESSAGE_ASSISTANT) {
      appendTranscript("AI:\n");
    } else {
      appendTranscript("NOTE: ");
    }
    appendTranscript(message.text);
    appendTranscript("\n");
  }
  followTail = !preserve;
  updateScrollMetrics();
  if (preserve) {
    const int skip = previousSkip < maxScrollOffset ? previousSkip
                                                    : maxScrollOffset;
    scrollOffset = maxScrollOffset - skip;
  } else if (anchorLatestTurn) {
    updateResponseScroll();
  } else {
    scrollOffset = 0;
  }
}

void AiApp::updateResponseScroll() {
  if (!followTail) {
    return;
  }
  updateScrollMetrics();
  const int firstVisible =
      turnStartLine < maxScrollOffset ? turnStartLine : maxScrollOffset;
  scrollOffset = maxScrollOffset - firstVisible;
}

bool AiApp::appendTranscriptChar(char c) {
  if (!ensureTextStorage()) {
    setStatus("No AI mem");
    return false;
  }
  if (transcriptLen + 1 >= TRANSCRIPT_MAX) {
    setStatus("Transcript full");
    return false;
  }
  transcript[transcriptLen++] = c;
  transcript[transcriptLen] = '\0';
  return true;
}

void AiApp::setStatus(const char *text) {
  copyText(status, sizeof(status), text);
  status[sizeof(status) - 1] = '\0';
}

const char *AiApp::currentModel() const {
  if (modelIndex == 0) {
    return SECRET_AI_MODEL_PRIMARY[0] ? SECRET_AI_MODEL_PRIMARY : "model-a";
  }
  return SECRET_AI_MODEL_SECONDARY[0] ? SECRET_AI_MODEL_SECONDARY : "model-b";
}

const char *AiApp::systemPrompt() const {
  return transcriptionTurn ? kTranscriptionSystemPrompt : kSystemPrompt;
}

bool AiApp::hasConfig() const {
  return SECRET_AI_API_BASE_URL[0] != '\0' && SECRET_AI_API_KEY[0] != '\0';
}

bool AiApp::handleKeyboardTouch(const TouchPoint &point) {
  KeyboardEvent event =
      keyboardMode == KEYBOARD_QWERTY_ZOOM
          ? zoomKeyboard.hitTest(point, inputText.length(), INPUT_MAX)
          : inputKeyboard.hitTest(point, inputText, INPUT_MAX);
  if (event.action == KEY_NONE) {
    return false;
  }

  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    if (event.action == KEY_CHAR && inputText.length() < INPUT_MAX) {
      inputText += event.value;
    } else if (event.action == KEY_SPACE && inputText.length() < INPUT_MAX) {
      inputText += ' ';
    } else if (event.action == KEY_BACKSPACE && inputText.length() > 0) {
      inputText.remove(inputText.length() - 1);
    }
  }

  if (event.action == KEY_OK) {
    submitPrompt();
  }
  return true;
}

void AiApp::drawMain(Adafruit_GFX &gfx) {
  drawTranscript(gfx);
  drawFooterButtons(gfx);
  drawRightText(gfx, status[0] ? status : "READY", 192);
}

void AiApp::drawKeyboard(Adafruit_GFX &gfx) {
  int maxLength = INPUT_MAX;
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.draw(gfx, inputText, maxLength);
    return;
  }
  inputKeyboard.draw(gfx, inputText, maxLength);
}

void AiApp::drawSettings(Adafruit_GFX &gfx) {
  gfx.fillRect(0, 0, kScreenWidth, 200, 0);
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(4, 10);
  gfx.print("AI SETTINGS");
  const char *labels[] = {"MODEL", "HISTORY", "INPUT", "OUTPUT"};
  const int16_t labelY[] = {32, 68, 104, 140};
  for (int i = 0; i < 4; i++) {
    gfx.setCursor(4, labelY[i]);
    gfx.print(i == settingsSelection ? ">" : " ");
    gfx.print(labels[i]);
  }
  SelectButtonGroup(MODEL_OPTIONS, 2).draw(gfx, modelIndex);
  SelectButtonGroup(HISTORY_OPTIONS, 2).draw(gfx, historyMode);
  SelectButtonGroup(INPUT_OPTIONS, 2).draw(gfx, voiceMode);
  SelectButtonGroup(OUTPUT_OPTIONS, 2).draw(gfx, voiceOutput ? 1 : 0);
  char selectedModel[32] = {};
  snprintf(selectedModel, sizeof(selectedModel), "MODEL %.24s",
           currentModel());
  gfx.setCursor(4, 162);
  gfx.print(selectedModel);
  uiDrawButton(gfx, SAVE_BUTTON, "SAVE", settingsSelection == 4);
}

void AiApp::drawNetworkFailure(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  drawCenteredText(gfx, status[0] ? status : "WiFi not connected", 88);
  drawCenteredText(gfx, "Touch: WiFi off", 112);
}

bool AiApp::handleSettingsTouch(const TouchPoint &point) {
  int8_t value = 0;
  if (uiContains(SAVE_BUTTON, point)) {
    settingsSelection = 4;
    saveSettings();
    return true;
  }
  if (SelectButtonGroup(MODEL_OPTIONS, 2).hitTest(point, value)) {
    settingsSelection = 0;
    if (modelIndex != value) {
      toggleModel();
    }
    return true;
  }
  if (SelectButtonGroup(HISTORY_OPTIONS, 2).hitTest(point, value)) {
    settingsSelection = 1;
    const HistoryMode selected = static_cast<HistoryMode>(value);
    if (historyMode != selected) {
      toggleHistoryMode();
    }
    return true;
  }
  if (SelectButtonGroup(INPUT_OPTIONS, 2).hitTest(point, value)) {
    settingsSelection = 2;
    const VoiceMode selected = static_cast<VoiceMode>(value);
    if (voiceMode != selected) {
      toggleVoiceMode();
    }
    return true;
  }
  if (SelectButtonGroup(OUTPUT_OPTIONS, 2).hitTest(point, value)) {
    settingsSelection = 3;
    if (voiceOutput != (value != 0)) {
      toggleVoiceOutput();
    }
    return true;
  }
  return false;
}

void AiApp::drawTranscript(Adafruit_GFX &gfx) {
  gfx.drawRect(TRANSCRIPT.x, TRANSCRIPT.y, TRANSCRIPT.w, TRANSCRIPT.h, 1);
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  if (!ensureTextStorage()) {
    drawCenteredText(gfx, "No AI memory", 88);
    return;
  }

  updateScrollMetrics();

  int totalLines = measureWrappedLines(transcript, LINE_WIDTH);
  int linesAboveTail = totalLines > VISIBLE_LINES ? totalLines - VISIBLE_LINES
                                                  : 0;
  int skipLines = linesAboveTail - scrollOffset;
  if (skipLines < 0) {
    skipLines = 0;
  }
  int x = TRANSCRIPT.x + 4;
  int y = TRANSCRIPT.y + 4;
  drawWrappedText(gfx, transcript, x, y, LINE_WIDTH, VISIBLE_LINES,
                  skipLines, true);

  if (scrollOffset < maxScrollOffset) {
    gfx.setCursor(TRANSCRIPT.x + TRANSCRIPT.w - 8, TRANSCRIPT.y + 2);
    gfx.print("^");
  }
  if (scrollOffset > 0) {
    gfx.setCursor(TRANSCRIPT.x + TRANSCRIPT.w - 8,
                  TRANSCRIPT.y + TRANSCRIPT.h - 10);
    gfx.print("v");
  }
}

void AiApp::drawFooterButtons(Adafruit_GFX &gfx) {
  uiDrawBorderlessIconButton(gfx, CHAT_BUTTON, 'R');
  uiDrawBorderlessIconButton(gfx, SETTINGS_BUTTON, 'l');
  uiDrawBorderlessIconButton(gfx, RECORD_BUTTON, 'k');

  // The microphone icon labels the adjacent hardware Power recording control.
  gfx.setFont(&iconASCII12pt7b);
  gfx.setTextSize(1);
  gfx.setCursor(150, 184);
  gfx.print("V");
  gfx.setFont();
}

void AiApp::drawCenteredText(Adafruit_GFX &gfx, const char *text, int16_t y,
                             uint8_t size) const {
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.setTextSize(size);
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor((kScreenWidth - static_cast<int>(w)) / 2 - x1, y);
  gfx.print(text);
}

void AiApp::drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y) const {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx.setCursor(196 - static_cast<int>(w) - x1, y);
  gfx.print(text);
}

void AiApp::toggleModel() {
  modelIndex = modelIndex == 0 ? 1 : 0;
  char text[STATUS_MAX];
  snprintf(text, sizeof(text), "Model: %.56s", currentModel());
  setStatus(text);
}

void AiApp::toggleHistoryMode() {
  historyMode = historyMode == HISTORY_SINGLE ? HISTORY_CONVERSATION
                                               : HISTORY_SINGLE;
  if (historyCount > 0) {
    rebuildTranscript();
  }
  setStatus(historyMode == HISTORY_SINGLE ? "Single messages" : "History on");
}

void AiApp::toggleVoiceMode() {
  voiceMode = voiceMode == VOICE_CHAT ? VOICE_TRANSCRIBE : VOICE_CHAT;
  if (voiceMode == VOICE_TRANSCRIBE) {
    voiceOutput = false;
    agentfiesta::stopVoiceOutput();
  }
  setStatus(voiceMode == VOICE_CHAT ? "Voice chat" : "Transcription on");
}

void AiApp::toggleVoiceOutput() {
  if (!voiceOutput && !agentfiesta::voiceOutputSupported()) {
    setStatus("Voice output unavailable");
    return;
  }
  voiceOutput = !voiceOutput;
  if (voiceOutput) {
    voiceMode = VOICE_CHAT;
  } else {
    agentfiesta::stopVoiceOutput();
  }
  setStatus(voiceOutput ? "Voice output on" : "Text output");
}

void AiApp::loadSettings() {
  char value[16] = {};
  appConfigGet(kAiConfig, kAiSettings[AI_SETTING_MODEL], value,
               sizeof(value));
  modelIndex = strcmp(value, "secondary") == 0 ? 1 : 0;

  appConfigGet(kAiConfig, kAiSettings[AI_SETTING_HISTORY], value,
               sizeof(value));
  historyMode = strcmp(value, "conversation") == 0 ? HISTORY_CONVERSATION
                                                    : HISTORY_SINGLE;

  appConfigGet(kAiConfig, kAiSettings[AI_SETTING_INPUT], value,
               sizeof(value));
  voiceMode = strcmp(value, "transcribe") == 0 ? VOICE_TRANSCRIBE
                                                : VOICE_CHAT;

  appConfigGet(kAiConfig, kAiSettings[AI_SETTING_OUTPUT], value,
               sizeof(value));
  voiceOutput = strcmp(value, "voice") == 0 &&
                agentfiesta::voiceOutputSupported();
  if (voiceMode == VOICE_TRANSCRIBE) {
    voiceOutput = false;
  }
}

void AiApp::saveSettings() {
  bool saved = true;
  saved = appConfigSet(kAiConfig, kAiSettings[AI_SETTING_MODEL],
                       modelIndex == 0 ? "primary" : "secondary") &&
          saved;
  saved = appConfigSet(kAiConfig, kAiSettings[AI_SETTING_HISTORY],
                       historyMode == HISTORY_SINGLE ? "single"
                                                     : "conversation") &&
          saved;
  saved = appConfigSet(kAiConfig, kAiSettings[AI_SETTING_INPUT],
                       voiceMode == VOICE_CHAT ? "chat" : "transcribe") &&
          saved;
  saved = appConfigSet(kAiConfig, kAiSettings[AI_SETTING_OUTPUT],
                       voiceOutput ? "voice" : "text") &&
          saved;
  settingsOpen = false;
  setStatus(saved ? "Settings saved" : "Settings save failed");
}

void AiApp::releaseCapturedAudio() {
  audioCapture.release(capturedAudio);
}

bool AiApp::ensureTextStorage() {
  if (transcript != nullptr) {
    return true;
  }
  transcript = static_cast<char *>(psram::allocate(TRANSCRIPT_MAX));
  if (transcript == nullptr) {
    transcriptLen = 0;
    return false;
  }
  copyText(transcript, TRANSCRIPT_MAX, kWelcomeText);
  transcriptLen = strlen(transcript);
  return true;
}

bool AiApp::ensureResponseStorage() {
  if (response != nullptr) {
    return true;
  }
  response = static_cast<char *>(psram::allocate(RESPONSE_MAX));
  if (response == nullptr) {
    responseLen = 0;
    return false;
  }
  response[0] = '\0';
  return true;
}

bool AiApp::ensureRxStorage() {
  if (rxLine != nullptr) {
    rxLineLen = 0;
    rxLine[0] = '\0';
    return true;
  }
  rxLine = static_cast<char *>(psram::allocate(RX_LINE_MAX));
  if (rxLine == nullptr) {
    rxLineLen = 0;
    return false;
  }
  rxLineLen = 0;
  rxLine[0] = '\0';
  return true;
}

bool AiApp::ensureRawResponseStorage(int needed) {
  if (needed < 1 || needed > RAW_RESPONSE_MAX) {
    return false;
  }
  if (rawResponse != nullptr && needed <= rawResponseCapacity) {
    return true;
  }
  int capacity = rawResponseCapacity > 0 ? rawResponseCapacity : 4096;
  while (capacity < needed && capacity < RAW_RESPONSE_MAX) {
    capacity = capacity > RAW_RESPONSE_MAX / 2 ? RAW_RESPONSE_MAX
                                               : capacity * 2;
  }
  if (capacity < needed) {
    return false;
  }
  void *resized = psram::reallocate(rawResponse,
                                    static_cast<size_t>(capacity));
  if (resized == nullptr) {
    return false;
  }
  rawResponse = static_cast<char *>(resized);
  rawResponseCapacity = capacity;
  if (rawResponseLen == 0) {
    rawResponse[0] = '\0';
  }
  return true;
}

bool AiApp::ensureHistoryStorage() {
  if (history != nullptr) {
    return true;
  }
  history = static_cast<ChatMessage *>(
      psram::allocate(sizeof(ChatMessage) * HISTORY_MAX_MESSAGES));
  if (history == nullptr) {
    return false;
  }
  for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
    new (&history[i]) ChatMessage();
  }
  return true;
}

void AiApp::releaseTextStorage() {
  if (transcript != nullptr) {
    psram::release(transcript);
    transcript = nullptr;
  }
  transcriptLen = 0;
}

void AiApp::releaseResponseStorage() {
  psram::release(response);
  response = nullptr;
  responseLen = 0;
}

void AiApp::releaseRxStorage() {
  psram::release(rxLine);
  rxLine = nullptr;
  rxLineLen = 0;
}

void AiApp::releaseRawResponseStorage() {
  psram::release(rawResponse);
  rawResponse = nullptr;
  rawResponseLen = 0;
  rawResponseCapacity = 0;
}

void AiApp::releaseInputTranscript() {
  psram::release(inputTranscript);
  inputTranscript = nullptr;
}

void AiApp::releaseHistoryStorage() {
  clearHistory();
  if (history == nullptr) {
    return;
  }
  for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
    history[i].~ChatMessage();
  }
  psram::release(history);
  history = nullptr;
}

void AiApp::updateScrollMetrics() {
  if (transcript == nullptr) {
    maxScrollOffset = 0;
    scrollOffset = 0;
    return;
  }
  int totalLines = measureWrappedLines(transcript, LINE_WIDTH);
  maxScrollOffset = totalLines > VISIBLE_LINES ? totalLines - VISIBLE_LINES
                                               : 0;
  if (scrollOffset > maxScrollOffset) {
    scrollOffset = maxScrollOffset;
  }
}

int AiApp::measureWrappedLines(const char *text, int maxChars) {
  if (text == nullptr || text[0] == '\0') {
    return 0;
  }

  int lines = 0;
  const char *cursor = text;
  MarkdownLine line;
  bool inBold = false;
  bool inItalic = false;
  while (nextMarkdownLine(cursor, line, maxChars, inBold, inItalic)) {
    lines += line.rows;
  }
  return lines;
}

int AiApp::base64Length(size_t rawLength) {
  size_t encoded = ((rawLength + 2) / 3) * 4;
  return encoded > 0x7fffffff ? -1 : static_cast<int>(encoded);
}

int AiApp::drawWrappedText(Adafruit_GFX &gfx, const char *text, int16_t x,
                           int16_t y, int maxChars, int maxLines,
                           int skipLines, bool draw) {
  int emittedLines = 0;
  int visibleLines = 0;
  const char *cursor = text;
  MarkdownLine line;
  bool inBold = false;
  bool inItalic = false;
  while (visibleLines < maxLines &&
         nextMarkdownLine(cursor, line, maxChars, inBold, inItalic)) {
    const int lineRows = line.rows;
    if (emittedLines + lineRows <= skipLines) {
      emittedLines += lineRows;
      continue;
    }
    if (emittedLines < skipLines) {
      emittedLines += lineRows;
      continue;
    }
    if (visibleLines + lineRows > maxLines) {
      break;
    }
    if (emittedLines >= skipLines) {
      if (draw) {
        gfx.setCursor(x, y + visibleLines * kLineHeight);
        if (line.svg != nullptr) {
          const ImageTransform transform;
          drawVectorStringTransformed(
              gfx, line.svg, line.svgBytes, x,
              y + visibleLines * kLineHeight, TRANSCRIPT.w - 8,
              lineRows * kLineHeight - 2, transform);
        } else if (line.heading) {
          gfx.setTextSize(2);
          gfx.print(line.text);
          gfx.setTextSize(1);
        } else {
          for (int i = 0; line.text[i] != '\0'; i++) {
            const int16_t charX = x + i * 6;
            const int16_t charY = y + visibleLines * kLineHeight;
            gfx.setCursor(charX, charY);
            gfx.print(line.text[i]);
            if ((line.boldMask & (1UL << i)) != 0U) {
              gfx.setCursor(charX + 1, charY);
              gfx.print(line.text[i]);
            }
            if ((line.italicMask & (1UL << i)) != 0U) {
              gfx.drawLine(charX, charY + 8, charX + 4, charY + 8, 1);
            }
          }
        }
      }
    }
    emittedLines += lineRows;
    visibleLines += lineRows;
  }
  return visibleLines;
}

bool AiApp::nextMarkdownLine(const char *&cursor, MarkdownLine &line,
                             int maxChars, bool &inBold, bool &inItalic) {
  if (cursor == nullptr || *cursor == '\0') {
    return false;
  }
  line = {};

  const char *svgStart = cursor;
  while (*svgStart == ' ' || *svgStart == '\t') {
    svgStart++;
  }
  if (strncmp(svgStart, "<svg", 4) == 0 &&
      (svgStart[4] == '>' || svgStart[4] == ' ' || svgStart[4] == '\t' ||
       svgStart[4] == '\n')) {
    const char *svgEnd = strstr(svgStart + 4, "</svg>");
    if (svgEnd == nullptr) {
      copyText(line.text, sizeof(line.text), "[SVG streaming...]");
      cursor += strlen(cursor);
      return true;
    }
    line.svg = svgStart;
    line.svgBytes = static_cast<size_t>(svgEnd + 6 - svgStart);
    line.rows = 8;
    cursor = svgEnd + 6;
    while (*cursor == '\r' || *cursor == '\n') {
      cursor++;
    }
    return true;
  }

  int outLen = 0;

  int markerLength = 0;
  while (cursor[markerLength] == '#') {
    markerLength++;
  }
  if (markerLength > 0 && cursor[markerLength] == ' ') {
    cursor += markerLength + 1;
    line.heading = true;
    line.rows = 2;
  } else if ((cursor[0] == '-' || cursor[0] == '*' || cursor[0] == '+') &&
             cursor[1] == ' ') {
    line.text[outLen++] = '-';
    line.text[outLen++] = ' ';
    cursor += 2;
  } else if (cursor[0] == '>' && cursor[1] == ' ') {
    line.text[outLen++] = '|';
    line.text[outLen++] = ' ';
    cursor += 2;
  }

  const int lineWidth = line.heading ? maxChars / 2 : maxChars;
  int lastSpace = -1;
  const char *afterCharacter[48] = {};
  bool boldAfter[48] = {};
  bool italicAfter[48] = {};
  auto hasClosingMarker = [](const char *start, const char *marker,
                             size_t markerBytes) {
    const char *found = strstr(start, marker);
    const char *newline = strchr(start, '\n');
    return found != nullptr && (newline == nullptr || found < newline) &&
           strlen(found) >= markerBytes;
  };
  while (*cursor && *cursor != '\n' && outLen < lineWidth &&
         outLen + 1 < static_cast<int>(sizeof(line.text))) {
    const bool triple =
        (strncmp(cursor, "***", 3) == 0 || strncmp(cursor, "___", 3) == 0);
    if (triple && ((inBold && inItalic) ||
                   hasClosingMarker(cursor + 3, cursor[0] == '*' ? "***"
                                                               : "___",
                                    3))) {
      inBold = !inBold;
      inItalic = !inItalic;
      cursor += 3;
      continue;
    }
    const bool bold =
        (strncmp(cursor, "**", 2) == 0 || strncmp(cursor, "__", 2) == 0);
    if (bold && (inBold ||
                 hasClosingMarker(cursor + 2, cursor[0] == '*' ? "**" : "__",
                                  2))) {
      inBold = !inBold;
      cursor += 2;
      continue;
    }
    if ((*cursor == '*' || *cursor == '_') &&
        (inItalic || hasClosingMarker(cursor + 1,
                                      *cursor == '*' ? "*" : "_", 1))) {
      inItalic = !inItalic;
      cursor++;
      continue;
    }
    if (*cursor == '`') {
      cursor++;
      continue;
    }
    if (*cursor == '[') {
      const char *labelEnd = cursor + 1;
      while (*labelEnd && *labelEnd != ']' && *labelEnd != '\n') {
        labelEnd++;
      }
      if (*labelEnd == ']' && labelEnd[1] == '(') {
        cursor++;
        continue;
      }
    }
    if (*cursor == ']' && cursor[1] == '(') {
      cursor += 2;
      while (*cursor && *cursor != ')' && *cursor != '\n') {
        cursor++;
      }
      if (*cursor == ')') {
        cursor++;
      }
      continue;
    }
    if (*cursor == '\r') {
      cursor++;
      continue;
    }
    if (*cursor == ' ') {
      lastSpace = outLen;
    }
    if (inBold && outLen < 32) {
      line.boldMask |= 1UL << outLen;
    }
    if (inItalic && outLen < 32) {
      line.italicMask |= 1UL << outLen;
    }
    line.text[outLen] = *cursor++;
    afterCharacter[outLen] = cursor;
    boldAfter[outLen] = inBold;
    italicAfter[outLen] = inItalic;
    outLen++;
  }
  if (*cursor && *cursor != '\n' && outLen >= lineWidth && lastSpace > 0) {
    cursor = afterCharacter[lastSpace];
    inBold = boldAfter[lastSpace];
    inItalic = italicAfter[lastSpace];
    outLen = lastSpace;
    const uint32_t keepMask = (1UL << outLen) - 1U;
    line.boldMask &= keepMask;
    line.italicMask &= keepMask;
    while (*cursor == ' ') {
      cursor++;
    }
  }
  if (*cursor == '\n') {
    cursor++;
  }
  line.text[outLen] = '\0';
  return true;
}

void AiApp::copyText(char *out, int outSize, const char *value) {
  if (out == nullptr || outSize <= 0) {
    return;
  }
  strncpy(out, value ? value : "", outSize - 1);
  out[outSize - 1] = '\0';
}

bool AiApp::startsWith(const char *text, const char *prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

void AiApp::trimTrailingNewline(char *text) {
  if (text == nullptr) {
    return;
  }
  int len = strlen(text);
  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' ||
                     text[len - 1] == ' ')) {
    text[--len] = '\0';
  }
}

#endif
