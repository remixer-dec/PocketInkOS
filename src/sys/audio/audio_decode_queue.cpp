#include "sys/audio/audio_decode_queue.h"

#include "sys/psram_allocator.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstring>

namespace {

constexpr uint32_t kDecodeTaskStackBytes = 6144;
constexpr UBaseType_t kDecodeTaskPriority = 2;
constexpr BaseType_t kDecodeTaskCore = 0;
constexpr unsigned long kPrefillTimeoutMs = 500;
constexpr unsigned long kStopTimeoutMs = 250;
constexpr uint16_t kMaintenanceWorkBudget = 8;

} // namespace

AudioDecodeQueue::~AudioDecodeQueue() { stop(); }

bool AudioDecodeQueue::begin(const AudioDecodeStream &stream) {
  if (started()) {
    return true;
  }
  if (stream.readPcm16 == nullptr || stream.channels == 0U ||
      stream.channels > 2U || stream.chunkFrames == 0U ||
      stream.chunkFrames > kChunkFrames) {
    return false;
  }

  decodeStream = stream;
  pcm = static_cast<int16_t *>(psram::allocate(
      kChunkCount * kChunkFrames * 2U * sizeof(int16_t)));
  freeQueue = xQueueCreate(kChunkCount, sizeof(uint8_t));
  readyQueue = xQueueCreate(kChunkCount + 1U, sizeof(ReadyChunk));
  if (pcm == nullptr || freeQueue == nullptr || readyQueue == nullptr) {
    stop();
    return false;
  }

  resetState();
  for (uint8_t i = 0; i < kChunkCount; i++) {
    xQueueSend(static_cast<QueueHandle_t>(freeQueue), &i, 0);
  }

  TaskHandle_t created = nullptr;
  // Claim the state before creation so stop() cannot release queues while the
  // new task is waiting to run.
  taskRunning = true;
  if (xTaskCreatePinnedToCore(taskEntry, "audio-decode", kDecodeTaskStackBytes,
                              this, kDecodeTaskPriority, &created,
                              kDecodeTaskCore) != pdPASS) {
    taskRunning = false;
    stop();
    return false;
  }
  task = created;

  const unsigned long start = millis();
  while (!workerFinished &&
         uxQueueMessagesWaiting(static_cast<QueueHandle_t>(readyQueue)) <
             kPrefillChunks &&
         millis() - start < kPrefillTimeoutMs) {
    delay(2);
  }
  if (uxQueueMessagesWaiting(static_cast<QueueHandle_t>(readyQueue)) == 0U) {
    stop();
    return false;
  }
  recordReadyDepth();
  return true;
}

size_t AudioDecodeQueue::readStereoPcm16(int16_t *frames,
                                         size_t frameCapacity) {
  if (!started() || frames == nullptr || frameCapacity == 0U) {
    return 0;
  }

  size_t written = 0;
  while (written < frameCapacity) {
    if (currentChunk == 0xffU) {
      ReadyChunk ready;
      const uint32_t wait = written == 0U ? pdMS_TO_TICKS(40) : 0;
      if (xQueueReceive(static_cast<QueueHandle_t>(readyQueue), &ready, wait) !=
          pdPASS) {
        readTimeouts.fetch_add(1U);
        break;
      }
      recordReadyDepth();
      if (ready.end) {
        endReached = true;
        break;
      }
      currentChunk = ready.index;
      currentFrames = ready.frames;
      currentPosition = 0;
    }

    const uint16_t available =
        static_cast<uint16_t>(currentFrames - currentPosition);
    const size_t wanted = frameCapacity - written;
    const uint16_t take = available < wanted ? available
                                             : static_cast<uint16_t>(wanted);
    const int16_t *source =
        pcm + static_cast<size_t>(currentChunk) * kChunkFrames * 2U +
        static_cast<size_t>(currentPosition) * 2U;
    memcpy(frames + written * 2U, source, take * 2U * sizeof(int16_t));
    currentPosition = static_cast<uint16_t>(currentPosition + take);
    written += take;

    if (currentPosition >= currentFrames) {
      const uint8_t released = currentChunk;
      xQueueSend(static_cast<QueueHandle_t>(freeQueue), &released, 0);
      currentChunk = 0xffU;
      currentFrames = 0;
      currentPosition = 0;
    }
  }
  if (written != frameCapacity && !endReached.load()) {
    shortReads.fetch_add(1U);
  }
  return written;
}

AudioDecodeDiagnostics AudioDecodeQueue::diagnostics() const {
  AudioDecodeDiagnostics result;
  result.decodedChunks = decodedChunks.load();
  result.lastDecodeUs = lastDecodeUs.load();
  result.maxDecodeUs = maxDecodeUs.load();
  result.readTimeouts = readTimeouts.load();
  result.shortReads = shortReads.load();
  result.readyChunks = readyChunks.load();
  const uint8_t minimum = minReadyChunks.load();
  result.minReadyChunks = minimum == 0xffU ? result.readyChunks : minimum;
  return result;
}

void AudioDecodeQueue::stop() {
  stopRequested = true;
  const unsigned long start = millis();
  while (taskRunning && millis() - start < kStopTimeoutMs) {
    delay(2);
  }
  if (taskRunning && task != nullptr) {
    vTaskDelete(static_cast<TaskHandle_t>(task));
    taskRunning = false;
  }
  task = nullptr;

  if (freeQueue != nullptr) {
    vQueueDelete(static_cast<QueueHandle_t>(freeQueue));
    freeQueue = nullptr;
  }
  if (readyQueue != nullptr) {
    vQueueDelete(static_cast<QueueHandle_t>(readyQueue));
    readyQueue = nullptr;
  }
  psram::release(pcm);
  pcm = nullptr;
  decodeStream = {};
  resetState();
}

void AudioDecodeQueue::taskEntry(void *user) {
  AudioDecodeQueue *queue = static_cast<AudioDecodeQueue *>(user);
  if (queue != nullptr) {
    queue->taskLoop();
  }
  vTaskDelete(nullptr);
}

void AudioDecodeQueue::taskLoop() {
  while (!stopRequested) {
    uint8_t index = 0;
    if (xQueueReceive(static_cast<QueueHandle_t>(freeQueue), &index,
                      pdMS_TO_TICKS(10)) != pdPASS) {
      const UBaseType_t buffered = uxQueueMessagesWaiting(
          static_cast<QueueHandle_t>(readyQueue));
      if (decodeStream.maintain != nullptr && buffered >= kPrefillChunks &&
          !decodeStream.maintain(decodeStream.user,
                                 kMaintenanceWorkBudget)) {
        decodeStream.maintain = nullptr;
      }
      continue;
    }

    int16_t *destination =
        pcm + static_cast<size_t>(index) * kChunkFrames * 2U;
    const uint32_t decodeStartedUs = micros();
    const size_t frames = decodeStream.readPcm16(
        decodeStream.user, destination, decodeStream.chunkFrames);
    const uint32_t decodeUs = micros() - decodeStartedUs;
    lastDecodeUs = decodeUs;
    decodedChunks.fetch_add(1U);
    uint32_t previousMax = maxDecodeUs.load();
    while (decodeUs > previousMax &&
           !maxDecodeUs.compare_exchange_weak(previousMax, decodeUs)) {
    }
    if (frames == 0U) {
      ReadyChunk end;
      end.end = true;
      xQueueSend(static_cast<QueueHandle_t>(readyQueue), &end, portMAX_DELAY);
      break;
    }

    if (decodeStream.channels == 1U) {
      // Expand backwards so mono input and stereo output can share one chunk.
      for (size_t i = frames; i > 0U; i--) {
        const int16_t sample = destination[i - 1U];
        destination[(i - 1U) * 2U] = sample;
        destination[(i - 1U) * 2U + 1U] = sample;
      }
    }

    ReadyChunk ready;
    ready.frames = static_cast<uint16_t>(frames);
    ready.index = index;
    if (xQueueSend(static_cast<QueueHandle_t>(readyQueue), &ready,
                   portMAX_DELAY) != pdPASS) {
      break;
    }
    recordReadyDepth();
  }
  workerFinished = true;
  taskRunning = false;
}

void AudioDecodeQueue::recordReadyDepth() {
  if (readyQueue == nullptr) {
    return;
  }
  const UBaseType_t waiting = uxQueueMessagesWaiting(
      static_cast<QueueHandle_t>(readyQueue));
  const uint8_t depth = waiting > 0xffU ? 0xffU : static_cast<uint8_t>(waiting);
  readyChunks = depth;
  uint8_t previousMin = minReadyChunks.load();
  while (depth < previousMin &&
         !minReadyChunks.compare_exchange_weak(previousMin, depth)) {
  }
}

void AudioDecodeQueue::resetState() {
  currentFrames = 0;
  currentPosition = 0;
  currentChunk = 0xffU;
  stopRequested = false;
  workerFinished = false;
  endReached = false;
  decodedChunks = 0;
  lastDecodeUs = 0;
  maxDecodeUs = 0;
  readTimeouts = 0;
  shortReads = 0;
  readyChunks = 0;
  minReadyChunks = 0xffU;
}
