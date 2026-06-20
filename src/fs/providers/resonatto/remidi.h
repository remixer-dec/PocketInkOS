#ifndef REMIDI_H
#define REMIDI_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct remidi_reader {
  void *user = nullptr;
  int (*read)(void *user) = nullptr;
};

enum remidi_result {
  REMIDI_OK = 0,
  REMIDI_ERR_IO = -1,
  REMIDI_ERR_FORMAT = -2,
  REMIDI_ERR_UNSUPPORTED = -3,
  REMIDI_ERR_MEMORY = -4,
};

struct remidi_info {
  uint16_t format = 0;
  uint16_t trackCount = 0;
  uint16_t division = 0;
  uint32_t durationMs = 0;
  uint32_t totalTicks = 0;
  uint32_t eventCount = 0;
  uint32_t noteOnCount = 0;
  uint16_t tempoChangeCount = 0;
};

struct remidi_event {
  uint32_t tick = 0;
  uint32_t timeMs = 0;
  uint8_t channel = 0;
  uint8_t note = 0;
  uint8_t velocity = 0;
  bool on = false;
};

struct remidi_sequence {
  remidi_info info = {};
  uint32_t eventCount = 0;
  remidi_event *events = nullptr;
};

namespace remidi {

struct TempoEvent {
  uint32_t tick = 0;
  uint32_t usPerQuarter = 500000;
};

struct ParseState {
  remidi_reader *reader = nullptr;
  TempoEvent *tempos = nullptr;
  uint32_t tempoCount = 0;
  uint32_t tempoCapacity = 0;
  remidi_event *events = nullptr;
  uint32_t eventCount = 0;
  uint32_t eventCapacity = 0;
  uint32_t totalTicks = 0;
  uint32_t rawEventCount = 0;
  uint32_t noteOnCount = 0;
};

inline int readByte(remidi_reader *reader) {
  return reader != nullptr && reader->read != nullptr ? reader->read(reader->user)
                                                      : -1;
}

inline bool readExact(remidi_reader *reader, uint8_t *out, size_t count) {
  if (out == nullptr) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    const int value = readByte(reader);
    if (value < 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}

inline bool skipBytes(remidi_reader *reader, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    if (readByte(reader) < 0) {
      return false;
    }
  }
  return true;
}

inline uint16_t readBe16(const uint8_t *data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

inline uint32_t readBe32(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

inline bool readVarLen(remidi_reader *reader, uint32_t *outValue,
                       uint32_t *remaining) {
  if (outValue == nullptr || remaining == nullptr) {
    return false;
  }
  uint32_t value = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (*remaining == 0) {
      return false;
    }
    const int raw = readByte(reader);
    if (raw < 0) {
      return false;
    }
    (*remaining)--;
    value = (value << 7) | static_cast<uint8_t>(raw & 0x7f);
    if ((raw & 0x80) == 0) {
      *outValue = value;
      return true;
    }
  }
  return false;
}

inline bool addU32(uint32_t left, uint32_t right, uint32_t *out) {
  if (out == nullptr || UINT32_MAX - left < right) {
    return false;
  }
  *out = left + right;
  return true;
}

inline bool incrementU32(uint32_t *value) {
  if (value == nullptr || *value == UINT32_MAX) {
    return false;
  }
  (*value)++;
  return true;
}

inline bool isMidiDataByte(uint8_t value) {
  return (value & 0x80U) == 0U;
}

inline bool ensureCapacity(void **buffer, uint32_t *capacity, uint32_t needed,
                           size_t itemSize) {
  if (buffer == nullptr || capacity == nullptr || itemSize == 0) {
    return false;
  }
  if (needed <= *capacity) {
    return true;
  }
  const size_t maxItemsSize = (~static_cast<size_t>(0)) / itemSize;
  if (needed > maxItemsSize) {
    return false;
  }
  const uint32_t maxItems =
      maxItemsSize > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(maxItemsSize);
  uint32_t next = *capacity == 0 ? 32U : *capacity;
  while (next < needed) {
    if (next > maxItems / 2U) {
      next = needed;
      break;
    }
    next *= 2U;
  }
  if (next > maxItems) {
    return false;
  }
  void *grown = realloc(*buffer, static_cast<size_t>(next) * itemSize);
  if (grown == nullptr) {
    return false;
  }
  *buffer = grown;
  *capacity = next;
  return true;
}

inline bool addTempo(ParseState &state, uint32_t tick, uint32_t usPerQuarter) {
  if (state.tempoCount == UINT32_MAX) {
    return false;
  }
  if (!ensureCapacity(reinterpret_cast<void **>(&state.tempos), &state.tempoCapacity,
                      state.tempoCount + 1U, sizeof(TempoEvent))) {
    return false;
  }
  state.tempos[state.tempoCount].tick = tick;
  state.tempos[state.tempoCount].usPerQuarter = usPerQuarter;
  state.tempoCount++;
  return true;
}

inline bool addNoteEvent(ParseState &state, uint32_t tick, uint8_t channel,
                         uint8_t note, uint8_t velocity, bool on) {
  if (state.eventCount == UINT32_MAX ||
      (on && state.noteOnCount == UINT32_MAX)) {
    return false;
  }
  if (!ensureCapacity(reinterpret_cast<void **>(&state.events), &state.eventCapacity,
                      state.eventCount + 1U, sizeof(remidi_event))) {
    return false;
  }
  remidi_event &event = state.events[state.eventCount];
  event.tick = tick;
  event.timeMs = 0;
  event.channel = channel;
  event.note = note;
  event.velocity = velocity;
  event.on = on;
  state.eventCount++;
  if (on) {
    state.noteOnCount++;
  }
  return true;
}

inline int compareTempo(const void *left, const void *right) {
  const TempoEvent *a = static_cast<const TempoEvent *>(left);
  const TempoEvent *b = static_cast<const TempoEvent *>(right);
  if (a->tick < b->tick) {
    return -1;
  }
  if (a->tick > b->tick) {
    return 1;
  }
  return 0;
}

inline int compareNoteEvent(const void *left, const void *right) {
  const remidi_event *a = static_cast<const remidi_event *>(left);
  const remidi_event *b = static_cast<const remidi_event *>(right);
  if (a->tick < b->tick) {
    return -1;
  }
  if (a->tick > b->tick) {
    return 1;
  }
  if (a->on != b->on) {
    return a->on ? 1 : -1;
  }
  if (a->channel < b->channel) {
    return -1;
  }
  if (a->channel > b->channel) {
    return 1;
  }
  if (a->note < b->note) {
    return -1;
  }
  if (a->note > b->note) {
    return 1;
  }
  return 0;
}

inline uint32_t durationMsForTick(const ParseState &state, uint32_t endTick,
                                  uint16_t division) {
  if (division == 0) {
    return 0;
  }

  uint64_t micros = 0;
  uint32_t cursor = 0;
  uint32_t tempo = 500000;
  for (uint32_t i = 0; i < state.tempoCount; i++) {
    const TempoEvent &event = state.tempos[i];
    if (event.tick > endTick) {
      break;
    }
    if (event.tick > cursor) {
      micros += (static_cast<uint64_t>(event.tick - cursor) * tempo) / division;
      cursor = event.tick;
    }
    tempo = event.usPerQuarter;
  }
  if (endTick > cursor) {
    micros += (static_cast<uint64_t>(endTick - cursor) * tempo) / division;
  }
  const uint64_t millis = micros / 1000ULL;
  return millis > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(millis);
}

inline remidi_result parseChannelEvent(ParseState &state, uint32_t tick,
                                        uint8_t status, uint8_t firstData,
                                        bool reusedFirstData,
                                        uint32_t *remaining) {
  if (remaining == nullptr || status < 0x80U || status >= 0xf0U) {
    return REMIDI_ERR_FORMAT;
  }
  const uint8_t type = static_cast<uint8_t>(status & 0xf0);
  const uint8_t channel = static_cast<uint8_t>(status & 0x0f);
  const bool oneDataByte = type == 0xc0 || type == 0xd0;

  uint8_t data1 = firstData;
  uint8_t data2 = 0;
  if (!reusedFirstData) {
    if (*remaining == 0) {
      return REMIDI_ERR_FORMAT;
    }
    const int value = readByte(state.reader);
    if (value < 0) {
      return REMIDI_ERR_IO;
    }
    (*remaining)--;
    data1 = static_cast<uint8_t>(value);
  }
  if (!isMidiDataByte(data1)) {
    return REMIDI_ERR_FORMAT;
  }
  if (!oneDataByte) {
    if (*remaining == 0) {
      return REMIDI_ERR_FORMAT;
    }
    const int value = readByte(state.reader);
    if (value < 0) {
      return REMIDI_ERR_IO;
    }
    (*remaining)--;
    data2 = static_cast<uint8_t>(value);
    if (!isMidiDataByte(data2)) {
      return REMIDI_ERR_FORMAT;
    }
  }

  if (!incrementU32(&state.rawEventCount)) {
    return REMIDI_ERR_UNSUPPORTED;
  }
  if (type == 0x90 && data2 != 0) {
    return addNoteEvent(state, tick, channel, data1, data2, true)
               ? REMIDI_OK
               : REMIDI_ERR_MEMORY;
  }
  if (type == 0x80 || (type == 0x90 && data2 == 0)) {
    return addNoteEvent(state, tick, channel, data1, data2, false)
               ? REMIDI_OK
               : REMIDI_ERR_MEMORY;
  }
  return REMIDI_OK;
}

inline remidi_result parseTrack(ParseState &state, uint32_t *endTickOut) {
  uint8_t header[8];
  if (!readExact(state.reader, header, sizeof(header))) {
    return REMIDI_ERR_IO;
  }
  if (header[0] != 'M' || header[1] != 'T' || header[2] != 'r' ||
      header[3] != 'k') {
    return REMIDI_ERR_FORMAT;
  }

  uint32_t remaining = readBe32(header + 4);
  uint32_t tick = 0;
  uint8_t runningStatus = 0;

  while (remaining > 0) {
    uint32_t delta = 0;
    if (!readVarLen(state.reader, &delta, &remaining)) {
      return REMIDI_ERR_FORMAT;
    }
    if (!addU32(tick, delta, &tick)) {
      return REMIDI_ERR_UNSUPPORTED;
    }
    if (tick > state.totalTicks) {
      state.totalTicks = tick;
    }

    if (remaining == 0) {
      return REMIDI_ERR_FORMAT;
    }

    int status = readByte(state.reader);
    if (status < 0) {
      return REMIDI_ERR_IO;
    }
    remaining--;

    const bool reuseRunning = (status & 0x80) == 0;
    uint8_t firstData = 0;
    if (reuseRunning) {
      if (runningStatus == 0) {
        return REMIDI_ERR_FORMAT;
      }
      firstData = static_cast<uint8_t>(status);
      status = runningStatus;
    } else if (status < 0xf0) {
      runningStatus = static_cast<uint8_t>(status);
    } else {
      runningStatus = 0;
    }

    if (status == 0xff) {
      if (remaining == 0) {
        return REMIDI_ERR_FORMAT;
      }
      const int metaType = readByte(state.reader);
      if (metaType < 0) {
        return REMIDI_ERR_IO;
      }
      remaining--;
      uint32_t length = 0;
      if (!readVarLen(state.reader, &length, &remaining) || length > remaining) {
        return REMIDI_ERR_FORMAT;
      }
      if (metaType == 0x51 && length == 3) {
        uint8_t tempoBytes[3];
        if (!readExact(state.reader, tempoBytes, sizeof(tempoBytes))) {
          return REMIDI_ERR_IO;
        }
        remaining -= 3;
        if (!addTempo(state, tick,
                      (static_cast<uint32_t>(tempoBytes[0]) << 16) |
                          (static_cast<uint32_t>(tempoBytes[1]) << 8) |
                          tempoBytes[2])) {
          return REMIDI_ERR_MEMORY;
        }
      } else {
        if (!skipBytes(state.reader, length)) {
          return REMIDI_ERR_IO;
        }
        remaining -= length;
      }
      if (!incrementU32(&state.rawEventCount)) {
        return REMIDI_ERR_UNSUPPORTED;
      }
      if (metaType == 0x2f) {
        if (remaining > 0 && !skipBytes(state.reader, remaining)) {
          return REMIDI_ERR_IO;
        }
        remaining = 0;
      }
      continue;
    }

    if (status == 0xf0 || status == 0xf7) {
      uint32_t length = 0;
      if (!readVarLen(state.reader, &length, &remaining) || length > remaining) {
        return REMIDI_ERR_FORMAT;
      }
      if (!skipBytes(state.reader, length)) {
        return REMIDI_ERR_IO;
      }
      remaining -= length;
      if (!incrementU32(&state.rawEventCount)) {
        return REMIDI_ERR_UNSUPPORTED;
      }
      continue;
    }

    const remidi_result result =
        parseChannelEvent(state, tick, static_cast<uint8_t>(status), firstData,
                          reuseRunning, &remaining);
    if (result != REMIDI_OK) {
      return result;
    }
  }

  if (endTickOut != nullptr) {
    *endTickOut = tick;
  }
  return REMIDI_OK;
}

inline void assignEventTimes(ParseState &state, uint16_t division) {
  for (uint32_t i = 0; i < state.eventCount; i++) {
    state.events[i].timeMs =
        durationMsForTick(state, state.events[i].tick, division);
  }
}

inline void freeParseState(ParseState &state) {
  free(state.tempos);
  free(state.events);
  state.tempos = nullptr;
  state.events = nullptr;
  state.tempoCount = 0;
  state.tempoCapacity = 0;
  state.eventCount = 0;
  state.eventCapacity = 0;
}

} // namespace remidi

inline void remidi_free_sequence(remidi_sequence *sequence) {
  if (sequence == nullptr) {
    return;
  }
  free(sequence->events);
  *sequence = remidi_sequence();
}

inline remidi_result remidi_read_sequence(remidi_reader *reader,
                                            remidi_sequence *outSequence) {
  if (reader == nullptr || reader->read == nullptr || outSequence == nullptr) {
    return REMIDI_ERR_FORMAT;
  }

  remidi_free_sequence(outSequence);

  uint8_t header[14];
  if (!remidi::readExact(reader, header, sizeof(header))) {
    return REMIDI_ERR_IO;
  }
  if (header[0] != 'M' || header[1] != 'T' || header[2] != 'h' ||
      header[3] != 'd' || remidi::readBe32(header + 4) != 6) {
    return REMIDI_ERR_FORMAT;
  }

  const uint16_t format = remidi::readBe16(header + 8);
  const uint16_t trackCount = remidi::readBe16(header + 10);
  const uint16_t division = remidi::readBe16(header + 12);
  if (format > 2 || trackCount == 0 || (format == 0 && trackCount != 1)) {
    return REMIDI_ERR_FORMAT;
  }
  if (division == 0) {
    return REMIDI_ERR_FORMAT;
  }
  if ((division & 0x8000U) != 0U) {
    return REMIDI_ERR_UNSUPPORTED;
  }

  remidi::ParseState state;
  state.reader = reader;
  if (!remidi::addTempo(state, 0, 500000)) {
    return REMIDI_ERR_MEMORY;
  }

  remidi_result result = REMIDI_OK;
  for (uint16_t i = 0; i < trackCount; i++) {
    uint32_t ignoredEndTick = 0;
    result = remidi::parseTrack(state, &ignoredEndTick);
    if (result != REMIDI_OK) {
      remidi::freeParseState(state);
      return result;
    }
  }

  if (state.tempoCount > 1) {
    qsort(state.tempos, state.tempoCount, sizeof(remidi::TempoEvent),
          remidi::compareTempo);
  }
  if (state.eventCount > 1) {
    qsort(state.events, state.eventCount, sizeof(remidi_event),
          remidi::compareNoteEvent);
  }
  remidi::assignEventTimes(state, division);

  outSequence->info.format = format;
  outSequence->info.trackCount = trackCount;
  outSequence->info.division = division;
  outSequence->info.durationMs =
      remidi::durationMsForTick(state, state.totalTicks, division);
  outSequence->info.totalTicks = state.totalTicks;
  outSequence->info.eventCount = state.rawEventCount;
  outSequence->info.noteOnCount = state.noteOnCount;
  outSequence->info.tempoChangeCount = static_cast<uint16_t>(
      state.tempoCount > 65536U ? UINT16_MAX
                                : (state.tempoCount > 0 ? state.tempoCount - 1U
                                                        : 0U));
  outSequence->eventCount = state.eventCount;
  outSequence->events = state.events;

  free(state.tempos);
  state.tempos = nullptr;
  state.events = nullptr;
  return REMIDI_OK;
}

inline remidi_result remidi_read_info(remidi_reader *reader,
                                        remidi_info *outInfo) {
  if (outInfo == nullptr) {
    return REMIDI_ERR_FORMAT;
  }
  remidi_sequence sequence;
  const remidi_result result = remidi_read_sequence(reader, &sequence);
  if (result == REMIDI_OK) {
    *outInfo = sequence.info;
  }
  remidi_free_sequence(&sequence);
  return result;
}

#endif
