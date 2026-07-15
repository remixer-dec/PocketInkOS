/// Bounded parser for the OpenAI-compatible chat-completion response shape.
#ifndef SYS_AGENTFIESTA_CHAT_COMPLETION_H
#define SYS_AGENTFIESTA_CHAT_COMPLETION_H

#include <stddef.h>
#include <stdint.h>

namespace agentfiesta {

struct ChatCompletion {
  char *content = nullptr;
  size_t contentBytes = 0;
  char *inputTranscript = nullptr;
  size_t inputTranscriptBytes = 0;
  char *audioTranscript = nullptr;
  size_t audioTranscriptBytes = 0;
  char *toolCallId = nullptr;
  char *toolName = nullptr;
  char *toolArguments = nullptr;
  char *errorMessage = nullptr;

  /// Points into the caller-owned response buffer after in-place base64 decode.
  uint8_t *audioData = nullptr;
  size_t audioBytes = 0;
};

/// Parses the first choice without inspecting model names or provider metadata.
/// Optional audio and input-transcript fields are ignored when absent. The JSON
/// buffer must stay alive while audioData is used.
bool parseChatCompletion(char *json, size_t jsonBytes, ChatCompletion &out);

/// Parses OpenAI-compatible server-sent chat-completion deltas. Text, audio,
/// and first-tool-call fragments are joined into the same result shape as a
/// non-streaming completion. A plain JSON response is accepted as a fallback.
bool parseStreamingChatCompletion(char *events, size_t eventBytes,
                                  ChatCompletion &out);

/// Releases all strings allocated by either completion parser.
void releaseChatCompletion(ChatCompletion &completion);

} // namespace agentfiesta

#endif
