#include "sys/agentfiesta/chat_completion.h"

#include "sys/psram_allocator.h"

#include <cstring>

namespace agentfiesta {
namespace {

struct Span {
  char *begin = nullptr;
  char *end = nullptr;
};

class TextAccumulator {
public:
  TextAccumulator() = default;
  ~TextAccumulator() { psram::release(data); }

  TextAccumulator(const TextAccumulator &) = delete;
  TextAccumulator &operator=(const TextAccumulator &) = delete;

  bool append(const Span &span, size_t maximum);

  char *take(size_t &outBytes) {
    char *result = data;
    outBytes = used;
    data = nullptr;
    used = 0;
    capacity = 0;
    return result;
  }

private:
  char *data = nullptr;
  size_t used = 0;
  size_t capacity = 0;
};

char *skipWhitespace(char *cursor, char *end) {
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                          *cursor == '\r' || *cursor == '\n')) {
    cursor++;
  }
  return cursor;
}

char *skipJsonPrefix(char *cursor, char *end) {
  cursor = skipWhitespace(cursor, end);
  if (end - cursor >= 3 &&
      static_cast<uint8_t>(cursor[0]) == 0xefU &&
      static_cast<uint8_t>(cursor[1]) == 0xbbU &&
      static_cast<uint8_t>(cursor[2]) == 0xbfU) {
    cursor = skipWhitespace(cursor + 3, end);
  }
  return cursor;
}

char *afterString(char *start, char *end) {
  if (start >= end || *start != '"') {
    return nullptr;
  }
  bool escaped = false;
  for (char *cursor = start + 1; cursor < end; cursor++) {
    if (!escaped && *cursor == '"') {
      return cursor + 1;
    }
    if (!escaped && *cursor == '\\') {
      escaped = true;
    } else {
      escaped = false;
    }
  }
  return nullptr;
}

char *afterValue(char *start, char *end) {
  start = skipWhitespace(start, end);
  if (start >= end) {
    return nullptr;
  }
  if (*start == '"') {
    return afterString(start, end);
  }
  if (*start != '{' && *start != '[') {
    char *cursor = start;
    while (cursor < end && *cursor != ',' && *cursor != '}' &&
           *cursor != ']') {
      cursor++;
    }
    return cursor;
  }

  const char opening = *start;
  const char closing = opening == '{' ? '}' : ']';
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (char *cursor = start; cursor < end; cursor++) {
    const char value = *cursor;
    if (inString) {
      if (!escaped && value == '"') {
        inString = false;
      }
      if (!escaped && value == '\\') {
        escaped = true;
      } else {
        escaped = false;
      }
      continue;
    }
    if (value == '"') {
      inString = true;
    } else if (value == opening) {
      depth++;
    } else if (value == closing && --depth == 0) {
      return cursor + 1;
    }
  }
  return nullptr;
}

bool rawStringEquals(const Span &span, const char *text) {
  if (span.begin == nullptr || span.end == nullptr || text == nullptr ||
      span.end < span.begin + 2 || *span.begin != '"' ||
      span.end[-1] != '"') {
    return false;
  }
  const size_t length = static_cast<size_t>(span.end - span.begin - 2);
  return strlen(text) == length && memcmp(span.begin + 1, text, length) == 0;
}

bool objectMember(const Span &object, const char *name, Span &value) {
  value = {};
  if (object.begin == nullptr || object.end == nullptr ||
      object.end <= object.begin || *object.begin != '{') {
    return false;
  }
  char *cursor = object.begin + 1;
  while (cursor < object.end) {
    cursor = skipWhitespace(cursor, object.end);
    if (cursor < object.end && *cursor == ',') {
      cursor = skipWhitespace(cursor + 1, object.end);
    }
    if (cursor >= object.end || *cursor == '}') {
      return false;
    }
    char *keyEnd = afterString(cursor, object.end);
    if (keyEnd == nullptr) {
      return false;
    }
    const Span key{cursor, keyEnd};
    cursor = skipWhitespace(keyEnd, object.end);
    if (cursor >= object.end || *cursor != ':') {
      return false;
    }
    char *valueStart = skipWhitespace(cursor + 1, object.end);
    char *valueEnd = afterValue(valueStart, object.end);
    if (valueEnd == nullptr) {
      return false;
    }
    if (rawStringEquals(key, name)) {
      value = {valueStart, valueEnd};
      return true;
    }
    cursor = valueEnd;
  }
  return false;
}

bool firstArrayValue(const Span &array, Span &value) {
  value = {};
  if (array.begin == nullptr || array.end == nullptr ||
      array.end <= array.begin || *array.begin != '[') {
    return false;
  }
  char *start = skipWhitespace(array.begin + 1, array.end);
  if (start >= array.end || *start == ']') {
    return false;
  }
  char *end = afterValue(start, array.end);
  if (end == nullptr) {
    return false;
  }
  value = {start, end};
  return true;
}

bool decodedStringLength(const Span &span, size_t maximum, size_t &length) {
  length = 0;
  if (span.begin == nullptr || span.end == nullptr ||
      span.end <= span.begin + 1 || *span.begin != '"' ||
      span.end[-1] != '"') {
    return false;
  }
  bool escaped = false;
  for (char *cursor = span.begin + 1; cursor < span.end - 1; cursor++) {
    if (!escaped && *cursor == '\\') {
      escaped = true;
      continue;
    }
    if (escaped && *cursor == 'u') {
      if (span.end - cursor <= 4) {
        return false;
      }
      cursor += 4;
    }
    escaped = false;
    if (++length > maximum) {
      return false;
    }
  }
  return !escaped;
}

void decodeStringInto(const Span &span, char *decoded) {
  size_t used = 0;
  for (char *cursor = span.begin + 1; cursor < span.end - 1;) {
    char value = *cursor++;
    if (value == '\\' && cursor < span.end - 1) {
      value = *cursor++;
      if (value == 'n' || value == 'r' || value == 't') {
        value = value == 'n' ? '\n' : value == 'r' ? '\r' : '\t';
      } else if (value == 'u') {
        for (uint8_t i = 0; i < 4U && cursor < span.end - 1; i++) {
          cursor++;
        }
        value = '?';
      }
    }
    decoded[used++] = value;
  }
  decoded[used] = '\0';
}

char *decodeString(const Span &span, size_t maximum, size_t &length) {
  if (!decodedStringLength(span, maximum, length)) {
    return nullptr;
  }
  char *decoded = static_cast<char *>(psram::allocate(length + 1U));
  if (decoded == nullptr) {
    return nullptr;
  }
  decodeStringInto(span, decoded);
  return decoded;
}

bool TextAccumulator::append(const Span &span, size_t maximum) {
  size_t fragmentBytes = 0;
  if (used > maximum ||
      !decodedStringLength(span, maximum - used, fragmentBytes)) {
    return false;
  }
  const size_t needed = used + fragmentBytes + 1U;
  if (needed > capacity) {
    size_t next = capacity > 0 ? capacity : 64U;
    const size_t limit = maximum + 1U;
    while (next < needed) {
      if (next >= limit / 2U) {
        next = limit;
        break;
      }
      next *= 2U;
    }
    void *resized = psram::reallocate(data, next);
    if (resized == nullptr) {
      return false;
    }
    data = static_cast<char *>(resized);
    capacity = next;
  }
  decodeStringInto(span, data + used);
  used += fragmentBytes;
  return true;
}

bool decodeOptionalString(const Span &object, const char *name,
                          size_t maximum, char *&out, size_t &length) {
  Span value;
  if (!objectMember(object, name, value) || value.begin == nullptr ||
      value.begin >= value.end || *value.begin != '"') {
    return true;
  }
  out = decodeString(value, maximum, length);
  return out != nullptr;
}

int base64Value(char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  return value == '+' ? 62 : value == '/' ? 63 : -1;
}

bool decodeBase64RangeInPlace(char *source, char *sourceEnd,
                              bool allowEscapedSlash, uint8_t *&data,
                              size_t &bytes) {
  data = nullptr;
  bytes = 0;
  if (source == nullptr || sourceEnd == nullptr || source >= sourceEnd) {
    return false;
  }
  uint8_t *destination = reinterpret_cast<uint8_t *>(source);
  size_t written = 0;
  int quartet[4] = {};
  uint8_t used = 0;
  bool padded = false;
  for (char *cursor = source; cursor < sourceEnd; cursor++) {
    char value = *cursor;
    if (allowEscapedSlash && value == '\\' && cursor + 1 < sourceEnd &&
        cursor[1] == '/') {
      value = *++cursor;
    }
    if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
      continue;
    }
    if (padded) {
      return false;
    }
    quartet[used++] = value == '=' ? -2 : base64Value(value);
    if (quartet[used - 1U] < -1) {
      // Padding is validated when the quartet is complete.
    } else if (quartet[used - 1U] < 0) {
      return false;
    }
    if (used < 4U) {
      continue;
    }
    if (quartet[0] < 0 || quartet[1] < 0 ||
        (quartet[2] == -2 && quartet[3] != -2)) {
      return false;
    }
    destination[written++] = static_cast<uint8_t>(
        (quartet[0] << 2) | (quartet[1] >> 4));
    if (quartet[2] != -2) {
      destination[written++] = static_cast<uint8_t>(
          (quartet[1] << 4) | (quartet[2] >> 2));
      if (quartet[3] != -2) {
        destination[written++] = static_cast<uint8_t>(
            (quartet[2] << 6) | quartet[3]);
      } else {
        padded = true;
      }
    } else {
      padded = true;
    }
    used = 0;
  }
  if (used != 0) {
    return false;
  }
  data = destination;
  bytes = written;
  return written > 0;
}

bool decodeBase64InPlace(const Span &span, uint8_t *&data, size_t &bytes) {
  if (span.begin == nullptr || span.end == nullptr ||
      span.end <= span.begin + 1 || *span.begin != '"' ||
      span.end[-1] != '"') {
    data = nullptr;
    bytes = 0;
    return false;
  }
  return decodeBase64RangeInPlace(span.begin + 1, span.end - 1, true, data,
                                  bytes);
}

bool appendBase64Fragment(const Span &span, char *destination,
                          size_t destinationSize, size_t &used) {
  if (span.begin == nullptr || span.end == nullptr ||
      span.end <= span.begin + 1 || *span.begin != '"' ||
      span.end[-1] != '"') {
    return false;
  }
  for (char *cursor = span.begin + 1; cursor < span.end - 1; cursor++) {
    char value = *cursor;
    if (value == '\\') {
      if (cursor + 1 >= span.end - 1 || cursor[1] != '/') {
        return false;
      }
      value = *++cursor;
    }
    if (used >= destinationSize) {
      return false;
    }
    destination[used++] = value;
  }
  return true;
}

bool decodeCompactedBase64(char *encoded, size_t encodedBytes,
                           uint8_t *&data, size_t &bytes) {
  return encoded != nullptr && encodedBytes > 0 &&
         decodeBase64RangeInPlace(encoded, encoded + encodedBytes, false,
                                  data, bytes);
}

bool appendOptionalFragment(const Span &object, const char *name,
                            TextAccumulator &out, size_t maximum) {
  Span value;
  return !objectMember(object, name, value) || value.begin == nullptr ||
         value.begin >= value.end || *value.begin != '"' ||
         out.append(value, maximum);
}

} // namespace

bool parseChatCompletion(char *json, size_t jsonBytes, ChatCompletion &out) {
  releaseChatCompletion(out);
  if (json == nullptr || jsonBytes == 0) {
    return false;
  }
  char *end = json + jsonBytes;
  char *rootStart = skipJsonPrefix(json, end);
  char *rootEnd = afterValue(rootStart, end);
  if (rootEnd == nullptr || skipWhitespace(rootEnd, end) != end) {
    return false;
  }
  const Span root{rootStart, rootEnd};
  Span choices;
  Span firstChoice;
  Span message;
  if (!objectMember(root, "choices", choices) ||
      !firstArrayValue(choices, firstChoice) ||
      !objectMember(firstChoice, "message", message)) {
    Span error;
    Span errorMessage;
    size_t ignored = 0;
    if (objectMember(root, "error", error) &&
        objectMember(error, "message", errorMessage)) {
      out.errorMessage = decodeString(errorMessage, 512U, ignored);
    }
    return false;
  }

  if (!decodeOptionalString(message, "content", 96U * 1024U, out.content,
                            out.contentBytes)) {
    releaseChatCompletion(out);
    return false;
  }

  Span metadata;
  Span transcripts;
  Span firstTranscript;
  if (objectMember(root, "history_metadata", metadata) &&
      objectMember(metadata, "input_audio_transcripts", transcripts) &&
      firstArrayValue(transcripts, firstTranscript)) {
    out.inputTranscript =
        decodeString(firstTranscript, 2U * 1024U, out.inputTranscriptBytes);
    if (out.inputTranscript == nullptr) {
      releaseChatCompletion(out);
      return false;
    }
  }

  Span toolCalls;
  Span firstTool;
  Span function;
  if (objectMember(message, "tool_calls", toolCalls) &&
      firstArrayValue(toolCalls, firstTool)) {
    size_t ignored = 0;
    if (!decodeOptionalString(firstTool, "id", 128U, out.toolCallId,
                              ignored) ||
        !objectMember(firstTool, "function", function) ||
        !decodeOptionalString(function, "name", 96U, out.toolName, ignored) ||
        !decodeOptionalString(function, "arguments", 72U * 1024U,
                              out.toolArguments, ignored) ||
        out.toolName == nullptr || out.toolArguments == nullptr) {
      releaseChatCompletion(out);
      return false;
    }
  }

  // Decode audio last because its binary output intentionally overwrites the
  // source JSON string in place.
  Span audio;
  if (objectMember(message, "audio", audio)) {
    if (!decodeOptionalString(audio, "transcript", 96U * 1024U,
                              out.audioTranscript,
                              out.audioTranscriptBytes)) {
      releaseChatCompletion(out);
      return false;
    }
    Span encodedAudio;
    if (objectMember(audio, "data", encodedAudio)) {
      decodeBase64InPlace(encodedAudio, out.audioData, out.audioBytes);
    }
  }
  return true;
}

bool parseStreamingChatCompletion(char *events, size_t eventBytes,
                                  ChatCompletion &out) {
  releaseChatCompletion(out);
  if (events == nullptr || eventBytes == 0) {
    return false;
  }

  char *end = events + eventBytes;
  char *first = skipJsonPrefix(events, end);
  if (first < end && *first == '{') {
    return parseChatCompletion(first, static_cast<size_t>(end - first), out);
  }
  if (first != events) {
    events = first;
    eventBytes = static_cast<size_t>(end - first);
  }

  TextAccumulator content;
  TextAccumulator inputTranscript;
  TextAccumulator audioTranscript;
  TextAccumulator toolCallId;
  TextAccumulator toolName;
  TextAccumulator toolArguments;
  bool inputTranscriptSet = false;
  bool sawChoice = false;
  size_t encodedAudioBytes = 0;

  for (char *line = events; line < end;) {
    char *lineEnd = static_cast<char *>(
        memchr(line, '\n', static_cast<size_t>(end - line)));
    char *next = lineEnd != nullptr ? lineEnd + 1 : end;
    char *contentEnd = lineEnd != nullptr ? lineEnd : end;
    if (contentEnd > line && contentEnd[-1] == '\r') {
      contentEnd--;
    }
    char *cursor = line;
    while (cursor < contentEnd && (*cursor == ' ' || *cursor == '\t')) {
      cursor++;
    }
    if (contentEnd - cursor < 5 || memcmp(cursor, "data:", 5) != 0) {
      line = next;
      continue;
    }
    cursor = skipWhitespace(cursor + 5, contentEnd);
    if (contentEnd - cursor == 6 && memcmp(cursor, "[DONE]", 6) == 0) {
      line = next;
      continue;
    }
    if (cursor >= contentEnd || *cursor != '{') {
      return false;
    }
    char *rootEnd = afterValue(cursor, contentEnd);
    if (rootEnd == nullptr || skipWhitespace(rootEnd, contentEnd) != contentEnd) {
      return false;
    }
    const Span root{cursor, rootEnd};

    Span error;
    Span errorMessage;
    size_t ignored = 0;
    if (objectMember(root, "error", error)) {
      if (objectMember(error, "message", errorMessage)) {
        out.errorMessage = decodeString(errorMessage, 512U, ignored);
      }
      return false;
    }

    Span metadata;
    Span transcripts;
    Span firstTranscript;
    if (!inputTranscriptSet && objectMember(root, "history_metadata", metadata) &&
        objectMember(metadata, "input_audio_transcripts", transcripts) &&
        firstArrayValue(transcripts, firstTranscript)) {
      inputTranscriptSet = true;
      if (!inputTranscript.append(firstTranscript, 2U * 1024U)) {
        return false;
      }
    }

    Span choices;
    Span firstChoice;
    if (!objectMember(root, "choices", choices) ||
        !firstArrayValue(choices, firstChoice)) {
      line = next;
      continue;
    }
    sawChoice = true;
    Span delta;
    if (!objectMember(firstChoice, "delta", delta)) {
      line = next;
      continue;
    }
    if (!appendOptionalFragment(delta, "content", content, 96U * 1024U)) {
      return false;
    }

    Span audio;
    Span encodedAudio;
    const bool hasAudio = objectMember(delta, "audio", audio);
    if (hasAudio &&
        !appendOptionalFragment(audio, "transcript", audioTranscript,
                                96U * 1024U)) {
      return false;
    }

    Span toolCalls;
    Span firstTool;
    Span function;
    if (objectMember(delta, "tool_calls", toolCalls) &&
        firstArrayValue(toolCalls, firstTool)) {
      if (!appendOptionalFragment(firstTool, "id", toolCallId, 128U) ||
          (objectMember(firstTool, "function", function) &&
           (!appendOptionalFragment(function, "name", toolName, 96U) ||
            !appendOptionalFragment(function, "arguments", toolArguments,
                                    72U * 1024U)))) {
        return false;
      }
    }

    // Compact audio only after all other fields in this event were inspected.
    // The destination trails the current source, so unread SSE data is safe.
    if (hasAudio && objectMember(audio, "data", encodedAudio) &&
        !appendBase64Fragment(encodedAudio, events, eventBytes,
                              encodedAudioBytes)) {
      return false;
    }
    line = next;
  }

  if (!sawChoice) {
    return false;
  }
  out.content = content.take(out.contentBytes);
  out.inputTranscript = inputTranscript.take(out.inputTranscriptBytes);
  out.audioTranscript = audioTranscript.take(out.audioTranscriptBytes);
  size_t ignored = 0;
  out.toolCallId = toolCallId.take(ignored);
  out.toolName = toolName.take(ignored);
  out.toolArguments = toolArguments.take(ignored);
  if (encodedAudioBytes > 0 &&
      !decodeCompactedBase64(events, encodedAudioBytes, out.audioData,
                             out.audioBytes)) {
    releaseChatCompletion(out);
    return false;
  }
  return true;
}

void releaseChatCompletion(ChatCompletion &completion) {
  psram::release(completion.content);
  psram::release(completion.inputTranscript);
  psram::release(completion.audioTranscript);
  psram::release(completion.toolCallId);
  psram::release(completion.toolName);
  psram::release(completion.toolArguments);
  psram::release(completion.errorMessage);
  completion = {};
}

} // namespace agentfiesta
