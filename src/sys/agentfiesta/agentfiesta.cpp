#include "sys/agentfiesta/agentfiesta.h"

#include "sys/agentfiesta/chat_completion.h"
#include "sys/agentfiesta/openai_transport.h"
#include "sys/debug_logging.h"
#include "sys/ext_app_api/app_api.h"
#include "sys/audio/audio_playback_renderer.h"
#include "sys/psram_allocator.h"
#include "sys/system_config.h"
#include "sys/system_fonts.h"

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#if ENABLE_NETWORK_APPS
#include "secrets_config.h"
#endif

#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <esp_rom_sys.h>
#include <new>
#include <cstdio>

#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
#include "sys/audio/audio.h"
#endif

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

namespace agentfiesta {
namespace {

constexpr size_t kAppIdCapacity = 24;
constexpr size_t kPathCapacity = 192;
constexpr size_t kPromptPathCapacity = 192;
constexpr size_t kMaxPromptFileBytes = 8U * 1024U;
constexpr size_t kMaxUserBytes = 2U * 1024U;
constexpr size_t kMaxResponseBytes = 96U * 1024U;
constexpr size_t kMaxVoiceResponseBytes = 2U * 1024U * 1024U;
constexpr size_t kMaxToolArgumentsBytes = 72U * 1024U;
constexpr size_t kMaxToolResultBytes = 8U * 1024U;
constexpr size_t kMaxRequestBytes = 4U * 1024U * 1024U;
constexpr size_t kMaxWavBytes =
    16000U * sizeof(int16_t) * kMaxVoiceDurationMs / 1000U + 4096U;
constexpr uint32_t kVoicePcmSampleRate = 24000U;
constexpr uint32_t kResponseTimeoutMs = 30000;
constexpr uint32_t kSnapshotMagic = 0x54534641UL; // "AFST"
constexpr uint16_t kSnapshotVersion = 2;
constexpr uint8_t kEventCapacity = 16;
constexpr size_t kMalformedResponseLogBytes = 256U;
constexpr uint8_t kMaxHistoryTurns = 64U;

static const char kFallbackSystemPrompt[] =
    "You are an AgentFiesta assistant. Be concise. Use tools only when they "
    "materially help, validate arguments, and report failures plainly.";

static const char kCoreToolsJson[] =
    "[{\"type\":\"function\",\"function\":{\"name\":\"discover_skill\","
    "\"description\":\"Load one bounded AgentFiesta skill by name; use index or list_skills to list available skills.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},"
    "\"required\":[\"name\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"pink_api\","
    "\"description\":\"Dispatch a bounded PocketInk host operation. Discover skills for operation details.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"operation\":{\"type\":\"string\"},\"arguments\":{\"type\":\"object\"}},"
    "\"required\":[\"operation\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"handoff_agent\","
    "\"description\":\"Prepare a bounded prompt for another AgentFiesta app.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"target\":{\"type\":\"string\"},\"prompt\":{\"type\":\"string\"}},"
    "\"required\":[\"target\",\"prompt\"]}}}]";

enum class RunState : uint8_t {
  Idle,
  RequestQueued,
  Receiving,
  ExecutingTool,
  AwaitingExternalTool,
  Failed,
};

struct EventRecord {
  EventType type = EventType::Error;
  FeedEntryType entryType = FeedEntryType::Error;
  Status status = Status::Error;
  uint32_t id = 0;
};

struct CustomTool {
  char *name = nullptr;
  char *description = nullptr;
  char *parameters = nullptr;
};

struct Session {
  FeedStore feed;
  OpenAiTransport transport;
  char appId[kAppIdCapacity] = {};
  char appPath[kPathCapacity] = {};
  char systemPromptPath[kPromptPathCapacity] = {};
  char appPromptPath[kPromptPathCapacity] = {};
  uint32_t toolPacks = ToolPackCore;
  uint32_t nextToolId = 1;
  uint32_t pendingToolId = 0;
  uint32_t voiceEntryId = 0;
  uint8_t historyEntries = 3;
  uint8_t modelIndex = 0;
  uint8_t eventHead = 0;
  uint8_t eventCount = 0;
  uint8_t scrollLines = 0;
  bool approvals = true;
  bool transcriptionMode = false;
  bool voiceOutput = false;
  bool inputWasAudio = false;
  bool turnActive = false;
  bool approvalNotified = false;
  RunState state = RunState::Idle;
  EventRecord events[kEventCapacity] = {};
  uint8_t *wav = nullptr;
  size_t wavBytes = 0;
  char *response = nullptr;
  size_t responseBytes = 0;
  size_t responseCapacity = 0;
  char *pendingToolWireId = nullptr;
  char *pendingToolName = nullptr;
  char *pendingToolArguments = nullptr;
  char *fullToolResult = nullptr;
  size_t fullToolResultBytes = 0;
  CustomTool *customTools = nullptr;
  uint8_t customToolCount = 0;
  uint8_t customToolCapacity = 0;
};

struct Handoff {
  char target[kPathCapacity] = {};
  char prompt[kMaxUserBytes + 1U] = {};
};

struct SnapshotHeader {
  uint32_t magic = kSnapshotMagic;
  uint32_t totalSize = 0;
  uint32_t toolPacks = 0;
  uint16_t version = kSnapshotVersion;
  uint16_t headerSize = sizeof(SnapshotHeader);
  uint16_t entryCount = 0;
  uint8_t historyLimit = 3;
  uint8_t modelIndex = 0;
  uint8_t approvalEnabled = 1;
  uint8_t scrollLines = 0;
  uint8_t reserved[2] = {};
};

struct SnapshotEntryHeader {
  uint16_t length = 0;
  uint8_t type = 0;
  uint8_t reserved = 0;
};

Session *session = nullptr;
Handoff *pendingHandoff = nullptr;

struct PcmVoicePlayer {
  AudioPlaybackRenderer renderer;
  uint8_t *pcm = nullptr;
  size_t pcmBytes = 0;
  size_t frameOffset = 0;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
};

PcmVoicePlayer *pcmVoicePlayer = nullptr;

#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
Audio *compressedVoicePlayer = nullptr;
#endif

uint16_t readLe16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(bytes[1]) << 8U;
}

uint32_t readLe32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         static_cast<uint32_t>(bytes[1]) << 8U |
         static_cast<uint32_t>(bytes[2]) << 16U |
         static_cast<uint32_t>(bytes[3]) << 24U;
}

size_t fillVoicePcm(void *user, int16_t *stereoFrames, size_t frameCount) {
  PcmVoicePlayer *player = static_cast<PcmVoicePlayer *>(user);
  if (player == nullptr || player->pcm == nullptr || stereoFrames == nullptr ||
      player->channels == 0) {
    return 0;
  }
  const size_t bytesPerFrame = player->channels * sizeof(int16_t);
  const size_t totalFrames = player->pcmBytes / bytesPerFrame;
  if (player->frameOffset >= totalFrames) {
    return 0;
  }
  const size_t available = totalFrames - player->frameOffset;
  const size_t take = frameCount < available ? frameCount : available;
  const int16_t *source = reinterpret_cast<const int16_t *>(player->pcm) +
                          player->frameOffset * player->channels;
  if (player->channels == 2U) {
    memcpy(stereoFrames, source, take * 2U * sizeof(int16_t));
  } else {
    for (size_t i = 0; i < take; i++) {
      stereoFrames[i * 2U] = source[i];
      stereoFrames[i * 2U + 1U] = source[i];
    }
  }
  player->frameOffset += take;
  return take;
}

bool startPcmVoicePlayer(const uint8_t *pcm, size_t pcmBytes,
                         uint32_t sampleRate, uint8_t channels) {
  if (pcm == nullptr || sampleRate == 0U ||
      (channels != 1U && channels != 2U)) {
    return false;
  }
  const size_t bytesPerFrame = channels * sizeof(int16_t);
  pcmBytes -= pcmBytes % bytesPerFrame;
  if (pcmBytes == 0U) {
    return false;
  }

  void *memory = psram::allocate(sizeof(PcmVoicePlayer));
  if (memory == nullptr) {
    return false;
  }
  PcmVoicePlayer *player = new (memory) PcmVoicePlayer();
  player->pcm = static_cast<uint8_t *>(psram::allocate(pcmBytes));
  if (player->pcm == nullptr) {
    player->~PcmVoicePlayer();
    psram::release(player);
    return false;
  }
  memcpy(player->pcm, pcm, pcmBytes);
  player->pcmBytes = pcmBytes;
  player->sampleRate = sampleRate;
  player->channels = channels;
  AudioPlaybackStream stream{player, fillVoicePcm};
  AudioPlaybackConfig config;
  config.sampleRate = sampleRate;
  config.chunkFrames = 512U;
  config.volumePercent = 80U;
  char error[48] = {};
  if (!player->renderer.begin(stream, config, error, sizeof(error))) {
    player->renderer.stop();
    psram::release(player->pcm);
    player->~PcmVoicePlayer();
    psram::release(player);
    return false;
  }
  pcmVoicePlayer = player;
  return true;
}

[[maybe_unused]] bool startWavVoicePlayer(const uint8_t *wav,
                                          size_t wavBytes) {
  if (wav == nullptr || wavBytes < 12U || memcmp(wav, "RIFF", 4) != 0 ||
      memcmp(wav + 8U, "WAVE", 4) != 0) {
    return false;
  }
  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint16_t blockAlign = 0;
  uint32_t sampleRate = 0;
  const uint8_t *pcm = nullptr;
  size_t pcmBytes = 0;
  size_t offset = 12U;
  while (offset + 8U <= wavBytes) {
    const uint8_t *chunk = wav + offset;
    const size_t chunkBytes = readLe32(chunk + 4U);
    offset += 8U;
    if (chunkBytes > wavBytes - offset) {
      return false;
    }
    if (memcmp(chunk, "fmt ", 4) == 0 && chunkBytes >= 16U) {
      format = readLe16(wav + offset);
      channels = readLe16(wav + offset + 2U);
      sampleRate = readLe32(wav + offset + 4U);
      blockAlign = readLe16(wav + offset + 12U);
      bitsPerSample = readLe16(wav + offset + 14U);
    } else if (memcmp(chunk, "data", 4) == 0) {
      pcm = wav + offset;
      pcmBytes = chunkBytes;
    }
    const size_t padding = chunkBytes & 1U;
    offset += chunkBytes;
    if (padding > wavBytes - offset) {
      return false;
    }
    offset += padding;
  }
  const size_t expectedBlockAlign = channels * sizeof(int16_t);
  if (format != 1U || (channels != 1U && channels != 2U) ||
      bitsPerSample != 16U || blockAlign != expectedBlockAlign ||
      sampleRate == 0 || pcm == nullptr || pcmBytes < expectedBlockAlign) {
    return false;
  }
  pcmBytes -= pcmBytes % expectedBlockAlign;
  return startPcmVoicePlayer(pcm, pcmBytes, sampleRate,
                             static_cast<uint8_t>(channels));
}


void copyText(char *out, size_t outSize, const char *text) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const char *source = text != nullptr ? text : "";
  strncpy(out, source, outSize - 1U);
  out[outSize - 1U] = '\0';
}

void logMalformedResponse(const char *response, size_t responseBytes) {
  if (!debugLoggingEnabled()) {
    return;
  }
  esp_rom_printf("[ai-http] malformed 200 response (%lu bytes)\n",
                 static_cast<unsigned long>(responseBytes));
  if (response == nullptr || responseBytes == 0U) {
    return;
  }
  char excerpt[kMalformedResponseLogBytes + 1U] = {};
  const size_t copied = responseBytes < kMalformedResponseLogBytes
                            ? responseBytes
                            : kMalformedResponseLogBytes;
  for (size_t i = 0; i < copied; i++) {
    const uint8_t value = static_cast<uint8_t>(response[i]);
    excerpt[i] = value >= 0x20U && value <= 0x7eU
                     ? static_cast<char>(value)
                     : ' ';
  }
  esp_rom_printf("[ai-http] response prefix: %s%s\n", excerpt,
                 responseBytes > copied ? "..." : "");
}

char *duplicateBounded(const char *text, size_t maxBytes,
                       size_t *outLength = nullptr) {
  const char *source = text != nullptr ? text : "";
  size_t length = strlen(source);
  if (length > maxBytes) {
    length = maxBytes;
  }
  char *copy = static_cast<char *>(psram::allocate(length + 1U));
  if (copy == nullptr) {
    return nullptr;
  }
  if (length > 0) {
    memcpy(copy, source, length);
  }
  copy[length] = '\0';
  if (outLength != nullptr) {
    *outLength = length;
  }
  return copy;
}

char *duplicateBytes(const char *text, size_t length, size_t maxBytes,
                     size_t *outLength = nullptr) {
  if (text == nullptr && length > 0) {
    return nullptr;
  }
  const size_t stored = length < maxBytes ? length : maxBytes;
  char *copy = static_cast<char *>(psram::allocate(stored + 1U));
  if (copy == nullptr) {
    return nullptr;
  }
  if (stored > 0) {
    memcpy(copy, text, stored);
  }
  copy[stored] = '\0';
  if (outLength != nullptr) {
    *outLength = stored;
  }
  return copy;
}

void releasePendingTool(Session &target) {
  psram::release(target.pendingToolWireId);
  target.pendingToolWireId = nullptr;
  psram::release(target.pendingToolName);
  target.pendingToolName = nullptr;
  psram::release(target.pendingToolArguments);
  target.pendingToolArguments = nullptr;
  target.pendingToolId = 0;
  target.approvalNotified = false;
}

void releaseRequestInputs(Session &target) {
  psram::release(target.wav);
  target.wav = nullptr;
  target.wavBytes = 0;
  psram::release(target.fullToolResult);
  target.fullToolResult = nullptr;
  target.fullToolResultBytes = 0;
  releasePendingTool(target);
}

void releaseTurnBuffers(Session &target) {
  target.transport.stop();
  releaseRequestInputs(target);
  psram::release(target.response);
  target.response = nullptr;
  target.responseBytes = 0;
  target.responseCapacity = 0;
  target.inputWasAudio = false;
  target.voiceEntryId = 0;
}

void releaseCustomTools(Session &target) {
  for (uint8_t i = 0; i < target.customToolCount; i++) {
    psram::release(target.customTools[i].name);
    psram::release(target.customTools[i].description);
    psram::release(target.customTools[i].parameters);
  }
  for (uint8_t i = 0; i < target.customToolCapacity; i++) {
    target.customTools[i].~CustomTool();
  }
  psram::release(target.customTools);
  target.customTools = nullptr;
  target.customToolCount = 0;
  target.customToolCapacity = 0;
}

void pushEvent(Session &target, EventType type, Status status, uint32_t id = 0,
               FeedEntryType entryType = FeedEntryType::Error) {
  uint8_t index = static_cast<uint8_t>(
      (target.eventHead + target.eventCount) % kEventCapacity);
  if (target.eventCount == kEventCapacity) {
    target.eventHead =
        static_cast<uint8_t>((target.eventHead + 1U) % kEventCapacity);
    index = static_cast<uint8_t>(
        (target.eventHead + target.eventCount - 1U) % kEventCapacity);
  } else {
    target.eventCount++;
  }
  target.events[index] = {type, entryType, status, id};
}

bool appendFeed(Session &target, FeedEntryType type, const char *text,
                size_t length, uint32_t *outId = nullptr) {
  uint32_t id = 0;
  if (!target.feed.append(type, text, length, id)) {
    return false;
  }
  pushEvent(target, EventType::FeedEntry, Status::Ok, id, type);
  if (outId != nullptr) {
    *outId = id;
  }
  return true;
}

bool appendFeed(Session &target, FeedEntryType type, const char *text,
                uint32_t *outId = nullptr) {
  return appendFeed(target, type, text != nullptr ? text : "",
                    text != nullptr ? strlen(text) : 0U, outId);
}

Status fail(Session &target, const char *message, Status status = Status::Error) {
  appendFeed(target, FeedEntryType::Error, message != nullptr ? message : "Error");
  pushEvent(target, EventType::Error, status);
  releaseTurnBuffers(target);
  target.turnActive = false;
  target.state = RunState::Failed;
  return status;
}

void completeTurn(Session &target) {
  releaseTurnBuffers(target);
  target.turnActive = false;
  target.state = RunState::Idle;
  target.feed.pruneCompleted(target.historyEntries);
  pushEvent(target, EventType::TurnComplete, Status::End);
}

class RequestWriter {
public:
  RequestWriter(OpenAiTransport *destination, size_t maximum)
      : transport(destination), maxBytes(maximum) {}

  bool append(const char *text) {
    return text != nullptr && append(text, strlen(text));
  }
  bool append(const char *text, size_t length) {
    if ((text == nullptr && length > 0) || length > maxBytes - written) {
      return false;
    }
    if (transport != nullptr && length > 0 &&
        !transport->writeAll(reinterpret_cast<const uint8_t *>(text), length)) {
      return false;
    }
    written += length;
    return true;
  }
  bool appendChar(char value) { return append(&value, 1U); }
  bool appendJsonString(const char *text, size_t length) {
    if ((text == nullptr && length > 0) || !appendChar('"')) {
      return false;
    }
    const char *source = text != nullptr ? text : "";
    size_t runStart = 0;
    for (size_t i = 0; i < length; i++) {
      const unsigned char value = static_cast<unsigned char>(source[i]);
      if (value >= 0x20U && value != '"' && value != '\\') {
        continue;
      }
      if (!append(source + runStart, i - runStart)) {
        return false;
      }
      if (value == '"' || value == '\\') {
        const char escaped[2] = {'\\', static_cast<char>(value)};
        if (!append(escaped, sizeof(escaped))) {
          return false;
        }
      } else if (value == '\n' || value == '\r' || value == '\t') {
        const char escaped[2] = {
            '\\', value == '\n' ? 'n' : value == '\r' ? 'r' : 't'};
        if (!append(escaped, sizeof(escaped))) {
          return false;
        }
      } else if (!appendChar(' ')) {
        return false;
      }
      runStart = i + 1U;
    }
    return append(source + runStart, length - runStart) && appendChar('"');
  }
  bool appendJsonString(const char *text) {
    const char *source = text != nullptr ? text : "";
    return appendJsonString(source, strlen(source));
  }
  bool appendBase64(const uint8_t *input, size_t inputBytes) {
    if (input == nullptr && inputBytes > 0) {
      return false;
    }
    const size_t encodedBytes = base64EncodedSize(inputBytes);
    if ((encodedBytes == 0 && inputBytes > 0) ||
        encodedBytes > maxBytes - written) {
      return false;
    }
    if (transport == nullptr) {
      written += encodedBytes;
      return true;
    }

    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char encoded[64];
    size_t source = 0;
    while (source < inputBytes) {
      size_t used = 0;
      while (source < inputBytes && used + 4U <= sizeof(encoded)) {
        const uint32_t a = input[source++];
        const bool haveB = source < inputBytes;
        const uint32_t b = haveB ? input[source++] : 0;
        const bool haveC = source < inputBytes;
        const uint32_t c = haveC ? input[source++] : 0;
        const uint32_t packed = (a << 16U) | (b << 8U) | c;
        encoded[used++] = kAlphabet[(packed >> 18U) & 0x3fU];
        encoded[used++] = kAlphabet[(packed >> 12U) & 0x3fU];
        encoded[used++] = haveB ? kAlphabet[(packed >> 6U) & 0x3fU] : '=';
        encoded[used++] = haveC ? kAlphabet[packed & 0x3fU] : '=';
      }
      if (!append(encoded, used)) {
        return false;
      }
    }
    return true;
  }
  size_t size() const { return written; }

private:
  OpenAiTransport *transport = nullptr;
  size_t maxBytes = 0;
  size_t written = 0;
};

class DynamicBuffer {
public:
  explicit DynamicBuffer(size_t maximum) : maxBytes(maximum) {}
  ~DynamicBuffer() { psram::release(data); }

  bool append(const char *text) {
    return text != nullptr && append(text, strlen(text));
  }
  bool append(const char *text, size_t length) {
    if ((text == nullptr && length > 0) || length > maxBytes - used ||
        !reserve(used + length + 1U)) {
      return false;
    }
    if (length > 0) {
      memcpy(data + used, text, length);
      used += length;
    }
    data[used] = '\0';
    return true;
  }
  bool appendChar(char value) { return append(&value, 1U); }
  char *bytes() const { return data; }
  size_t size() const { return used; }

private:
  char *data = nullptr;
  size_t used = 0;
  size_t capacity = 0;
  size_t maxBytes = 0;

  bool reserve(size_t required) {
    if (required <= capacity) {
      return true;
    }
    size_t next = capacity == 0 ? 1024U : capacity;
    while (next < required) {
      if (next > maxBytes / 2U) {
        next = maxBytes;
        break;
      }
      next *= 2U;
    }
    if (next < required || next > maxBytes) {
      return false;
    }
    void *resized = psram::reallocate(data, next);
    if (resized == nullptr) {
      return false;
    }
    data = static_cast<char *>(resized);
    capacity = next;
    return true;
  }
};

char *readBoundedFile(const char *path, size_t maximum, size_t &length) {
  length = 0;
  if (path == nullptr || path[0] != '/') {
    return nullptr;
  }
  size_t fileSize = 0;
  size_t ignored = 0;
  const AppApiStatus queried =
      appApiReadFile(path, 0, nullptr, 0, &ignored, &fileSize);
  if ((queried != AppApiStatus::Ok && queried != kAppApiEnd) ||
      fileSize > maximum) {
    return nullptr;
  }
  char *text = static_cast<char *>(psram::allocate(fileSize + 1U));
  if (text == nullptr) {
    return nullptr;
  }
  size_t read = 0;
  const AppApiStatus status = appApiReadFile(
      path, 0, reinterpret_cast<uint8_t *>(text), fileSize, &read, &fileSize);
  if ((status != AppApiStatus::Ok && status != kAppApiEnd) || read != fileSize) {
    psram::release(text);
    return nullptr;
  }
  text[read] = '\0';
  length = read;
  return text;
}

struct RequestPrompts {
  ~RequestPrompts() {
    psram::release(system);
    psram::release(app);
  }

  char *system = nullptr;
  size_t systemLength = 0;
  char *app = nullptr;
  size_t appLength = 0;
};

[[maybe_unused]] void loadRequestPrompts(const Session &target,
                                         RequestPrompts &prompts) {
  prompts.system = readBoundedFile(target.systemPromptPath,
                                   kMaxPromptFileBytes,
                                   prompts.systemLength);
  if (target.appPromptPath[0] != '\0') {
    prompts.app = readBoundedFile(target.appPromptPath, kMaxPromptFileBytes,
                                  prompts.appLength);
  }
}

const char *roleFor(FeedEntryType type) {
  switch (type) {
  case FeedEntryType::User:
  case FeedEntryType::ToolResult:
    return "user";
  case FeedEntryType::Assistant:
  case FeedEntryType::ToolCall:
    return "assistant";
  case FeedEntryType::Error:
    return nullptr;
  }
  return nullptr;
}

bool appendMessage(RequestWriter &body, bool &first, const char *role,
                   const char *text, size_t length) {
  if (role == nullptr) {
    return true;
  }
  if (!first && !body.appendChar(',')) {
    return false;
  }
  first = false;
  return body.append("{\"role\":\"") && body.append(role) &&
         body.append("\",\"content\":") &&
         body.appendJsonString(text, length) && body.appendChar('}');
}

bool appendToolContinuation(RequestWriter &body, bool &first,
                            const Session &target) {
  if (target.fullToolResult == nullptr) {
    return true;
  }
  if (target.pendingToolWireId == nullptr || target.pendingToolName == nullptr ||
      target.pendingToolArguments == nullptr) {
    return false;
  }
  if ((!first && !body.appendChar(',')) ||
      !body.append("{\"role\":\"assistant\",\"content\":null,"
                   "\"tool_calls\":[{\"id\":") ||
      !body.appendJsonString(target.pendingToolWireId) ||
      !body.append(",\"type\":\"function\",\"function\":{\"name\":") ||
      !body.appendJsonString(target.pendingToolName) ||
      !body.append(",\"arguments\":") ||
      !body.appendJsonString(target.pendingToolArguments) ||
      !body.append("}}]},{\"role\":\"tool\",\"tool_call_id\":") ||
      !body.appendJsonString(target.pendingToolWireId) ||
      !body.append(",\"content\":") ||
      !body.appendJsonString(target.fullToolResult,
                             target.fullToolResultBytes) ||
      !body.appendChar('}')) {
    return false;
  }
  first = false;
  return true;
}

[[maybe_unused]] bool buildRequest(const Session &target,
                                   const RequestPrompts &prompts,
                                   RequestWriter &body) {
  const char *model = target.modelIndex == 0 ? SECRET_AI_MODEL_PRIMARY
                                             : SECRET_AI_MODEL_SECONDARY;
  if (model == nullptr || model[0] == '\0') {
    model = target.modelIndex == 0 ? "model-a" : "model-b";
  }
  if (!body.append("{\"model\":") || !body.appendJsonString(model) ||
      !body.append(target.voiceOutput
                       ? ",\"stream\":true,\"messages\":["
                       : ",\"stream\":false,\"messages\":[")) {
    return false;
  }
  bool first = true;
  const char *systemText = prompts.system != nullptr
                               ? prompts.system
                               : kFallbackSystemPrompt;
  const size_t systemLength = prompts.system != nullptr
                                  ? prompts.systemLength
                                  : strlen(kFallbackSystemPrompt);
  if (!appendMessage(body, first, "system", systemText, systemLength)) {
    return false;
  }
  if (target.transcriptionMode &&
      !appendMessage(body, first, "system", kTranscriptionInstruction,
                     strlen(kTranscriptionInstruction))) {
    return false;
  }
  if (prompts.app != nullptr &&
      !appendMessage(body, first, "system", prompts.app,
                     prompts.appLength)) {
    return false;
  }

  const size_t feedCount = target.feed.count();
  for (size_t i = 0; i < feedCount; i++) {
    FeedEntryView entry;
    if (!target.feed.at(i, entry)) {
      continue;
    }
    const bool currentToolEntry =
        target.fullToolResult != nullptr &&
        ((entry.type == FeedEntryType::ToolCall && i + 2U == feedCount) ||
         (entry.type == FeedEntryType::ToolResult && i + 1U == feedCount));
    if (currentToolEntry || entry.type == FeedEntryType::Error) {
      continue;
    }
    const bool audioMessage = target.wav != nullptr &&
                              entry.type == FeedEntryType::User &&
                              i + 1U == feedCount;
    if (!audioMessage) {
      if (!appendMessage(body, first, roleFor(entry.type), entry.text,
                         entry.length)) {
        return false;
      }
      continue;
    }
    if (!first && !body.appendChar(',')) {
      return false;
    }
    first = false;
    if (!body.append("{\"role\":\"user\",\"content\":[{\"type\":\"text\","
                     "\"text\":") ||
        !body.appendJsonString(entry.text, entry.length) ||
        !body.append("},{\"type\":\"input_audio\",\"input_audio\":{"
                     "\"data\":\"") ||
        !body.appendBase64(target.wav, target.wavBytes) ||
        !body.append("\",\"format\":\"wav\"}}]}")) {
      return false;
    }
  }
  if (!appendToolContinuation(body, first, target)) {
    return false;
  }
  if (!body.appendChar(']')) {
    return false;
  }
  if (target.voiceOutput &&
      !body.append(",\"modalities\":[\"text\",\"audio\"],"
                   "\"audio\":{\"voice\":\"alloy\",\"format\":\"pcm16\"}")) {
    return false;
  }
  const bool hasCoreTools =
      (target.toolPacks & (ToolPackCore | ToolPackDevice)) != 0U;
  if (hasCoreTools || target.customToolCount > 0) {
    if (!body.append(",\"tools\":[")) {
      return false;
    }
    bool firstTool = true;
    if (hasCoreTools) {
      const size_t coreLength = strlen(kCoreToolsJson);
      if (coreLength < 2U ||
          !body.append(kCoreToolsJson + 1U, coreLength - 2U)) {
        return false;
      }
      firstTool = false;
    }
    for (uint8_t i = 0; i < target.customToolCount; i++) {
      const CustomTool &tool = target.customTools[i];
      if ((!firstTool && !body.appendChar(',')) ||
          !body.append("{\"type\":\"function\",\"function\":{\"name\":") ||
          !body.appendJsonString(tool.name) ||
          !body.append(",\"description\":") ||
          !body.appendJsonString(tool.description) ||
          !body.append(",\"parameters\":") ||
          !body.append(tool.parameters) || !body.append("}}")) {
        return false;
      }
      firstTool = false;
    }
    if (!body.append("],\"tool_choice\":\"auto\"")) {
      return false;
    }
  }
  return body.appendChar('}');
}

[[maybe_unused]] Status beginRequest(Session &target) {
#if !ENABLE_NETWORK_APPS
  (void)target;
  return Status::Unsupported;
#else
  if (SECRET_AI_API_BASE_URL[0] == '\0' || SECRET_AI_API_KEY[0] == '\0') {
    return fail(target, "AI configuration is missing");
  }
  RequestPrompts prompts;
  loadRequestPrompts(target, prompts);
  RequestWriter measured(nullptr, kMaxRequestBytes);
  if (!buildRequest(target, prompts, measured)) {
    return fail(target, "AI request exceeds memory limits");
  }
  const TransportStatus opened = target.transport.begin(
      SECRET_AI_API_BASE_URL, SECRET_AI_API_KEY, "/chat/completions",
      measured.size());
  if (opened == TransportStatus::Unsupported) {
    return fail(target, "AI is unsupported in this build", Status::Unsupported);
  }
  RequestWriter output(&target.transport, kMaxRequestBytes);
  if (opened != TransportStatus::Ok || !buildRequest(target, prompts, output) ||
      output.size() != measured.size() ||
      target.transport.finishRequest() != TransportStatus::Ok) {
    return fail(target, target.transport.error());
  }
  releaseRequestInputs(target);
  target.responseCapacity = 4096U;
  target.response =
      static_cast<char *>(psram::allocate(target.responseCapacity));
  if (target.response == nullptr) {
    return fail(target, "Unable to allocate AI response");
  }
  target.response[0] = '\0';
  target.responseBytes = 0;
  target.state = RunState::Receiving;
  return Status::Pending;
#endif
}

bool ensureResponseCapacity(Session &target, size_t needed, size_t maximum) {
  if (needed <= target.responseCapacity) {
    return true;
  }
  size_t next = target.responseCapacity;
  while (next < needed && next < maximum + 1U) {
    next *= 2U;
  }
  if (next > maximum + 1U) {
    next = maximum + 1U;
  }
  if (next < needed) {
    return false;
  }
  void *resized = psram::reallocate(target.response, next);
  if (resized == nullptr) {
    return false;
  }
  target.response = static_cast<char *>(resized);
  target.responseCapacity = next;
  return true;
}

const char *findJsonValue(const char *json, const char *key,
                          const char *start = nullptr) {
  if (json == nullptr || key == nullptr) {
    return nullptr;
  }
  const size_t keyLength = strlen(key);
  const char *cursor = start != nullptr ? start : json;
  while ((cursor = strchr(cursor, '"')) != nullptr) {
    const char *text = ++cursor;
    bool escaped = false;
    while (*cursor != '\0') {
      if (!escaped && *cursor == '"') {
        break;
      }
      if (!escaped && *cursor == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
      cursor++;
    }
    if (*cursor == '\0') {
      return nullptr;
    }
    const size_t length = static_cast<size_t>(cursor - text);
    const char *after = cursor + 1;
    while (*after == ' ' || *after == '\t' || *after == '\r' ||
           *after == '\n') {
      after++;
    }
    if (length == keyLength && memcmp(text, key, keyLength) == 0 &&
        *after == ':') {
      after++;
      while (*after == ' ' || *after == '\t' || *after == '\r' ||
             *after == '\n') {
        after++;
      }
      return after;
    }
    cursor++;
  }
  return nullptr;
}

char *decodeJsonString(const char *value, size_t maximum,
                       size_t *outLength = nullptr) {
  if (value == nullptr || *value != '"') {
    return nullptr;
  }
  const char *cursor = value + 1;
  size_t length = 0;
  bool escaped = false;
  while (*cursor != '\0') {
    if (!escaped && *cursor == '"') {
      break;
    }
    if (!escaped && *cursor == '\\') {
      escaped = true;
      cursor++;
      continue;
    }
    escaped = false;
    length++;
    cursor++;
    if (length > maximum) {
      return nullptr;
    }
  }
  if (*cursor != '"') {
    return nullptr;
  }
  char *out = static_cast<char *>(psram::allocate(length + 1U));
  if (out == nullptr) {
    return nullptr;
  }
  cursor = value + 1;
  size_t used = 0;
  while (*cursor != '\0' && *cursor != '"') {
    char current = *cursor++;
    if (current == '\\' && *cursor != '\0') {
      current = *cursor++;
      if (current == 'n' || current == 'r' || current == 't') {
        current = current == 'n' ? '\n' : current == 'r' ? '\r' : '\t';
      } else if (current == 'u') {
        for (uint8_t i = 0; i < 4U && *cursor != '\0'; i++) {
          cursor++;
        }
        current = '?';
      }
    }
    out[used++] = current;
  }
  out[used] = '\0';
  if (outLength != nullptr) {
    *outLength = used;
  }
  return out;
}

bool jsonArgument(const char *arguments, const char *key, char *out,
                  size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  const char *value = findJsonValue(arguments, key);
  size_t length = 0;
  char *decoded = decodeJsonString(value, outSize - 1U, &length);
  if (decoded == nullptr) {
    return false;
  }
  memcpy(out, decoded, length + 1U);
  psram::release(decoded);
  return true;
}

size_t jsonUnsignedArgument(const char *arguments, const char *key,
                            size_t fallback) {
  const char *value = findJsonValue(arguments, key);
  if (value == nullptr || *value < '0' || *value > '9') {
    return fallback;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  return end != value ? static_cast<size_t>(parsed) : fallback;
}

bool safeSkillName(const char *name) {
  if (name == nullptr || name[0] == '\0' || strlen(name) > 48U) {
    return false;
  }
  for (const char *cursor = name; *cursor != '\0'; cursor++) {
    const char value = *cursor;
    if (!((value >= 'a' && value <= 'z') ||
          (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '-' || value == '_')) {
      return false;
    }
  }
  return true;
}

bool secretConfigKey(const char *key) {
  if (key == nullptr) {
    return true;
  }
  const char *tokens[] = {"password", "secret", "token", "apiKey", "apikey"};
  for (const char *token : tokens) {
    if (strstr(key, token) != nullptr) {
      return true;
    }
  }
  return false;
}

bool allowedConfigKey(const char *key) {
  static const char *const kAllowed[] = {
      "home.sunBtnApp",
      "home.widgets",
      "layout",
      "interface.audioPlayerLayout",
      "lockscreen.widget",
      "lockscreen.topIcon",
      "lockscreen.date",
      "lockscreen.inactivityMinutes",
      "notifications.mode",
      "notifications.ringtone",
      "wifi.autoConnect",
      "wifi.autoOffMinutes",
      "cpu.mhz",
      "cpu.governor",
      "time.format",
      "date.format",
      "time.syncProtocol",
      "time.syncIntervalHours",
      "timezone",
      "temp.unit",
      "sensor.tempOffsetC",
      "periodic.minIntervalMinutes",
      "periodic.disableOnLowBattery",
  };
  if (key == nullptr || secretConfigKey(key)) {
    return false;
  }
  for (const char *allowed : kAllowed) {
    if (strcmp(key, allowed) == 0) {
      return true;
    }
  }
  return false;
}

Status protectedAction(Session &target, const char *action,
                       AppApiAgentCapability capability,
                       const char *resource) {
  const AppApiStatus status =
      target.approvals
          ? appApiRequestAgentAction(action, capability, resource)
          : appApiRequestCapability(capability, resource);
  if (status == AppApiStatus::Pending) {
    if (!target.approvalNotified) {
      pushEvent(target, EventType::ApprovalPending, Status::Pending,
                target.pendingToolId);
      target.approvalNotified = true;
    }
    return Status::Pending;
  }
  target.approvalNotified = false;
  if (status == AppApiStatus::Denied) {
    return Status::Denied;
  }
  return status == AppApiStatus::Ok ? Status::Ok : Status::Error;
}

Status executePinkApi(Session &target, const char *arguments, char *&result,
                      size_t &resultLength) {
  char operation[40] = {};
  if (!jsonArgument(arguments, "operation", operation, sizeof(operation))) {
    result = duplicateBounded("pink_api requires an operation", 96U,
                              &resultLength);
    return Status::Error;
  }
  if (strcmp(operation, "read") == 0) {
    char path[kPathCapacity] = {};
    if (!jsonArgument(arguments, "path", path, sizeof(path))) {
      result = duplicateBounded("read requires path", 96U, &resultLength);
      return Status::Error;
    }
    const size_t offset = jsonUnsignedArgument(arguments, "offset", 0U);
    size_t limit = jsonUnsignedArgument(arguments, "limit", 4096U);
    if (offset > 65535U) {
      result = duplicateBounded("read offset exceeds 65535", 96U,
                                &resultLength);
      return Status::Error;
    }
    if (limit == 0 || limit > kMaxToolResultBytes) {
      limit = kMaxToolResultBytes;
    }
    result = static_cast<char *>(psram::allocate(limit + 1U));
    if (result == nullptr) {
      return Status::Error;
    }
    size_t fileSize = 0;
    size_t read = 0;
    const AppApiStatus status = appApiReadFile(
        path, offset, reinterpret_cast<uint8_t *>(result), limit, &read,
        &fileSize);
    if (status != AppApiStatus::Ok && status != kAppApiEnd) {
      psram::release(result);
      char message[256] = {};
      snprintf(message, sizeof(message), "read failed: %s", path);
      result = duplicateBounded(message, sizeof(message) - 1U, &resultLength);
      return Status::Error;
    }
    result[read] = '\0';
    resultLength = read;
    return Status::Ok;
  }

  if ((target.toolPacks & ToolPackDevice) == 0U) {
    result = duplicateBounded("device tool pack is disabled", 96U,
                              &resultLength);
    return Status::Denied;
  }
  if (strcmp(operation, "config_get") == 0) {
    char key[64] = {};
    if (!jsonArgument(arguments, "key", key, sizeof(key))) {
      result = duplicateBounded("setting is not readable", 96U,
                                &resultLength);
      return Status::Denied;
    }
    if (secretConfigKey(key)) {
      result = duplicateBounded("[redacted]", 16U, &resultLength);
      return result != nullptr ? Status::Ok : Status::Error;
    }
    if (!allowedConfigKey(key)) {
      result = duplicateBounded("setting is not readable", 96U,
                                &resultLength);
      return Status::Denied;
    }
    char value[CONFIG_VALUE_MAX] = {};
    if (!configGet(key, value, sizeof(value))) {
      result = duplicateBounded("setting not found", 96U, &resultLength);
      return Status::Error;
    }
    result = duplicateBounded(value, CONFIG_VALUE_MAX, &resultLength);
    return result != nullptr ? Status::Ok : Status::Error;
  }
  if (strcmp(operation, "config_set") == 0) {
    char key[64] = {};
    char value[CONFIG_VALUE_MAX] = {};
    if (!jsonArgument(arguments, "key", key, sizeof(key)) ||
        !jsonArgument(arguments, "value", value, sizeof(value)) ||
        !allowedConfigKey(key)) {
      result = duplicateBounded("setting change rejected", 96U,
                                &resultLength);
      return Status::Denied;
    }
    char action[112] = {};
    snprintf(action, sizeof(action), "Change setting %s", key);
    const Status approved = protectedAction(
        target, action, AppApiAgentCapability::SystemConfig, key);
    if (approved != Status::Ok) {
      return approved;
    }
    const bool saved = configSet(key, value);
    result = duplicateBounded(saved ? "setting updated" : "invalid setting value",
                              96U, &resultLength);
    return saved ? Status::Ok : Status::Error;
  }
  if (strcmp(operation, "open_app") == 0) {
    char id[kAppIdCapacity] = {};
    if (!jsonArgument(arguments, "id", id, sizeof(id))) {
      result = duplicateBounded("open_app requires id", 96U, &resultLength);
      return Status::Error;
    }
    char action[80] = {};
    snprintf(action, sizeof(action), "Open app %s", id);
    const Status approved = protectedAction(
        target, action, AppApiAgentCapability::None, id);
    if (approved != Status::Ok) {
      return approved;
    }
    const bool queued = appApiRequestOpenApp(id);
    result = duplicateBounded(queued ? "app launch queued" : "app not queued",
                              96U, &resultLength);
    return queued ? Status::Ok : Status::Error;
  }
  if (strcmp(operation, "file_write") == 0) {
    char path[kPathCapacity] = {};
    size_t contentLength = 0;
    char *content = decodeJsonString(findJsonValue(arguments, "content"),
                                     kMaxToolResultBytes, &contentLength);
    if (!jsonArgument(arguments, "path", path, sizeof(path)) ||
        content == nullptr) {
      psram::release(content);
      result = duplicateBounded("file_write requires path and content", 96U,
                                &resultLength);
      return Status::Error;
    }
    char action[256] = {};
    snprintf(action, sizeof(action), "Write %s", path);
    const Status approved = protectedAction(
        target, action, AppApiAgentCapability::FileWrite, path);
    if (approved != Status::Ok) {
      psram::release(content);
      return approved;
    }
    const AppApiStatus written = appApiWriteFile(
        path, reinterpret_cast<const uint8_t *>(content), contentLength,
        false);
    psram::release(content);
    result = duplicateBounded(written == AppApiStatus::Ok ? "file written"
                                                          : "file write failed",
                              96U, &resultLength);
    return written == AppApiStatus::Ok ? Status::Ok : Status::Error;
  }
  if (strcmp(operation, "file_delete") == 0) {
    char path[kPathCapacity] = {};
    if (!jsonArgument(arguments, "path", path, sizeof(path))) {
      result = duplicateBounded("file_delete requires path", 96U,
                                &resultLength);
      return Status::Error;
    }
    char action[256] = {};
    snprintf(action, sizeof(action), "Delete %s", path);
    const Status approved = protectedAction(
        target, action, AppApiAgentCapability::FileDelete, path);
    if (approved != Status::Ok) {
      return approved;
    }
    const AppApiStatus removed = appApiDeleteFile(path);
    result = duplicateBounded(removed == AppApiStatus::Ok ? "file deleted"
                                                          : "file delete failed",
                              96U, &resultLength);
    return removed == AppApiStatus::Ok ? Status::Ok : Status::Error;
  }
  result = duplicateBounded("unknown pink_api operation", 96U, &resultLength);
  return Status::Unsupported;
}

Status executeTool(Session &target, char *&result, size_t &resultLength) {
  result = nullptr;
  resultLength = 0;
  if (target.pendingToolName == nullptr ||
      target.pendingToolArguments == nullptr) {
    return Status::Error;
  }
  if (strcmp(target.pendingToolName, "discover_skill") == 0) {
    char name[52] = {};
    if (!jsonArgument(target.pendingToolArguments, "name", name,
                      sizeof(name)) ||
        !safeSkillName(name)) {
      result = duplicateBounded("invalid skill name", 96U, &resultLength);
      return Status::Error;
    }
    char path[128] = {};
    if (strcmp(name, "index") == 0 || strcmp(name, "list_skills") == 0) {
      copyText(path, sizeof(path), "/misc/agentfiesta/skills/index.tsv");
    } else {
      snprintf(path, sizeof(path), "/misc/agentfiesta/skills/%s.md", name);
    }
    result = readBoundedFile(path, kMaxToolResultBytes, resultLength);
    if (result == nullptr) {
      result = duplicateBounded("skill not found", 96U, &resultLength);
      return Status::Error;
    }
    return Status::Ok;
  }
  if (strcmp(target.pendingToolName, "pink_api") == 0) {
    return executePinkApi(target, target.pendingToolArguments, result,
                          resultLength);
  }
  if (strcmp(target.pendingToolName, "handoff_agent") == 0) {
    char targetPath[kPathCapacity] = {};
    char prompt[kMaxUserBytes + 1U] = {};
    if (!jsonArgument(target.pendingToolArguments, "target", targetPath,
                      sizeof(targetPath)) ||
        !jsonArgument(target.pendingToolArguments, "prompt", prompt,
                      sizeof(prompt))) {
      result = duplicateBounded("handoff requires target and prompt", 96U,
                                &resultLength);
      return Status::Error;
    }
    const Status prepared = handoff(targetPath, prompt);
    result = duplicateBounded(prepared == Status::Ok ? "handoff ready"
                                                     : "handoff failed",
                              96U, &resultLength);
    if (prepared == Status::Ok) {
      pushEvent(target, EventType::HandoffReady, Status::Ok,
                target.pendingToolId);
    }
    return prepared;
  }
  return Status::Unsupported;
}

Status queueToolResult(Session &target, const char *text, size_t length,
                       bool isError) {
  const char *display = text != nullptr ? text : "";
  size_t displayLength = length;
  DynamicBuffer preview(1200U);
  if (!isError && target.pendingToolName != nullptr &&
      strcmp(target.pendingToolName, "read") == 0 && length > 0U) {
    size_t totalLines = 1U;
    for (size_t i = 0; i < length; i++) {
      if (display[i] == '\n' && i + 1U < length) {
        totalLines++;
      }
    }
    size_t shownLines = 1U;
    size_t shownBytes = 0U;
    while (shownBytes < length && shownLines <= 3U) {
      if (display[shownBytes++] == '\n') {
        shownLines++;
      }
    }
    const size_t visibleLines = totalLines < 3U ? totalLines : 3U;
    const size_t moreLines = totalLines - visibleLines;
    if (shownBytes > 0U && shownBytes <= 1024U &&
        preview.append(display, shownBytes)) {
      if (moreLines > 0U) {
        if (preview.size() > 0U &&
            preview.bytes()[preview.size() - 1U] != '\n') {
          preview.append("\n");
        }
        char suffix[40] = {};
        snprintf(suffix, sizeof(suffix), "... %lu more line%s",
                 static_cast<unsigned long>(moreLines),
                 moreLines == 1U ? "" : "s");
        preview.append(suffix);
      }
      display = preview.bytes();
      displayLength = preview.size();
    }
  }
  if (!appendFeed(target,
                  isError ? FeedEntryType::Error : FeedEntryType::ToolResult,
                  display, displayLength)) {
    return fail(target, "Unable to retain tool result");
  }
  psram::release(target.fullToolResult);
  target.fullToolResult = duplicateBytes(text != nullptr ? text : "", length,
                                         kMaxToolResultBytes,
                                         &target.fullToolResultBytes);
  if (target.fullToolResult == nullptr) {
    return fail(target, "Unable to retain tool result");
  }
  target.state = RunState::RequestQueued;
  return Status::Pending;
}

Status continueTool(Session &target) {
  char *result = nullptr;
  size_t resultLength = 0;
  const Status status = executeTool(target, result, resultLength);
  if (status == Status::Pending) {
    return Status::Pending;
  }
  if (status == Status::Unsupported) {
    pushEvent(target, EventType::ToolRequested, Status::Pending,
              target.pendingToolId);
    target.state = RunState::AwaitingExternalTool;
    return Status::Pending;
  }
  if (result == nullptr) {
    result = duplicateBounded(status == Status::Denied ? "tool denied"
                                                       : "tool failed",
                              96U, &resultLength);
  }
  const Status queued = queueToolResult(target, result, resultLength,
                                        status != Status::Ok);
  psram::release(result);
  return queued;
}

char *splitTranscriptionAnswer(char *text, char *&transcript) {
  transcript = nullptr;
  if (text == nullptr) {
    return nullptr;
  }
  char *line = text;
  while (*line != '\0') {
    char *lineEnd = strchr(line, '\n');
    char *contentEnd = lineEnd != nullptr ? lineEnd : line + strlen(line);
    char *first = line;
    while (first < contentEnd && (*first == ' ' || *first == '\r' ||
                                  *first == '\t')) {
      first++;
    }
    char *last = contentEnd;
    while (last > first && (last[-1] == ' ' || last[-1] == '\r' ||
                            last[-1] == '\t')) {
      last--;
    }
    if (last - first == 3 && memcmp(first, "---", 3) == 0) {
      *line = '\0';
      char *before = line;
      while (before > text && (before[-1] == '\n' || before[-1] == '\r' ||
                               before[-1] == ' ')) {
        *--before = '\0';
      }
      transcript = text;
      char *answer = lineEnd != nullptr ? lineEnd + 1 : contentEnd;
      while (*answer == '\n' || *answer == '\r') {
        answer++;
      }
      return answer;
    }
    if (lineEnd == nullptr) {
      break;
    }
    line = lineEnd + 1;
  }
  return text;
}

bool applyInputTranscript(Session &target, const char *text, size_t length) {
  if (!target.inputWasAudio || target.voiceEntryId == 0 || text == nullptr ||
      length == 0) {
    return true;
  }
  if (!target.feed.replaceText(target.voiceEntryId, text, length)) {
    return false;
  }
  pushEvent(target, EventType::InputTranscribed, Status::Ok,
            target.voiceEntryId, FeedEntryType::User);
  return true;
}

Status parseResponse(Session &target) {
  ChatCompletion completion;
  // The streaming parser also accepts a plain completion object. Using it for
  // both modes tolerates compatible providers that ignore the stream flag.
  const bool parsed = parseStreamingChatCompletion(
      target.response, target.responseBytes, completion);
  if (!parsed) {
    logMalformedResponse(target.response, target.responseBytes);
    const char *message = completion.errorMessage != nullptr
                              ? completion.errorMessage
                              : "Malformed AI response";
    char retained[513] = {};
    copyText(retained, sizeof(retained), message);
    releaseChatCompletion(completion);
    return fail(target, retained);
  }

  if (completion.inputTranscript != nullptr &&
      !applyInputTranscript(target, completion.inputTranscript,
                            completion.inputTranscriptBytes)) {
    releaseChatCompletion(completion);
    return fail(target, "Unable to retain input transcription");
  }

  char *assistant =
      completion.content != nullptr && completion.content[0] != '\0'
          ? completion.content
          : completion.audioTranscript;
  char *fallbackTranscript = nullptr;
  if (target.transcriptionMode && target.inputWasAudio && assistant != nullptr) {
    assistant = splitTranscriptionAnswer(assistant, fallbackTranscript);
    if (completion.inputTranscript == nullptr && fallbackTranscript != nullptr &&
        !applyInputTranscript(target, fallbackTranscript,
                              strlen(fallbackTranscript))) {
      releaseChatCompletion(completion);
      return fail(target, "Unable to retain input transcription");
    }
  }

  if (assistant != nullptr && assistant[0] != '\0' &&
      !appendFeed(target, FeedEntryType::Assistant, assistant)) {
    releaseChatCompletion(completion);
    return fail(target, "Unable to retain AI response");
  }

  if (completion.toolName == nullptr) {
    if (target.voiceOutput && completion.audioData != nullptr &&
        completion.audioBytes > 0 &&
        !playPcm16VoiceOutput(completion.audioData, completion.audioBytes)) {
      appendFeed(target, FeedEntryType::Error, "Voice playback failed");
    }
    releaseChatCompletion(completion);
    completeTurn(target);
    return Status::End;
  }

  releasePendingTool(target);
  target.pendingToolWireId = completion.toolCallId;
  completion.toolCallId = nullptr;
  target.pendingToolName = completion.toolName;
  completion.toolName = nullptr;
  target.pendingToolArguments = completion.toolArguments;
  completion.toolArguments = nullptr;
  releaseChatCompletion(completion);
  uint32_t id = 2166136261UL;
  const char *idText = target.pendingToolWireId != nullptr
                           ? target.pendingToolWireId
                           : target.pendingToolName;
  for (const char *cursor = idText; *cursor != '\0'; cursor++) {
    id ^= static_cast<uint8_t>(*cursor);
    id *= 16777619UL;
  }
  if (id == 0) {
    id = target.nextToolId++;
  }
  target.pendingToolId = id;
  if (target.pendingToolWireId == nullptr) {
    char generated[24] = {};
    snprintf(generated, sizeof(generated), "call_%08lx",
             static_cast<unsigned long>(id));
    target.pendingToolWireId = duplicateBounded(generated, 23U);
    if (target.pendingToolWireId == nullptr) {
      return fail(target, "Unable to retain tool call");
    }
  }

  DynamicBuffer display(1200U);
  char path[kPathCapacity] = {};
  if (strcmp(target.pendingToolName, "read") == 0) {
    const bool hasPath = jsonArgument(target.pendingToolArguments, "path", path,
                                      sizeof(path));
    if (!display.append("read: ") ||
        !display.append(hasPath ? path : "draft.js")) {
      return fail(target, "Unable to retain tool call");
    }
  } else if (strcmp(target.pendingToolName, "write") == 0) {
    const char *content = findJsonValue(target.pendingToolArguments, "content");
    size_t lines = content != nullptr && *content == '"' ? 1U : 0U;
    bool escaped = false;
    for (const char *at = content != nullptr ? content + 1U : nullptr;
         at != nullptr && *at != '\0'; at++) {
      if (!escaped && *at == '"') {
        break;
      }
      if (!escaped && *at == '\\') {
        escaped = true;
        continue;
      }
      if ((escaped && *at == 'n') || (!escaped && *at == '\n')) {
        lines++;
      }
      escaped = false;
    }
    char summary[64] = {};
    snprintf(summary, sizeof(summary), "write: draft.js (%lu line%s)",
             static_cast<unsigned long>(lines), lines == 1U ? "" : "s");
    if (!display.append(summary)) {
      return fail(target, "Unable to retain tool call");
    }
  } else if ((strcmp(target.pendingToolName, "list") == 0 ||
              strcmp(target.pendingToolName, "search_names") == 0 ||
              strcmp(target.pendingToolName, "search_content") == 0) &&
             jsonArgument(target.pendingToolArguments, "path", path,
                          sizeof(path))) {
    if (!display.append(target.pendingToolName) || !display.append(": ") ||
        !display.append(path)) {
      return fail(target, "Unable to retain tool call");
    }
  } else if (strcmp(target.pendingToolName, "replace") == 0) {
    if (!display.append("replace: draft.js")) {
      return fail(target, "Unable to retain tool call");
    }
  } else if (!display.append(target.pendingToolName) ||
             !display.append(": ") ||
             !display.append(target.pendingToolArguments)) {
    return fail(target, "Unable to retain tool call");
  }
  if (!appendFeed(target, FeedEntryType::ToolCall, display.bytes(),
                  display.size())) {
    return fail(target, "Unable to retain tool call");
  }
  target.state = RunState::ExecutingTool;
  return continueTool(target);
}

[[maybe_unused]] Status pumpResponse(Session &target) {
  const size_t maximum =
      target.voiceOutput ? kMaxVoiceResponseBytes : kMaxResponseBytes;
  char value = 0;
  size_t readThisUpdate = 0;
  while (readThisUpdate < 2048U && target.transport.readBodyByte(value)) {
    if (target.responseBytes >= maximum ||
        !ensureResponseCapacity(target, target.responseBytes + 2U, maximum)) {
      return fail(target, target.voiceOutput
                              ? "AI voice response exceeds 2 MiB"
                              : "AI response exceeds 96 KiB");
    }
    target.response[target.responseBytes++] = value;
    target.response[target.responseBytes] = '\0';
    readThisUpdate++;
  }
  if (target.transport.timedOut(kResponseTimeoutMs)) {
    return fail(target, "AI response timeout");
  }
  const TransportStatus status = target.transport.responseStatus();
  if (status == TransportStatus::Error) {
    return fail(target, target.transport.error());
  }
  if (status != TransportStatus::End) {
    return Status::Pending;
  }
  target.transport.stop();
  return parseResponse(target);
}

const char *entryPrefix(FeedEntryType type) {
  switch (type) {
  case FeedEntryType::User:
    return "YOU: ";
  case FeedEntryType::Assistant:
    return "AI: ";
  case FeedEntryType::ToolCall:
    // Reserve the icon's 16px advance in the default six-pixel text grid.
    return "   ";
  case FeedEntryType::ToolResult:
    return "";
  case FeedEntryType::Error:
    return "ERROR: ";
  }
  return "";
}

size_t entryWrappedLines(const FeedEntryView &entry, size_t columns) {
  const size_t prefix = strlen(entryPrefix(entry.type));
  if (columns == 0) {
    return 0;
  }
  size_t lines = 1;
  size_t column = 0;
  const size_t total = prefix + entry.length;
  for (size_t i = 0; i < total; i++) {
    const char value =
        i < prefix ? entryPrefix(entry.type)[i] : entry.text[i - prefix];
    if (value == '\n') {
      lines++;
      column = 0;
    } else if (++column >= columns && i + 1U < total) {
      lines++;
      column = 0;
    }
  }
  return lines;
}

void drawEntry(Adafruit_GFX &gfx, const FeedEntryView &entry, size_t columns,
               size_t &globalLine, size_t skipLines, size_t visibleLines,
               int16_t x, int16_t y) {
  char line[64] = {};
  const size_t usableColumns = columns < sizeof(line) ? columns
                                                       : sizeof(line) - 1U;
  size_t lineLength = 0;
  const char *prefixText = entryPrefix(entry.type);
  const size_t prefix = strlen(prefixText);
  const size_t total = prefix + entry.length;
  size_t entryLine = 0;
  auto emit = [&]() {
    if (globalLine >= skipLines &&
        globalLine - skipLines < visibleLines) {
      const int16_t lineY =
          y + static_cast<int16_t>(globalLine - skipLines) * 10;
      line[lineLength] = '\0';
      const bool toolResult = entry.type == FeedEntryType::ToolResult;
      if (toolResult) {
        gfx.fillRect(x, lineY, static_cast<int16_t>(usableColumns * 6U), 10,
                     1);
        gfx.setTextColor(0);
      }
      if (entry.type == FeedEntryType::ToolCall && entryLine == 0U) {
        if (systemFontApply(gfx, "icon-8")) {
          gfx.setCursor(x, lineY + 9);
          gfx.print('l');
        }
        gfx.setFont();
        gfx.setTextSize(1);
      }
      gfx.setCursor(x, lineY);
      gfx.print(line);
      if (toolResult) {
        gfx.setTextColor(1);
      }
    }
    globalLine++;
    entryLine++;
    lineLength = 0;
  };
  for (size_t i = 0; i < total; i++) {
    const char value = i < prefix ? prefixText[i] : entry.text[i - prefix];
    if (value == '\n') {
      emit();
      continue;
    }
    line[lineLength++] = value;
    if (lineLength >= usableColumns) {
      emit();
    }
  }
  if (lineLength > 0 || total == 0) {
    emit();
  }
}

} // namespace

Status start(const char *appId, const char *appPath,
             const SessionConfig &config) {
#if !ENABLE_NETWORK_APPS
  (void)appId;
  (void)appPath;
  (void)config;
  return Status::Unsupported;
#else
  if (appId == nullptr || appId[0] == '\0' || appPath == nullptr ||
      appPath[0] == '\0' || strlen(appId) >= kAppIdCapacity ||
      strlen(appPath) >= kPathCapacity) {
    return Status::Error;
  }
  if (session != nullptr && strcmp(session->appId, appId) == 0) {
    return configure(config);
  }
  stop();
  void *memory = psram::allocate(sizeof(Session));
  if (memory == nullptr) {
    return Status::Error;
  }
  session = new (memory) Session();
  copyText(session->appId, sizeof(session->appId), appId);
  copyText(session->appPath, sizeof(session->appPath), appPath);
  const Status configured = configure(config);
  if (configured != Status::Ok) {
    stop();
    return configured;
  }
  if (pendingHandoff != nullptr &&
      strcmp(pendingHandoff->target, session->appPath) == 0) {
    char prompt[kMaxUserBytes + 1U] = {};
    copyText(prompt, sizeof(prompt), pendingHandoff->prompt);
    clearHandoff();
    const Status submitted = submitText(prompt);
    if (submitted != Status::Pending && submitted != Status::Ok) {
      appendFeed(*session, FeedEntryType::Error,
                 "Unable to start the handed-off request");
    }
  }
  return Status::Ok;
#endif
}

void stop(const char *appId) {
  if (session == nullptr ||
      (appId != nullptr && strcmp(session->appId, appId) != 0)) {
    return;
  }
  stopVoiceOutput();
  releaseTurnBuffers(*session);
  releaseCustomTools(*session);
  session->~Session();
  psram::release(session);
  session = nullptr;
}

bool activeFor(const char *appId) {
  return session != nullptr && appId != nullptr &&
         strcmp(session->appId, appId) == 0;
}

Status configure(const SessionConfig &config) {
#if !ENABLE_NETWORK_APPS
  (void)config;
  return Status::Unsupported;
#else
  if (session == nullptr || config.historyLimit < 1U ||
      config.historyLimit > kMaxHistoryTurns || config.modelIndex > 1U ||
      (config.transcriptionMode && config.voiceOutput) ||
      (config.toolPacks & ~(ToolPackCore | ToolPackDevice)) != 0U) {
    return Status::Error;
  }
  if (config.voiceOutput && !voiceOutputSupported()) {
    return Status::Unsupported;
  }
  session->toolPacks = config.toolPacks;
  session->historyEntries = config.historyLimit;
  session->modelIndex = config.modelIndex;
  session->approvals = config.approvalEnabled;
  session->transcriptionMode = config.transcriptionMode;
  session->voiceOutput = config.voiceOutput;
  copyText(session->systemPromptPath, sizeof(session->systemPromptPath),
           config.systemPromptPath != nullptr ? config.systemPromptPath :
                                                "/misc/agentfiesta/core.md");
  copyText(session->appPromptPath, sizeof(session->appPromptPath),
           config.appPromptPath);
  return Status::Ok;
#endif
}

Status submitText(const char *text) {
#if !ENABLE_NETWORK_APPS
  (void)text;
  return Status::Unsupported;
#else
  if (session == nullptr || text == nullptr || text[0] == '\0') {
    return Status::Error;
  }
  if (session->state != RunState::Idle && session->state != RunState::Failed) {
    return Status::Busy;
  }
  const size_t length = strlen(text);
  if (length > kMaxUserBytes ||
      !appendFeed(*session, FeedEntryType::User, text, length)) {
    return Status::Error;
  }
  releaseTurnBuffers(*session);
  session->inputWasAudio = false;
  session->voiceEntryId = 0;
  session->turnActive = true;
  session->state = RunState::RequestQueued;
  session->scrollLines = 0;
  pushEvent(*session, EventType::TurnStarted, Status::Pending);
  return Status::Pending;
#endif
}

Status submitWav(const uint8_t *wav, size_t wavBytes,
                 const char *optionalText) {
#if !ENABLE_NETWORK_APPS
  (void)wav;
  (void)wavBytes;
  (void)optionalText;
  return Status::Unsupported;
#else
  if (session == nullptr || wav == nullptr || wavBytes < 12U ||
      wavBytes > kMaxWavBytes || memcmp(wav, "RIFF", 4) != 0 ||
      memcmp(wav + 8U, "WAVE", 4) != 0) {
    return Status::Error;
  }
  if (session->state != RunState::Idle && session->state != RunState::Failed) {
    return Status::Busy;
  }
  uint8_t *copy = static_cast<uint8_t *>(psram::allocate(wavBytes));
  if (copy == nullptr) {
    return Status::Error;
  }
  memcpy(copy, wav, wavBytes);
  const char *text = optionalText != nullptr && optionalText[0] != '\0'
                         ? optionalText
                         : "[voice message]";
  uint32_t userEntryId = 0;
  if (strlen(text) > kMaxUserBytes ||
      !appendFeed(*session, FeedEntryType::User, text, &userEntryId)) {
    psram::release(copy);
    return Status::Error;
  }
  releaseTurnBuffers(*session);
  session->wav = copy;
  session->wavBytes = wavBytes;
  session->inputWasAudio = true;
  session->voiceEntryId = userEntryId;
  session->turnActive = true;
  session->state = RunState::RequestQueued;
  session->scrollLines = 0;
  pushEvent(*session, EventType::TurnStarted, Status::Pending);
  return Status::Pending;
#endif
}

Status update() {
#if !ENABLE_NETWORK_APPS
  return Status::Unsupported;
#else
  updateVoiceOutput();
  if (session == nullptr) {
    return Status::Error;
  }
  switch (session->state) {
  case RunState::Idle:
    return Status::End;
  case RunState::RequestQueued:
    return beginRequest(*session);
  case RunState::Receiving:
    return pumpResponse(*session);
  case RunState::ExecutingTool:
    return continueTool(*session);
  case RunState::AwaitingExternalTool:
    return Status::Pending;
  case RunState::Failed:
    return Status::Error;
  }
  return Status::Error;
#endif
}

Status pollEvent(Event &event, char *text, size_t textSize) {
#if !ENABLE_NETWORK_APPS
  (void)event;
  (void)text;
  (void)textSize;
  return Status::Unsupported;
#else
  event = {};
  if (text != nullptr && textSize > 0) {
    text[0] = '\0';
  }
  if (session == nullptr) {
    return Status::Error;
  }
  if (session->eventCount == 0) {
    return Status::End;
  }
  const EventRecord record = session->events[session->eventHead];
  session->eventHead =
      static_cast<uint8_t>((session->eventHead + 1U) % kEventCapacity);
  session->eventCount--;
  event.type = record.type;
  event.entryType = record.entryType;
  event.status = record.status;
  event.id = record.id;
  if (record.type == EventType::ToolRequested &&
      record.id == session->pendingToolId && text != nullptr && textSize > 0 &&
      session->pendingToolArguments != nullptr) {
    copyText(text, textSize, session->pendingToolArguments);
  } else if (record.id != 0 && text != nullptr && textSize > 0) {
    FeedEntryView entry;
    if (session->feed.byId(record.id, entry)) {
      const size_t amount = entry.length < textSize - 1U ? entry.length
                                                         : textSize - 1U;
      memcpy(text, entry.text, amount);
      text[amount] = '\0';
    }
  }
  return Status::Ok;
#endif
}

void cancel() {
  if (session == nullptr || session->state == RunState::Idle) {
    return;
  }
  releaseTurnBuffers(*session);
  session->turnActive = false;
  session->state = RunState::Idle;
  appendFeed(*session, FeedEntryType::Error, "Request cancelled");
  session->feed.pruneCompleted(session->historyEntries);
  pushEvent(*session, EventType::Cancelled, Status::End);
}

Status provideToolResult(uint32_t callId, const char *result, bool error) {
#if !ENABLE_NETWORK_APPS
  (void)callId;
  (void)result;
  (void)error;
  return Status::Unsupported;
#else
  if (session == nullptr || session->state != RunState::AwaitingExternalTool ||
      callId == 0 || callId != session->pendingToolId || result == nullptr) {
    return Status::Error;
  }
  const size_t length = strlen(result);
  if (length > kMaxToolResultBytes) {
    return Status::Error;
  }
  return queueToolResult(*session, result, length, error);
#endif
}

Status registerTool(const char *name, const char *description,
                    const char *parametersJson) {
#if !ENABLE_NETWORK_APPS
  (void)name;
  (void)description;
  (void)parametersJson;
  return Status::Unsupported;
#else
  if (session == nullptr ||
      (session->turnActive && session->state != RunState::RequestQueued) ||
      !safeSkillName(name) ||
      description == nullptr || strlen(description) > 256U ||
      parametersJson == nullptr || parametersJson[0] != '{' ||
      strlen(parametersJson) > 2048U || session->customToolCount >= 8U) {
    return Status::Error;
  }
  for (uint8_t i = 0; i < session->customToolCount; i++) {
    if (strcmp(session->customTools[i].name, name) == 0) {
      return Status::Error;
    }
  }
  if (session->customToolCount == session->customToolCapacity) {
    const uint8_t nextCapacity = session->customToolCapacity == 0U
                                     ? 4U
                                     : session->customToolCapacity * 2U;
    CustomTool *next = static_cast<CustomTool *>(
        psram::allocate(nextCapacity * sizeof(CustomTool)));
    if (next == nullptr) {
      return Status::Error;
    }
    for (uint8_t i = 0; i < nextCapacity; i++) {
      new (&next[i]) CustomTool();
    }
    for (uint8_t i = 0; i < session->customToolCount; i++) {
      next[i] = session->customTools[i];
    }
    for (uint8_t i = 0; i < session->customToolCapacity; i++) {
      session->customTools[i].~CustomTool();
    }
    psram::release(session->customTools);
    session->customTools = next;
    session->customToolCapacity = nextCapacity;
  }
  CustomTool tool;
  tool.name = duplicateBounded(name, 48U);
  tool.description = duplicateBounded(description, 256U);
  tool.parameters = duplicateBounded(parametersJson, 2048U);
  if (tool.name == nullptr || tool.description == nullptr ||
      tool.parameters == nullptr) {
    psram::release(tool.name);
    psram::release(tool.description);
    psram::release(tool.parameters);
    return Status::Error;
  }
  session->customTools[session->customToolCount++] = tool;
  return Status::Ok;
#endif
}

Status pendingToolName(uint32_t callId, char *out, size_t outSize) {
#if !ENABLE_NETWORK_APPS
  (void)callId;
  (void)out;
  (void)outSize;
  return Status::Unsupported;
#else
  if (session == nullptr || callId == 0 || callId != session->pendingToolId ||
      session->pendingToolName == nullptr || out == nullptr || outSize == 0 ||
      strlen(session->pendingToolName) >= outSize) {
    return Status::Error;
  }
  copyText(out, outSize, session->pendingToolName);
  return Status::Ok;
#endif
}

void drawFeed(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t width,
              int16_t height) {
  gfx.drawRect(x, y, width, height, 1);
  if (session == nullptr || width < 16 || height < 16) {
    return;
  }
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  size_t columns = static_cast<size_t>((width - 8) / 6);
  if (columns > 63U) {
    columns = 63U;
  }
  const size_t visibleLines = static_cast<size_t>((height - 8) / 10);
  size_t totalLines = 0;
  for (size_t i = 0; i < session->feed.count(); i++) {
    FeedEntryView entry;
    if (session->feed.at(i, entry)) {
      totalLines += entryWrappedLines(entry, columns);
    }
  }
  const size_t linesAboveTail =
      totalLines > visibleLines ? totalLines - visibleLines : 0U;
  if (session->scrollLines > linesAboveTail) {
    session->scrollLines = static_cast<uint8_t>(
        linesAboveTail < UINT8_MAX ? linesAboveTail : UINT8_MAX);
  }
  const size_t skip = linesAboveTail - session->scrollLines;
  size_t line = 0;
  for (size_t i = 0; i < session->feed.count(); i++) {
    FeedEntryView entry;
    if (session->feed.at(i, entry)) {
      drawEntry(gfx, entry, columns, line, skip, visibleLines, x + 4, y + 4);
    }
  }
  if (session->scrollLines < linesAboveTail) {
    gfx.setCursor(x + width - 8, y + 2);
    gfx.print("^");
  }
  if (session->scrollLines > 0U) {
    gfx.setCursor(x + width - 8, y + height - 10);
    gfx.print("v");
  }
}

void scrollFeed(int8_t wrappedLines) {
  if (session == nullptr) {
    return;
  }
  int next = static_cast<int>(session->scrollLines) + wrappedLines;
  if (next < 0) {
    next = 0;
  } else if (next > UINT8_MAX) {
    next = UINT8_MAX;
  }
  session->scrollLines = static_cast<uint8_t>(next);
}

uint8_t historyLimit() {
  return session != nullptr ? session->historyEntries : 0U;
}

Status setHistoryLimit(uint8_t entries) {
#if !ENABLE_NETWORK_APPS
  (void)entries;
  return Status::Unsupported;
#else
  if (session == nullptr || entries < 1U || entries > kMaxHistoryTurns) {
    return Status::Error;
  }
  session->historyEntries = entries;
  if (!session->turnActive) {
    session->feed.pruneCompleted(entries);
  }
  return Status::Ok;
#endif
}

bool approvalEnabled() { return session == nullptr || session->approvals; }

void setApprovalEnabled(bool enabled) {
  if (session != nullptr) {
    session->approvals = enabled;
  }
}

bool voiceOutputSupported() {
#if ENABLE_NETWORK_APPS
  return true;
#else
  return false;
#endif
}

bool playVoiceOutput(const uint8_t *audio, size_t audioBytes) {
#if !ENABLE_NETWORK_APPS
  (void)audio;
  (void)audioBytes;
  return false;
#else
  if (audio == nullptr || audioBytes == 0 || !voiceOutputSupported()) {
    return false;
  }
  const bool wav = audioBytes >= 12U && memcmp(audio, "RIFF", 4) == 0 &&
                   memcmp(audio + 8U, "WAVE", 4) == 0;
  stopVoiceOutput();
  if (wav) {
    return startWavVoicePlayer(audio, audioBytes);
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  if (!Audio::codecAvailable(AudioCodec::Mp3)) {
    return false;
  }
  void *memory = psram::allocate(sizeof(Audio));
  if (memory == nullptr) {
    return false;
  }
  compressedVoicePlayer =
      new (memory) Audio(audio, audioBytes, AudioCodec::Mp3);
  if (!compressedVoicePlayer->preload() || !compressedVoicePlayer->play()) {
    stopVoiceOutput();
    return false;
  }
  return true;
#else
  return false;
#endif
#endif
}

bool playPcm16VoiceOutput(const uint8_t *audio, size_t audioBytes) {
#if !ENABLE_NETWORK_APPS
  (void)audio;
  (void)audioBytes;
  return false;
#else
  if (audio == nullptr || audioBytes < sizeof(int16_t) ||
      audioBytes % sizeof(int16_t) != 0U || !voiceOutputSupported()) {
    return false;
  }
  stopVoiceOutput();
  return startPcmVoicePlayer(audio, audioBytes, kVoicePcmSampleRate, 1U);
#endif
}

void updateVoiceOutput() {
#if ENABLE_NETWORK_APPS
  if (pcmVoicePlayer != nullptr && !pcmVoicePlayer->renderer.active()) {
    stopVoiceOutput();
    return;
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  if (compressedVoicePlayer != nullptr) {
    compressedVoicePlayer->update();
    if (compressedVoicePlayer->finished()) {
      stopVoiceOutput();
    }
  }
#endif
#endif
}

void stopVoiceOutput() {
#if ENABLE_NETWORK_APPS
  if (pcmVoicePlayer != nullptr) {
    pcmVoicePlayer->renderer.stop();
    psram::release(pcmVoicePlayer->pcm);
    pcmVoicePlayer->pcm = nullptr;
    pcmVoicePlayer->~PcmVoicePlayer();
    psram::release(pcmVoicePlayer);
    pcmVoicePlayer = nullptr;
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  if (compressedVoicePlayer != nullptr) {
    compressedVoicePlayer->~Audio();
    psram::release(compressedVoicePlayer);
    compressedVoicePlayer = nullptr;
  }
#endif
#endif
}

size_t snapshotSize() {
  if (session == nullptr || session->turnActive) {
    return 0;
  }
  size_t size = sizeof(SnapshotHeader);
  for (size_t i = 0; i < session->feed.count(); i++) {
    FeedEntryView entry;
    if (!session->feed.at(i, entry) ||
        entry.length > SIZE_MAX - size - sizeof(SnapshotEntryHeader)) {
      return 0;
    }
    size += sizeof(SnapshotEntryHeader) + entry.length;
  }
  return size;
}

Status saveSnapshot(uint8_t *out, size_t outSize, size_t &written) {
#if !ENABLE_NETWORK_APPS
  (void)out;
  (void)outSize;
  written = 0;
  return Status::Unsupported;
#else
  written = 0;
  const size_t required = snapshotSize();
  if (required == 0 || out == nullptr || outSize < required ||
      session->feed.count() > UINT16_MAX) {
    return Status::Error;
  }
  SnapshotHeader header;
  header.totalSize = static_cast<uint32_t>(required);
  header.toolPacks = session->toolPacks;
  header.entryCount = static_cast<uint16_t>(session->feed.count());
  header.historyLimit = session->historyEntries;
  header.modelIndex = session->modelIndex;
  header.approvalEnabled = session->approvals ? 1U : 0U;
  header.scrollLines = session->scrollLines;
  memcpy(out, &header, sizeof(header));
  size_t offset = sizeof(header);
  for (size_t i = 0; i < session->feed.count(); i++) {
    FeedEntryView entry;
    session->feed.at(i, entry);
    SnapshotEntryHeader entryHeader;
    entryHeader.length = static_cast<uint16_t>(entry.length);
    entryHeader.type = static_cast<uint8_t>(entry.type);
    memcpy(out + offset, &entryHeader, sizeof(entryHeader));
    offset += sizeof(entryHeader);
    memcpy(out + offset, entry.text, entry.length);
    offset += entry.length;
  }
  written = offset;
  return Status::Ok;
#endif
}

Status restoreSnapshot(const uint8_t *data, size_t size) {
#if !ENABLE_NETWORK_APPS
  (void)data;
  (void)size;
  return Status::Unsupported;
#else
  if (session == nullptr || session->turnActive || data == nullptr ||
      size < sizeof(SnapshotHeader)) {
    return Status::Error;
  }
  SnapshotHeader header;
  memcpy(&header, data, sizeof(header));
  if (header.magic != kSnapshotMagic || header.version != kSnapshotVersion ||
      header.headerSize != sizeof(SnapshotHeader) || header.totalSize != size ||
      header.entryCount > 512U ||
      header.historyLimit < 1U ||
      header.historyLimit > kMaxHistoryTurns ||
      header.modelIndex > 1U || header.approvalEnabled > 1U ||
      header.reserved[0] != 0U || header.reserved[1] != 0U ||
      (header.toolPacks & ~(ToolPackCore | ToolPackDevice)) != 0U) {
    return Status::Error;
  }
  FeedStore restored;
  size_t offset = sizeof(header);
  size_t restoredBytes = 0;
  for (uint16_t i = 0; i < header.entryCount; i++) {
    if (offset > size || size - offset < sizeof(SnapshotEntryHeader)) {
      return Status::Error;
    }
    SnapshotEntryHeader entryHeader;
    memcpy(&entryHeader, data + offset, sizeof(entryHeader));
    offset += sizeof(entryHeader);
    if (entryHeader.type < static_cast<uint8_t>(FeedEntryType::User) ||
        entryHeader.type > static_cast<uint8_t>(FeedEntryType::Error) ||
        entryHeader.reserved != 0U || entryHeader.length > size - offset) {
      return Status::Error;
    }
    size_t entryLimit = FeedStore::kMaxAssistantBytes;
    if (entryHeader.type ==
        static_cast<uint8_t>(FeedEntryType::ToolResult)) {
      entryLimit = FeedStore::kMaxDisplayedToolResultBytes;
    } else if (entryHeader.type ==
               static_cast<uint8_t>(FeedEntryType::ToolCall)) {
      entryLimit = 1024U;
    }
    if (entryHeader.length > entryLimit ||
        entryHeader.length > FeedStore::kMaxRetainedBytes - restoredBytes) {
      return Status::Error;
    }
    restoredBytes += entryHeader.length;
    uint32_t id = 0;
    if (!restored.append(static_cast<FeedEntryType>(entryHeader.type),
                         reinterpret_cast<const char *>(data + offset),
                         entryHeader.length, id)) {
      return Status::Error;
    }
    offset += entryHeader.length;
  }
  if (offset != size) {
    return Status::Error;
  }
  session->feed.swap(restored);
  session->toolPacks = header.toolPacks;
  session->historyEntries = header.historyLimit;
  session->modelIndex = header.modelIndex;
  session->approvals = header.approvalEnabled != 0;
  session->scrollLines = header.scrollLines;
  return Status::Ok;
#endif
}

Status handoff(const char *targetAppPath, const char *prompt) {
#if !ENABLE_NETWORK_APPS
  (void)targetAppPath;
  (void)prompt;
  return Status::Unsupported;
#else
  if (targetAppPath == nullptr || targetAppPath[0] != '/' || prompt == nullptr ||
      prompt[0] == '\0' || strlen(targetAppPath) >= kPathCapacity ||
      strlen(prompt) > kMaxUserBytes) {
    return Status::Error;
  }
  if (pendingHandoff == nullptr) {
    pendingHandoff = static_cast<Handoff *>(psram::allocate(sizeof(Handoff)));
    if (pendingHandoff == nullptr) {
      return Status::Error;
    }
    new (pendingHandoff) Handoff();
  }
  copyText(pendingHandoff->target, sizeof(pendingHandoff->target), targetAppPath);
  copyText(pendingHandoff->prompt, sizeof(pendingHandoff->prompt), prompt);
  return Status::Ok;
#endif
}

bool handoffPending() { return pendingHandoff != nullptr; }

void failHandoff(const char *detail) {
  if (pendingHandoff == nullptr) {
    return;
  }
  clearHandoff();
  if (session == nullptr) {
    return;
  }
  releaseTurnBuffers(*session);
  session->turnActive = false;
  session->state = RunState::Idle;
  appendFeed(*session, FeedEntryType::Error,
             detail != nullptr && detail[0] != '\0'
                 ? detail
                 : "Agent handoff target is not installed");
  session->feed.pruneCompleted(session->historyEntries);
  pushEvent(*session, EventType::Error, Status::Error);
}

void clearHandoff() {
  if (pendingHandoff == nullptr) {
    return;
  }
  pendingHandoff->~Handoff();
  psram::release(pendingHandoff);
  pendingHandoff = nullptr;
}

size_t feedEntryCount() { return session != nullptr ? session->feed.count() : 0U; }

bool feedEntryAt(size_t index, FeedEntryView &out) {
  return session != nullptr && session->feed.at(index, out);
}

} // namespace agentfiesta
