#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS

#include "netapps/ai_app.h"
#include "netapps/lightweight_json_parser.h"
#include "secrets_config.h"
#include "sys/builtin_apps.h"
#include "ui/ui_helpers.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

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
    if (strcmp(key, "choices") == 0) {
      inChoices = true;
      choicesDepth = depth;
    }
  }

  void onArrayEnd(int depth) override {
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
  int choicesDepth = -1;
  int choiceDepth = -1;
  int deltaDepth = -1;
  int messageDepth = -1;
};

namespace {
static const UiRect MODEL_SELECTOR = {4, 4, 192, 20};
static const UiRect TRANSCRIPT = {4, 30, 192, 130};
static const UiRect CHAT_BUTTON = {28, 166, 84, 24};

static const char *kWelcomeText =
    "AI ready.\n"
    "Tap CHAT to compose a prompt.\n"
    "Tap the model bar to toggle.\n";

static const char *kSystemPrompt =
    "You are a concise assistant. Keep answers clear and structured.";

static bool parseBaseUrl(const char *url, char *host, int hostSize,
                         uint16_t &port, char *path, int pathSize) {
  if (url == nullptr || url[0] == '\0') {
    return false;
  }

  const char *start = url;
  if (strncmp(start, "https://", 8) == 0) {
    start += 8;
    port = 443;
  } else if (strncmp(start, "http://", 7) == 0) {
    start += 7;
    port = 80;
  } else {
    port = 443;
  }

  const char *slash = strchr(start, '/');
  size_t hostLen = slash ? static_cast<size_t>(slash - start) : strlen(start);
  const char *colon = static_cast<const char *>(memchr(start, ':', hostLen));
  if (colon != nullptr) {
    char portText[8] = "";
    size_t portLen = hostLen - static_cast<size_t>(colon - start) - 1;
    if (portLen == 0 || portLen >= sizeof(portText)) {
      return false;
    }
    memcpy(portText, colon + 1, portLen);
    int parsedPort = atoi(portText);
    if (parsedPort <= 0 || parsedPort > 65535) {
      return false;
    }
    hostLen = static_cast<size_t>(colon - start);
    port = static_cast<uint16_t>(parsedPort);
  }
  if (hostLen == 0 || hostLen >= static_cast<size_t>(hostSize)) {
    return false;
  }
  strncpy(host, start, hostLen);
  host[hostLen] = '\0';

  if (slash == nullptr || slash[1] == '\0') {
    strncpy(path, "/v1", pathSize - 1);
    path[pathSize - 1] = '\0';
    return true;
  }

  strncpy(path, slash, pathSize - 1);
  path[pathSize - 1] = '\0';
  size_t pathLen = strlen(path);
  if (pathLen > 1 && path[pathLen - 1] == '/') {
    path[pathLen - 1] = '\0';
  }
  return true;
}
} // namespace

void AiApp::reset() {
  state = STATE_IDLE;
  keyboardMode = KEYBOARD_T9;
  keyboardOpen = false;
  followTail = true;
  responseHeaderWritten = false;
  thinkingHeaderWritten = false;
  requestSent = false;
  requestComplete = false;
  responseChunked = false;
  pendingAudio = false;
  pendingAudioFormat = AUDIO_WAV;
  releaseCapturedAudio();
  modelIndex = 0;
  requestPort = 443;
  scrollOffset = 0;
  maxScrollOffset = 0;
  chunkRemaining = 0;
  transcriptLen = 0;
  rxLineLen = 0;
  requestStartedAt = 0;
  status[0] = '\0';
  requestHost[0] = '\0';
  requestPath[0] = '\0';
  rxLine[0] = '\0';
  chunkLine[0] = '\0';
  pendingPrompt[0] = '\0';
  chunkLineLen = 0;
  inputText = "";
  copyText(transcript, sizeof(transcript), kWelcomeText);
  transcriptLen = strlen(transcript);
  clearActiveMenuButtonConsumer(this);
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
  drawMain(gfx);
}

bool AiApp::update() {
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
    if (capturedAudio.length >=
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

  if (state == STATE_FAILED && WiFi.status() != WL_CONNECTED) {
    wifiTurnOff();
    setStatus("WiFi off");
    return true;
  }

  if (uiContains(MODEL_SELECTOR, point)) {
    toggleModel();
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
    clearActiveMenuButtonConsumer(this);
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
  if (keyboardOpen) {
    return false;
  }
  if (state == STATE_RECORDING) {
    return stopVoiceRecording();
  }
  if (state == STATE_CONNECTING || state == STATE_STREAMING) {
    setStatus("Wait for the current response");
    return true;
  }
  return startVoiceRecording();
}

bool AiApp::handleMenuButton() {
  if (!keyboardOpen) {
    return false;
  }
  if (keyboardMode == KEYBOARD_QWERTY_ZOOM) {
    zoomKeyboard.toggleCaps();
  } else {
    inputKeyboard.toggleCaps();
  }
  return true;
}

bool AiApp::handleMenuDoubleButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardMode = keyboardMode == KEYBOARD_QWERTY_ZOOM ? KEYBOARD_T9
                                                       : KEYBOARD_QWERTY_ZOOM;
  return true;
}

bool AiApp::handleMenuLongButton() {
  if (!keyboardOpen) {
    return false;
  }
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  return true;
}

void AiApp::stopAudio() {
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
  setActiveMenuButtonConsumer(this);
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

  if (transcriptLen > 0 && transcript[transcriptLen - 1] != '\n') {
    appendTranscript("\n");
  }
  appendTranscript("YOU: ");
  appendTranscript(prompt);
  appendTranscript("\n");

  copyText(pendingPrompt, sizeof(pendingPrompt), prompt);
  pendingAudio = false;
  releaseCapturedAudio();
  followTail = true;
  scrollOffset = 0;
  responseHeaderWritten = false;
  thinkingHeaderWritten = false;
  requestComplete = false;
  requestSent = false;
  responseChunked = false;
  chunkRemaining = 0;
  chunkLineLen = 0;
  chunkLine[0] = '\0';
  requestStartedAt = millis();
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  state = STATE_CONNECTING;
  setStatus("Connecting...");
}

void AiApp::submitVoicePrompt() {
  if (capturedAudio.data == nullptr || capturedAudio.length == 0) {
    pendingAudio = false;
    setStatus("No audio captured");
    return;
  }

  if (transcriptLen > 0 && transcript[transcriptLen - 1] != '\n') {
    appendTranscript("\n");
  }
  appendTranscript("YOU: [voice]\n");

  pendingPrompt[0] = '\0';
  pendingAudio = true;
  pendingAudioFormat = AUDIO_PCM16;
  followTail = true;
  scrollOffset = 0;
  responseHeaderWritten = false;
  thinkingHeaderWritten = false;
  requestComplete = false;
  requestSent = false;
  responseChunked = false;
  chunkRemaining = 0;
  chunkLineLen = 0;
  chunkLine[0] = '\0';
  requestStartedAt = millis();
  keyboardOpen = false;
  clearActiveMenuButtonConsumer(this);
  state = STATE_CONNECTING;
  setStatus("Connecting...");
}

bool AiApp::startVoiceRecording() {
  char error[STATUS_MAX] = "";
  releaseCapturedAudio();
  pendingAudio = false;
  if (!audioCapture.beginPcm16(capturedAudio, AUDIO_CAPTURE_MS, error,
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
  if (capturedAudio.length == 0) {
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

  if (!parseBaseUrl(SECRET_AI_API_BASE_URL, requestHost, sizeof(requestHost),
                    requestPort, requestPath, sizeof(requestPath))) {
    setStatus("Bad AI base URL");
    state = STATE_FAILED;
    return false;
  }

  char endpoint[160];
  snprintf(endpoint, sizeof(endpoint), "%s/chat/completions", requestPath);

  client.setInsecure();
  if (!client.connect(requestHost, requestPort)) {
    setStatus("AI connect failed");
    state = STATE_FAILED;
    return false;
  }

  char escapedPrompt[256];
  int escapedPromptLen = 0;
  if (!appendEscapedJson(escapedPrompt, sizeof(escapedPrompt), escapedPromptLen,
                         pendingPrompt)) {
    setStatus("Prompt too long");
    state = STATE_FAILED;
    client.stop();
    return false;
  }
  escapedPrompt[escapedPromptLen] = '\0';

  char escapedSystem[192];
  int escapedSystemLen = 0;
  if (!appendEscapedJson(escapedSystem, sizeof(escapedSystem),
                         escapedSystemLen, kSystemPrompt)) {
    setStatus("Prompt build failed");
    state = STATE_FAILED;
    client.stop();
    return false;
  }
  escapedSystem[escapedSystemLen] = '\0';

  if (!sendRequest(endpoint, escapedSystem, escapedPrompt)) {
    setStatus("Request write failed");
    state = STATE_FAILED;
    client.stop();
    return false;
  }

  char line[256];
  if (!readLine(line, sizeof(line), 4000)) {
    setStatus("No HTTP response");
    state = STATE_FAILED;
    client.stop();
    return false;
  }
  if (!startsWith(line, "HTTP/1.1 200") && !startsWith(line, "HTTP/1.0 200")) {
    setStatus(line);
    state = STATE_FAILED;
    client.stop();
    return false;
  }
  while (readLine(line, sizeof(line), 2000)) {
    if (line[0] == '\0') {
      break;
    }
    if (containsIgnoreCase(line, "transfer-encoding:") &&
        containsIgnoreCase(line, "chunked")) {
      responseChunked = true;
    }
  }

  requestSent = true;
  requestStartedAt = millis();
  inputText = "";
  pendingPrompt[0] = '\0';
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
  while (readBodyByte(c)) {
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
    if (rxLineLen + 1 < static_cast<int>(sizeof(rxLine))) {
      rxLine[rxLineLen++] = c;
      rxLine[rxLineLen] = '\0';
    }
  }

  if (requestComplete || (!client.connected() && client.available() <= 0)) {
    client.stop();
    if (state != STATE_FAILED) {
      state = STATE_READY;
      setStatus("Ready");
    }
    changed = true;
  }

  if (millis() - requestStartedAt > STREAM_TIMEOUT_MS) {
    client.stop();
    state = STATE_FAILED;
    setStatus("Stream timeout");
    changed = true;
  }
  return changed;
}

bool AiApp::readBodyByte(char &out) {
  if (!responseChunked) {
    if (client.available() <= 0) {
      return false;
    }
    int raw = client.read();
    if (raw < 0) {
      return false;
    }
    out = static_cast<char>(raw);
    return true;
  }

  if (chunkRemaining <= 0) {
    while (client.available() > 0) {
      int raw = client.read();
      if (raw < 0) {
        return false;
      }
      char c = static_cast<char>(raw);
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        chunkLine[chunkLineLen] = '\0';
        if (chunkLineLen == 0) {
          continue;
        }
        chunkRemaining = parseChunkSize(chunkLine);
        chunkLineLen = 0;
        chunkLine[0] = '\0';
        if (chunkRemaining <= 0) {
          requestComplete = true;
          return false;
        }
        break;
      }
      if (chunkLineLen + 1 >= static_cast<int>(sizeof(chunkLine))) {
        setStatus("Bad chunk header");
        state = STATE_FAILED;
        return false;
      }
      chunkLine[chunkLineLen++] = c;
      chunkLine[chunkLineLen] = '\0';
    }
    if (chunkRemaining <= 0) {
      return false;
    }
  }

  if (client.available() <= 0) {
    return false;
  }
  int raw = client.read();
  if (raw < 0) {
    return false;
  }
  out = static_cast<char>(raw);
  chunkRemaining--;
  if (chunkRemaining == 0) {
    if (client.peek() == '\r') {
      client.read();
    }
    if (client.peek() == '\n') {
      client.read();
    }
  }
  return true;
}

bool AiApp::buildRequestBody(char *out, int outSize,
                             const char *escapedSystem,
                             const char *escapedPrompt,
                             const char *audioBase64) const {
  if (out == nullptr || outSize <= 0 || escapedSystem == nullptr ||
      escapedPrompt == nullptr) {
    return false;
  }

  int written = 0;
  if (audioBase64 != nullptr && audioBase64[0] != '\0') {
    written = snprintf(out, outSize,
                       "{\"model\":\"%s\",\"stream\":true,"
                       "\"messages\":["
                       "{\"role\":\"system\",\"content\":\"%s\"},"
                       "{\"role\":\"user\",\"content\":["
                       "{\"type\":\"text\",\"text\":\"%s\"},"
                       "{\"type\":\"input_audio\",\"input_audio\":{"
                       "\"data\":\"%s\",\"format\":\"%s\"}}]}]}",
                       currentModel(), escapedSystem, escapedPrompt,
                       audioBase64, audioFormatLabel());
  } else {
    written = snprintf(out, outSize,
                       "{\"model\":\"%s\",\"stream\":true,"
                       "\"messages\":["
                       "{\"role\":\"system\",\"content\":\"%s\"},"
                       "{\"role\":\"user\",\"content\":\"%s\"}]}",
                       currentModel(), escapedSystem, escapedPrompt);
  }
  return written > 0 && written < outSize;
}

bool AiApp::sendRequest(const char *endpoint, const char *escapedSystem,
                        const char *escapedPrompt) {
  int bodyLen = requestBodyLength(escapedSystem, escapedPrompt);
  if (bodyLen <= 0) {
    return false;
  }
  if (!writeRequestHeader(endpoint, bodyLen)) {
    return false;
  }

  if (!pendingAudio) {
    char body[1024];
    if (!buildRequestBody(body, sizeof(body), escapedSystem, escapedPrompt,
                          "")) {
      return false;
    }
    return writeAll(body);
  }

  if (capturedAudio.data == nullptr || capturedAudio.length == 0) {
    return false;
  }

  if (!writeAll("{\"model\":\"") || !writeAll(currentModel()) ||
      !writeAll("\",\"stream\":true,\"messages\":[") ||
      !writeAll("{\"role\":\"system\",\"content\":\"") ||
      !writeAll(escapedSystem) ||
      !writeAll("\"},{\"role\":\"user\",\"content\":[")) {
    return false;
  }
  if (escapedPrompt[0] != '\0' &&
      (!writeAll("{\"type\":\"text\",\"text\":\"") ||
       !writeAll(escapedPrompt) || !writeAll("\"},"))) {
    return false;
  }
  if (!writeAll("{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"")) {
    return false;
  }
  if (!writeBase64(capturedAudio.data, capturedAudio.length)) {
    return false;
  }
  return writeAll("\",\"format\":\"") && writeAll(audioFormatLabel()) &&
         writeAll("\"}}]}]}");
}

bool AiApp::writeRequestHeader(const char *endpoint, int bodyLen) {
  char request[512];
  int requestLen = snprintf(
      request, sizeof(request),
      "POST %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Accept: text/event-stream\r\n"
      "Connection: keep-alive\r\n"
      "User-Agent: PocketInk/AI\r\n"
      "Content-Length: %d\r\n\r\n",
      endpoint, requestHost, SECRET_AI_API_KEY, bodyLen);
  if (requestLen < 0 || requestLen >= static_cast<int>(sizeof(request))) {
    return false;
  }
  return writeAll(reinterpret_cast<const uint8_t *>(request),
                  static_cast<size_t>(requestLen));
}

bool AiApp::writeAll(const char *text) {
  if (text == nullptr) {
    return false;
  }
  return writeAll(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

bool AiApp::writeAll(const uint8_t *data, size_t length) {
  if (data == nullptr && length > 0) {
    return false;
  }
  size_t sent = 0;
  while (sent < length) {
    size_t written = client.write(data + sent, length - sent);
    if (written == 0) {
      return false;
    }
    sent += written;
  }
  return true;
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

int AiApp::requestBodyLength(const char *escapedSystem,
                             const char *escapedPrompt) const {
  if (escapedSystem == nullptr || escapedPrompt == nullptr) {
    return -1;
  }
  if (!pendingAudio) {
    char body[1024];
    if (!buildRequestBody(body, sizeof(body), escapedSystem, escapedPrompt,
                          "")) {
      return -1;
    }
    return static_cast<int>(strlen(body));
  }
  if (capturedAudio.data == nullptr || capturedAudio.length == 0) {
    return -1;
  }

  size_t length = 0;
  length += strlen("{\"model\":\"");
  length += strlen(currentModel());
  length += strlen("\",\"stream\":true,\"messages\":[");
  length += strlen("{\"role\":\"system\",\"content\":\"");
  length += strlen(escapedSystem);
  length += strlen("\"},{\"role\":\"user\",\"content\":[");
  if (escapedPrompt[0] != '\0') {
    length += strlen("{\"type\":\"text\",\"text\":\"");
    length += strlen(escapedPrompt);
    length += strlen("\"},");
  }
  length += strlen("{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"");
  int encodedAudioLength = base64Length(capturedAudio.length);
  if (encodedAudioLength < 0) {
    return -1;
  }
  length += static_cast<size_t>(encodedAudioLength);
  length += strlen("\",\"format\":\"");
  length += strlen(audioFormatLabel());
  length += strlen("\"}}]}]}");
  if (length > 0x7fffffff) {
    return -1;
  }
  return static_cast<int>(length);
}

bool AiApp::readLine(char *out, int outSize, unsigned long timeoutMs) {
  if (out == nullptr || outSize <= 1) {
    return false;
  }

  unsigned long started = millis();
  int len = 0;
  while (millis() - started < timeoutMs) {
    if (client.available() <= 0) {
      delay(1);
      continue;
    }
    int raw = client.read();
    if (raw < 0) {
      continue;
    }
    char c = static_cast<char>(raw);
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      out[len] = '\0';
      return true;
    }
    if (len + 1 < outSize) {
      out[len++] = c;
    }
  }
  out[len] = '\0';
  return len > 0;
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
  if (followTail) {
    scrollOffset = 0;
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
  return changed;
}

bool AiApp::appendTranscriptChar(char c) {
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
  drawSelector(gfx);
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

void AiApp::drawSelector(Adafruit_GFX &gfx) {
  gfx.drawRect(MODEL_SELECTOR.x, MODEL_SELECTOR.y, MODEL_SELECTOR.w,
               MODEL_SELECTOR.h, 1);
  gfx.setTextSize(1);
  gfx.setTextColor(1);
  gfx.setCursor(8, 11);
  gfx.print("MODEL:");
  gfx.print(currentModel());
}

void AiApp::drawTranscript(Adafruit_GFX &gfx) {
  gfx.drawRect(TRANSCRIPT.x, TRANSCRIPT.y, TRANSCRIPT.w, TRANSCRIPT.h, 1);
  gfx.setTextColor(1);
  gfx.setTextSize(1);

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
  uiDrawButton(gfx, CHAT_BUTTON, "CHAT");
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
  setStatus("Model switched");
}

void AiApp::beginVoiceCapture() {
  handlePowerButton();
}

const char *AiApp::audioFormatLabel() const {
  return pendingAudioFormat == AUDIO_PCM16 ? "pcm16" : "wav";
}

void AiApp::releaseCapturedAudio() {
  audioCapture.release(capturedAudio);
}

void AiApp::updateScrollMetrics() {
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
  const char *cursor = skipSpaces(text);
  while (*cursor) {
    if (*cursor == '\n') {
      lines++;
      cursor = skipSpaces(cursor + 1);
      continue;
    }

    int len = 0;
    int lastSpace = -1;
    const char *lineStart = cursor;
    while (*cursor && *cursor != '\n' && len < maxChars &&
           len + 1 < static_cast<int>(TRANSCRIPT_MAX)) {
      if (*cursor == ' ') {
        lastSpace = len;
      }
      cursor++;
      len++;
    }
    if (*cursor && *cursor != '\n' && len >= maxChars && lastSpace > 0) {
      cursor = lineStart + lastSpace + 1;
      len = lastSpace;
    }
    lines++;
    if (*cursor == '\n') {
      cursor++;
    }
    cursor = skipSpaces(cursor);
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
  const char *cursor = skipSpaces(text);
  while (*cursor && visibleLines < maxLines) {
    if (*cursor == '\n') {
      if (emittedLines++ >= skipLines) {
        if (draw) {
          gfx.setCursor(x, y + visibleLines * kLineHeight);
          gfx.print("");
        }
        visibleLines++;
      }
      cursor = skipSpaces(cursor + 1);
      continue;
    }

    char line[48];
    int len = 0;
    int lastSpace = -1;
    const char *lineStart = cursor;
    while (*cursor && *cursor != '\n' && len < maxChars &&
           len + 1 < static_cast<int>(sizeof(line))) {
      if (*cursor == ' ') {
        lastSpace = len;
      }
      line[len++] = *cursor++;
    }
    if (*cursor && *cursor != '\n' && len >= maxChars && lastSpace > 0) {
      cursor = lineStart + lastSpace + 1;
      len = lastSpace;
    }
    if (*cursor == '\n') {
      cursor++;
    }
    line[len] = '\0';
    if (emittedLines++ >= skipLines) {
      if (draw) {
        gfx.setCursor(x, y + visibleLines * kLineHeight);
        gfx.print(line);
      }
      visibleLines++;
    }
    cursor = skipSpaces(cursor);
  }
  return visibleLines;
}

const char *AiApp::skipSpaces(const char *text) {
  while (*text == ' ') {
    text++;
  }
  return text;
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

bool AiApp::containsIgnoreCase(const char *text, const char *needle) {
  if (text == nullptr || needle == nullptr || needle[0] == '\0') {
    return false;
  }
  for (int i = 0; text[i] != '\0'; i++) {
    int j = 0;
    while (needle[j] != '\0' && text[i + j] != '\0') {
      char a = text[i + j];
      char b = needle[j];
      if (a >= 'A' && a <= 'Z') {
        a = a - 'A' + 'a';
      }
      if (b >= 'A' && b <= 'Z') {
        b = b - 'A' + 'a';
      }
      if (a != b) {
        break;
      }
      j++;
    }
    if (needle[j] == '\0') {
      return true;
    }
  }
  return false;
}

int AiApp::parseChunkSize(const char *text) {
  if (text == nullptr) {
    return 0;
  }
  int value = 0;
  for (int i = 0; text[i] != '\0' && text[i] != ';'; i++) {
    char c = text[i];
    int digit = -1;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      break;
    }
    if (value > 0x0fffffff) {
      return 0;
    }
    value = value * 16 + digit;
  }
  return value;
}

bool AiApp::appendEscapedJson(char *out, int outSize, int &length,
                              const char *value) {
  if (out == nullptr || outSize <= 0 || value == nullptr) {
    return false;
  }
  for (int i = 0; value[i] != '\0'; i++) {
    char c = value[i];
    if (c == '\\' || c == '"') {
      if (length + 2 >= outSize) {
        return false;
      }
      out[length++] = '\\';
      out[length++] = c;
      continue;
    }
    if (c == '\n' || c == '\r' || c == '\t') {
      if (length + 2 >= outSize) {
        return false;
      }
      out[length++] = '\\';
      out[length++] = (c == '\n') ? 'n' : (c == '\r') ? 'r' : 't';
      continue;
    }
    if (length + 1 >= outSize) {
      return false;
    }
    out[length++] = c;
  }
  return true;
}

bool AiApp::appendLiteral(char *out, int outSize, int &length,
                          const char *value) {
  if (out == nullptr || outSize <= 0 || value == nullptr) {
    return false;
  }
  int valueLen = strlen(value);
  if (length + valueLen >= outSize) {
    return false;
  }
  memcpy(out + length, value, static_cast<size_t>(valueLen));
  length += valueLen;
  return true;
}

bool AiApp::appendChar(char *out, int outSize, int &length, char value) {
  if (out == nullptr || outSize <= 0 || length + 1 >= outSize) {
    return false;
  }
  out[length++] = value;
  return true;
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
