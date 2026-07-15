#include "sys/agentfiesta/feed_store.h"

#include "sys/psram_allocator.h"

#include <cstring>
#include <new>

namespace agentfiesta {
namespace {

size_t clippedLength(FeedEntryType type, size_t length) {
  size_t limit = FeedStore::kMaxRetainedBytes;
  if (type == FeedEntryType::User || type == FeedEntryType::Assistant ||
      type == FeedEntryType::Error) {
    limit = FeedStore::kMaxAssistantBytes;
  } else if (type == FeedEntryType::ToolCall) {
    limit = 1024U;
  } else if (type == FeedEntryType::ToolResult) {
    limit = FeedStore::kMaxDisplayedToolResultBytes;
  }
  return length < limit ? length : limit;
}

} // namespace

FeedStore::~FeedStore() { clear(); }

bool FeedStore::ensureCapacity() {
  if (entryCount < entryCapacity) {
    return true;
  }
  const size_t nextCapacity = entryCapacity == 0 ? 8U : entryCapacity * 2U;
  if (nextCapacity < entryCapacity ||
      nextCapacity > SIZE_MAX / sizeof(Entry)) {
    return false;
  }
  Entry *next = static_cast<Entry *>(
      psram::allocate(nextCapacity * sizeof(Entry)));
  if (next == nullptr) {
    return false;
  }
  for (size_t i = 0; i < nextCapacity; i++) {
    new (&next[i]) Entry();
  }
  for (size_t i = 0; i < entryCount; i++) {
    next[i] = entries[i];
  }
  for (size_t i = 0; i < entryCapacity; i++) {
    entries[i].~Entry();
  }
  psram::release(entries);
  entries = next;
  entryCapacity = nextCapacity;
  return true;
}

bool FeedStore::append(FeedEntryType type, const char *text, size_t length,
                       uint32_t &outId) {
  outId = 0;
  if ((text == nullptr && length > 0) || !ensureCapacity()) {
    return false;
  }
  const size_t storedLength = clippedLength(type, length);
  char *stored = static_cast<char *>(psram::allocate(storedLength + 1U));
  if (stored == nullptr) {
    return false;
  }
  if (storedLength > 0) {
    memcpy(stored, text, storedLength);
  }
  stored[storedLength] = '\0';

  Entry &entry = entries[entryCount++];
  entry.id = nextId++;
  if (entry.id == 0) {
    entry.id = nextId++;
  }
  entry.length = static_cast<uint16_t>(storedLength);
  entry.type = type;
  entry.text = stored;
  retainedBytes += storedLength;
  outId = entry.id;
  enforceByteLimit();
  return true;
}

bool FeedStore::append(FeedEntryType type, const char *text, uint32_t &outId) {
  return append(type, text != nullptr ? text : "",
                text != nullptr ? strlen(text) : 0U, outId);
}

size_t FeedStore::count() const { return entryCount; }

size_t FeedStore::textBytes() const { return retainedBytes; }

bool FeedStore::at(size_t index, FeedEntryView &out) const {
  if (index >= entryCount) {
    out = {};
    return false;
  }
  const Entry &entry = entries[index];
  out.id = entry.id;
  out.type = entry.type;
  out.text = entry.text;
  out.length = entry.length;
  return true;
}

bool FeedStore::byId(uint32_t id, FeedEntryView &out) const {
  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i].id == id) {
      return at(i, out);
    }
  }
  out = {};
  return false;
}

bool FeedStore::replaceText(uint32_t id, const char *text, size_t length) {
  if (id == 0 || (text == nullptr && length > 0)) {
    return false;
  }
  for (size_t i = 0; i < entryCount; i++) {
    Entry &entry = entries[i];
    if (entry.id != id) {
      continue;
    }
    const size_t storedLength = clippedLength(entry.type, length);
    char *stored = static_cast<char *>(psram::allocate(storedLength + 1U));
    if (stored == nullptr) {
      return false;
    }
    if (storedLength > 0) {
      memcpy(stored, text, storedLength);
    }
    stored[storedLength] = '\0';
    retainedBytes -= entry.length;
    retainedBytes += storedLength;
    psram::release(entry.text);
    entry.text = stored;
    entry.length = static_cast<uint16_t>(storedLength);
    enforceByteLimit();
    return true;
  }
  return false;
}

size_t FeedStore::firstUserIndex() const {
  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i].type == FeedEntryType::User) {
      return i;
    }
  }
  return SIZE_MAX;
}

void FeedStore::removeAt(size_t index) {
  if (index >= entryCount) {
    return;
  }
  retainedBytes -= entries[index].length;
  psram::release(entries[index].text);
  for (size_t i = index + 1U; i < entryCount; i++) {
    entries[i - 1U] = entries[i];
  }
  entries[--entryCount] = Entry{};
}

void FeedStore::enforceByteLimit() {
  while (retainedBytes > kMaxRetainedBytes && entryCount > 1U) {
    const size_t firstUser = firstUserIndex();
    const size_t remove = firstUser == 0 ? 1U : 0U;
    removeAt(remove);
  }
}

void FeedStore::pruneCompleted(uint8_t latestTurns) {
  if (latestTurns < 1U) {
    latestTurns = 1U;
  }
  const size_t firstUser = firstUserIndex();
  const uint32_t firstUserId =
      firstUser != SIZE_MAX ? entries[firstUser].id : 0U;

  size_t userTurns = 0;
  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i].type == FeedEntryType::User) {
      userTurns++;
    }
  }
  if (userTurns <= latestTurns) {
    return;
  }

  size_t turnsToSkip = userTurns - latestTurns;
  size_t tailStart = 0;
  for (; tailStart < entryCount; tailStart++) {
    if (entries[tailStart].type == FeedEntryType::User &&
        turnsToSkip-- == 0U) {
      break;
    }
  }
  for (size_t i = tailStart; i > 0; i--) {
    const size_t remove = i - 1U;
    if (entries[remove].id != firstUserId) {
      removeAt(remove);
    }
  }
}

void FeedStore::swap(FeedStore &other) {
  Entry *otherEntries = other.entries;
  other.entries = entries;
  entries = otherEntries;

  size_t value = other.entryCount;
  other.entryCount = entryCount;
  entryCount = value;
  value = other.entryCapacity;
  other.entryCapacity = entryCapacity;
  entryCapacity = value;
  value = other.retainedBytes;
  other.retainedBytes = retainedBytes;
  retainedBytes = value;

  const uint32_t id = other.nextId;
  other.nextId = nextId;
  nextId = id;
}

void FeedStore::clear() {
  for (size_t i = 0; i < entryCount; i++) {
    psram::release(entries[i].text);
    entries[i].text = nullptr;
  }
  for (size_t i = 0; i < entryCapacity; i++) {
    entries[i].~Entry();
  }
  psram::release(entries);
  entries = nullptr;
  entryCount = 0;
  entryCapacity = 0;
  retainedBytes = 0;
  nextId = 1;
}

} // namespace agentfiesta
