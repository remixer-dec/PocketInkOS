/// Versioned C-compatible AgentFiesta table appended to the .pink host ABI.
#ifndef SYS_AGENTFIESTA_PINK_API_H
#define SYS_AGENTFIESTA_PINK_API_H

#include <stddef.h>
#include <stdint.h>

static const uint16_t PINK_AGENTFIESTA_API_VERSION = 1;
static const uint32_t PINK_AGENTFIESTA_MAX_VOICE_MS = 60000U;

enum PinkAgentFiestaStatus : int8_t {
  PINK_AGENTFIESTA_ERROR = -1,
  PINK_AGENTFIESTA_DENIED = -2,
  PINK_AGENTFIESTA_UNSUPPORTED = -3,
  PINK_AGENTFIESTA_PENDING = 0,
  PINK_AGENTFIESTA_OK = 1,
  PINK_AGENTFIESTA_END = 2,
  PINK_AGENTFIESTA_BUSY = 3,
};

enum PinkAgentFiestaToolPack : uint32_t {
  PINK_AGENTFIESTA_TOOL_CORE = 1U << 0U,
  PINK_AGENTFIESTA_TOOL_DEVICE = 1U << 1U,
};

enum PinkAgentFiestaEventType : uint8_t {
  PINK_AGENTFIESTA_EVENT_FEED_ENTRY = 1,
  PINK_AGENTFIESTA_EVENT_TURN_STARTED = 2,
  PINK_AGENTFIESTA_EVENT_TOOL_REQUESTED = 3,
  PINK_AGENTFIESTA_EVENT_APPROVAL_PENDING = 4,
  PINK_AGENTFIESTA_EVENT_TURN_COMPLETE = 5,
  PINK_AGENTFIESTA_EVENT_HANDOFF_READY = 6,
  PINK_AGENTFIESTA_EVENT_CANCELLED = 7,
  PINK_AGENTFIESTA_EVENT_ERROR = 8,
  PINK_AGENTFIESTA_EVENT_INPUT_TRANSCRIBED = 9,
};

enum PinkAgentFiestaOption : uint8_t {
  PINK_AGENTFIESTA_TRANSCRIPTION = 1U << 0U,
  PINK_AGENTFIESTA_VOICE_OUTPUT = 1U << 1U,
};

enum PinkAgentFiestaFeedEntryType : uint8_t {
  PINK_AGENTFIESTA_FEED_USER = 1,
  PINK_AGENTFIESTA_FEED_ASSISTANT = 2,
  PINK_AGENTFIESTA_FEED_TOOL_CALL = 3,
  PINK_AGENTFIESTA_FEED_TOOL_RESULT = 4,
  PINK_AGENTFIESTA_FEED_ERROR = 5,
};

struct PinkAgentFiestaConfig {
  uint16_t structSize = sizeof(PinkAgentFiestaConfig);
  uint16_t apiVersion = PINK_AGENTFIESTA_API_VERSION;
  uint32_t toolPacks = PINK_AGENTFIESTA_TOOL_CORE;
  const char *systemPromptPath = "/misc/agentfiesta/core.md";
  const char *appPromptPath = nullptr;
  uint8_t historyLimit = 3;
  uint8_t modelIndex = 0;
  uint8_t approvalEnabled = 1;
  uint8_t options = 0;
};

struct PinkAgentFiestaEvent {
  uint16_t structSize = sizeof(PinkAgentFiestaEvent);
  uint8_t type = 0;
  uint8_t entryType = 0;
  int8_t status = PINK_AGENTFIESTA_ERROR;
  uint8_t reserved[3] = {};
  uint32_t id = 0;
};

struct PinkAgentFiestaApi {
  uint16_t apiVersion;
  uint16_t reserved;
  size_t apiSize;
  uint32_t maxVoiceDurationMs;
  int8_t (*sessionStart)(const PinkAgentFiestaConfig *config);
  void (*sessionStop)();
  int8_t (*sessionConfigure)(const PinkAgentFiestaConfig *config);
  int8_t (*submitText)(const char *text);
  int8_t (*submitWav)(const uint8_t *wav, size_t wavBytes,
                      const char *optionalText);
  int8_t (*update)();
  int8_t (*pollEvent)(PinkAgentFiestaEvent *event, char *text,
                      size_t textSize);
  void (*cancel)();
  int8_t (*toolResult)(uint32_t callId, const char *result, bool error);
  void (*drawFeed)(int16_t x, int16_t y, int16_t width, int16_t height);
  void (*scrollFeed)(int8_t wrappedLines);
  uint8_t (*historyLimit)();
  int8_t (*setHistoryLimit)(uint8_t entries);
  bool (*approvalEnabled)();
  void (*setApprovalEnabled)(bool enabled);
  size_t (*snapshotSize)();
  int8_t (*snapshotSave)(uint8_t *out, size_t outSize, size_t *written);
  int8_t (*snapshotRestore)(const uint8_t *data, size_t size);
  int8_t (*handoff)(const char *targetAppPath, const char *prompt);
  int8_t (*registerTool)(const char *name, const char *description,
                         const char *parametersJson);
  int8_t (*pendingToolName)(uint32_t callId, char *out, size_t outSize);
};

#endif
