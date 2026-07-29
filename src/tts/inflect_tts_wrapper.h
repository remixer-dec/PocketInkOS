#ifndef INFLECT_TTS_WRAPPER_H
#define INFLECT_TTS_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

namespace tts {

enum class ModelVersion : uint8_t {
  Auto,
  V1,
  V2,
};

struct ModelPaths {
  const char *acoustic = nullptr;
  const char *vocoder = nullptr;
  const char *cmudict = nullptr;
  const char *v2Model = nullptr;
  const char *v2Lexicon = nullptr;
  ModelVersion version = ModelVersion::Auto;
};

struct SynthesisOptions {
  int speakerId = 0;
  int vocoderChunkFrames = 96;
  const char *vocoderBackend = nullptr;
  int griffinLimIterations = 8;
  int packedQuantTimeTile = 8;
  float lengthScale = 1.0f;
  float pitchScale = 1.0f;
  float energyScale = 1.0f;
  float v2Speed = 1.0f;
  float v2Variation = 0.667f;
  uint64_t seed = 1234;
  uint64_t v2Seed = 7;
  int v2DecoderChunkFrames = 32;
  bool (*shouldCancel)(void *user) = nullptr;
  void *cancelUser = nullptr;
};

class PcmBuffer {
public:
  PcmBuffer() = default;
  ~PcmBuffer();

  PcmBuffer(const PcmBuffer &) = delete;
  PcmBuffer &operator=(const PcmBuffer &) = delete;

  bool reserve(size_t frames);
  bool appendMono(const int16_t *samples, size_t count);
  void clear();

  const int16_t *data() const { return pcm; }
  size_t frameCount() const { return frames; }
  uint32_t sampleRateHz() const { return sampleRate; }
  void setSampleRateHz(uint32_t nextSampleRate) { sampleRate = nextSampleRate; }

private:
  int16_t *pcm = nullptr;
  size_t frames = 0;
  size_t capacity = 0;
  uint32_t sampleRate = 24000;
};

struct SynthesisResult {
  bool ok = false;
  char error[96] = {};
  uint32_t elapsedMs = 0;
  size_t frames = 0;
  uint32_t sampleRateHz = 24000;
  ModelVersion modelVersion = ModelVersion::Auto;
};

bool defaultModelPaths(ModelPaths &paths);
bool modelFilesPresent(const ModelPaths &paths, char *missing, size_t missingSize);
SynthesisResult synthesizeToPcm(const char *text, const ModelPaths &paths,
                                const SynthesisOptions &options,
                                PcmBuffer &out);

} // namespace tts

#endif
