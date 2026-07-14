#ifndef SYS_AUDIO_AUDIO_DECODE_QUEUE_H
#define SYS_AUDIO_AUDIO_DECODE_QUEUE_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/// Decoder callback consumed by AudioDecodeQueue on core 0.
struct AudioDecodeStream {
  void *user = nullptr;
  /// Writes interleaved PCM using the source channel count.
  size_t (*readPcm16)(void *user, int16_t *frames,
                      size_t frameCapacity) = nullptr;
  /// Optional bounded maintenance performed only with buffered PCM headroom.
  /// Returns true while more maintenance remains useful.
  bool (*maintain)(void *user, uint16_t workBudget) = nullptr;
  uint8_t channels = 0;
  /// Source-native pull size, bounded by AudioDecodeQueue::kChunkFrames.
  uint16_t chunkFrames = 0;
};

struct AudioDecodeDiagnostics {
  uint32_t decodedChunks = 0;
  uint32_t lastDecodeUs = 0;
  uint32_t maxDecodeUs = 0;
  uint32_t readTimeouts = 0;
  uint32_t shortReads = 0;
  uint8_t readyChunks = 0;
  uint8_t minReadyChunks = 0;
};

/// PSRAM-backed PCM decode-ahead queue with stereo output for the renderer.
class AudioDecodeQueue {
public:
  static constexpr size_t kChunkFrames = 1152;
  static constexpr uint8_t kChunkCount = 8;
  static constexpr uint8_t kPrefillChunks = 6;

  AudioDecodeQueue() = default;
  ~AudioDecodeQueue();

  AudioDecodeQueue(const AudioDecodeQueue &) = delete;
  AudioDecodeQueue &operator=(const AudioDecodeQueue &) = delete;

  /// Starts the core-0 decoder and prefills enough PCM for prompt playback.
  bool begin(const AudioDecodeStream &stream);
  /// Reads renderer-ready interleaved stereo PCM.
  size_t readStereoPcm16(int16_t *frames, size_t frameCapacity);
  /// Stops the worker before its decoder or source is changed or released.
  void stop();

  bool started() const { return readyQueue != nullptr; }
  bool reachedEnd() const { return endReached.load(); }
  AudioDecodeDiagnostics diagnostics() const;

private:
  struct ReadyChunk {
    uint16_t frames = 0;
    uint8_t index = 0;
    bool end = false;
  };

  static void taskEntry(void *user);
  void taskLoop();
  void recordReadyDepth();
  void resetState();

  AudioDecodeStream decodeStream = {};
  int16_t *pcm = nullptr;
  void *freeQueue = nullptr;
  void *readyQueue = nullptr;
  void *task = nullptr;
  uint16_t currentFrames = 0;
  uint16_t currentPosition = 0;
  uint8_t currentChunk = 0xffU;
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> taskRunning{false};
  std::atomic<bool> workerFinished{false};
  std::atomic<bool> endReached{false};
  std::atomic<uint32_t> decodedChunks{0};
  std::atomic<uint32_t> lastDecodeUs{0};
  std::atomic<uint32_t> maxDecodeUs{0};
  std::atomic<uint32_t> readTimeouts{0};
  std::atomic<uint32_t> shortReads{0};
  std::atomic<uint8_t> readyChunks{0};
  std::atomic<uint8_t> minReadyChunks{0xffU};
};

#endif
