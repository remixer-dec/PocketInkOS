/// Bounded PSRAM-backed conversation/feed storage for AgentFiesta sessions.
#ifndef SYS_AGENTFIESTA_FEED_STORE_H
#define SYS_AGENTFIESTA_FEED_STORE_H

#include <stddef.h>
#include <stdint.h>

namespace agentfiesta {

enum class FeedEntryType : uint8_t {
  User = 1,
  Assistant = 2,
  ToolCall = 3,
  ToolResult = 4,
  Error = 5,
};

struct FeedEntryView {
  uint32_t id = 0;
  FeedEntryType type = FeedEntryType::Error;
  const char *text = nullptr;
  size_t length = 0;
};

class FeedStore {
public:
  static constexpr size_t kMaxRetainedBytes = 128U * 1024U;
  static constexpr size_t kMaxAssistantBytes = 2U * 1024U;
  static constexpr size_t kMaxDisplayedToolResultBytes = 8U * 1024U;

  FeedStore() = default;
  ~FeedStore();

  FeedStore(const FeedStore &) = delete;
  FeedStore &operator=(const FeedStore &) = delete;

  bool append(FeedEntryType type, const char *text, size_t length,
              uint32_t &outId);
  bool append(FeedEntryType type, const char *text, uint32_t &outId);
  size_t count() const;
  size_t textBytes() const;
  bool at(size_t index, FeedEntryView &out) const;
  bool byId(uint32_t id, FeedEntryView &out) const;
  /// Replaces one entry's text while preserving its stable event/feed id.
  bool replaceText(uint32_t id, const char *text, size_t length);

  /// Keeps the first user entry plus the latest completed user turns.
  void pruneCompleted(uint8_t latestTurns);
  void swap(FeedStore &other);
  void clear();

private:
  struct Entry {
    uint32_t id = 0;
    uint16_t length = 0;
    FeedEntryType type = FeedEntryType::Error;
    char *text = nullptr;
  };

  Entry *entries = nullptr;
  size_t entryCount = 0;
  size_t entryCapacity = 0;
  size_t retainedBytes = 0;
  uint32_t nextId = 1;

  bool ensureCapacity();
  void removeAt(size_t index);
  void enforceByteLimit();
  size_t firstUserIndex() const;
};

} // namespace agentfiesta

#endif
