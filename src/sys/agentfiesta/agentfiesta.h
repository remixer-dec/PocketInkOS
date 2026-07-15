/// System-level AgentFiesta SDK. Sessions and all bulk state are allocated on
/// demand in PSRAM and released when the owning app stops.
#ifndef SYS_AGENTFIESTA_AGENTFIESTA_H
#define SYS_AGENTFIESTA_AGENTFIESTA_H

#include "sys/agentfiesta/feed_store.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

namespace agentfiesta {

static constexpr uint16_t kApiVersion = 1;
static constexpr uint32_t kMaxVoiceDurationMs = 60000;
static constexpr const char kTranscriptionInstruction[] =
    "For audio input: first transcribe the audio, then write --- on its own "
    "line, then respond.";

enum class Status : int8_t {
  Error = -1,
  Denied = -2,
  Unsupported = -3,
  Pending = 0,
  Ok = 1,
  End = 2,
  Busy = 3,
};

enum ToolPack : uint32_t {
  ToolPackCore = 1U << 0U,
  ToolPackDevice = 1U << 1U,
};

struct SessionConfig {
  uint32_t toolPacks = ToolPackCore;
  const char *systemPromptPath = "/misc/agentfiesta/core.md";
  const char *appPromptPath = nullptr;
  uint8_t historyLimit = 3;
  uint8_t modelIndex = 0;
  bool approvalEnabled = true;
  bool transcriptionMode = false;
  bool voiceOutput = false;
};

enum class EventType : uint8_t {
  FeedEntry = 1,
  TurnStarted = 2,
  ToolRequested = 3,
  ApprovalPending = 4,
  TurnComplete = 5,
  HandoffReady = 6,
  Cancelled = 7,
  Error = 8,
  InputTranscribed = 9,
};

struct Event {
  EventType type = EventType::Error;
  FeedEntryType entryType = FeedEntryType::Error;
  Status status = Status::Error;
  uint32_t id = 0;
};

Status start(const char *appId, const char *appPath,
             const SessionConfig &config);
void stop(const char *appId = nullptr);
bool activeFor(const char *appId);
Status configure(const SessionConfig &config);

Status submitText(const char *text);
/// Copies a bounded WAV payload. Capture duration is enforced by the external
/// app audio service at 60 seconds; this validates the resulting payload size.
Status submitWav(const uint8_t *wav, size_t wavBytes,
                 const char *optionalText = nullptr);
Status update();
Status pollEvent(Event &event, char *text, size_t textSize);
void cancel();
Status provideToolResult(uint32_t callId, const char *result, bool error);
/// Registers one external tool schema for the active app. Unknown tool calls
/// are surfaced as ToolRequested events and completed with provideToolResult.
Status registerTool(const char *name, const char *description,
                    const char *parametersJson);
/// Copies the pending custom-tool name for the matching call id.
Status pendingToolName(uint32_t callId, char *out, size_t outSize);

void drawFeed(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t width,
              int16_t height);
void scrollFeed(int8_t wrappedLines);
uint8_t historyLimit();
Status setHistoryLimit(uint8_t entries);
bool approvalEnabled();
void setApprovalEnabled(bool enabled);

/// Returns whether this firmware can decode supported response-audio formats.
bool voiceOutputSupported();
/// Starts system playback from copied MP3 or RIFF/WAV response bytes.
bool playVoiceOutput(const uint8_t *audio, size_t audioBytes);
/// Starts system playback from copied raw 24 kHz mono PCM16 response bytes.
bool playPcm16VoiceOutput(const uint8_t *audio, size_t audioBytes);
/// Reaps naturally completed playback; safe to call from any app update loop.
void updateVoiceOutput();
void stopVoiceOutput();

size_t snapshotSize();
Status saveSnapshot(uint8_t *out, size_t outSize, size_t &written);
Status restoreSnapshot(const uint8_t *data, size_t size);

/// Stores a bounded prompt for a future AgentFiesta app. The target consumes it
/// automatically on start when its app path matches.
Status handoff(const char *targetAppPath, const char *prompt);
bool handoffPending();
/// Converts a failed shell launch into a normal feed error and drops the
/// pending handoff so a later matching app cannot consume stale context.
void failHandoff(const char *detail);
void clearHandoff();

/// Introspection used by focused host tests and future app wrappers.
size_t feedEntryCount();
bool feedEntryAt(size_t index, FeedEntryView &out);

} // namespace agentfiesta

#endif
