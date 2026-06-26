#include "tts/inflect_tts_wrapper.h"

#include "tts/tts_ggml_psram_buffer.h"
#include "inflect-nano.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define TTS_HAS_ROM_PRINTF 1
#else
#define TTS_HAS_ROM_PRINTF 0
#endif

namespace tts {
namespace {

#ifndef INFLECT_USE_ESP_DSP_CONTIG
#define INFLECT_USE_ESP_DSP_CONTIG 0
#endif

#ifndef INFLECT_USE_ESP_DSP_STRIDED
#define INFLECT_USE_ESP_DSP_STRIDED 1
#endif

#ifndef INFLECT_USE_RESBLOCK_IM2COL
#define INFLECT_USE_RESBLOCK_IM2COL 1
#endif

#ifndef INFLECT_VOCODER_BACKEND
#define INFLECT_VOCODER_BACKEND neural
#endif

#ifndef INFLECT_GRIFFIN_LIM_ITERS
#define INFLECT_GRIFFIN_LIM_ITERS 8
#endif

#ifndef INFLECT_PROFILE_ACOUSTIC_OPS
#define INFLECT_PROFILE_ACOUSTIC_OPS 0
#endif

#ifndef INFLECT_PROFILE_VOCODER_OPS
#define INFLECT_PROFILE_VOCODER_OPS 0
#endif

#ifndef INFLECT_ACOUSTIC_SKIP_POSTNET
#define INFLECT_ACOUSTIC_SKIP_POSTNET 0
#endif

#define TTS_STRINGIFY_IMPL(x) #x
#define TTS_STRINGIFY(x) TTS_STRINGIFY_IMPL(x)

void ttsLog(const char *format, ...);

const char *buildVocoderBackend() { return TTS_STRINGIFY(INFLECT_VOCODER_BACKEND); }

uint32_t inflectMillis() { return static_cast<uint32_t>(millis()); }

const char *inflectBackendLabel() {
#if INFLECT_USE_ESP_DSP_CONTIG && INFLECT_USE_ESP_DSP_STRIDED
  return "esp-dsp-contig+strided";
#elif INFLECT_USE_ESP_DSP_CONTIG
  return "esp-dsp-contig";
#elif INFLECT_USE_ESP_DSP_STRIDED
  return "esp-dsp-strided";
#else
  return "scalar";
#endif
}

void *inflectScratchAlloc(size_t bytes, inflect::ScratchMemoryKind kind) {
  if (bytes == 0) {
    return nullptr;
  }
  uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  if (kind == inflect::ScratchMemoryKind::InternalPreferred) {
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
      return ptr;
    }
  }
  return heap_caps_malloc(bytes, caps);
}

void inflectScratchFree(void *ptr) { heap_caps_free(ptr); }

void inflectTraceHeap(const char *label) {
#if defined(ESP_PLATFORM)
  const size_t internalFree =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psramFree =
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t psramLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ttsLog("[mem] %s internal_free=%u internal_largest=%u "
         "psram_free=%u psram_largest=%u\n",
         label != nullptr ? label : "",
         static_cast<unsigned>(internalFree),
         static_cast<unsigned>(internalLargest),
         static_cast<unsigned>(psramFree),
         static_cast<unsigned>(psramLargest));
#else
  (void)label;
#endif
}

void configureInflectRuntime() {
  inflect::RuntimeConfig config;
  config.weight_buffer_type = tts::psramCpuBufferType;
  config.now_ms = inflectMillis;
  config.scratch_alloc = inflectScratchAlloc;
  config.scratch_free = inflectScratchFree;
  config.trace_heap = inflectTraceHeap;
  config.backend_label = inflectBackendLabel();
  inflect::configure_runtime(config);
}

const char *selectedVocoderBackend(const SynthesisOptions &options) {
  return (options.vocoderBackend != nullptr && options.vocoderBackend[0] != '\0')
             ? options.vocoderBackend
             : buildVocoderBackend();
}

bool isGriffinLimBackend(const char *backend) {
  return strcmp(backend != nullptr ? backend : "", "griffin_lim") == 0;
}

constexpr const char *kAcousticPath =
    "/sdcard/tts/acoustic/inflect_acoustic_q4_0_E.gguf";
constexpr const char *kVocoderPath =
    "/sdcard/tts/vocoder/inflect_vocoder_q4_0_E.gguf";
constexpr const char *kCmuDictPath = "/sdcard/tts/cmudict.bin";

void ttsLog(const char *format, ...) {
  char line[320];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
#if TTS_HAS_ROM_PRINTF
  esp_rom_printf("%s", line);
#else
  Serial.print(line);
#endif
}

void logHeapState(const char *label) {
#if defined(ESP_PLATFORM)
  const size_t internalFree =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psramFree =
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t psramLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ttsLog("[tts] heap %s internal_free=%u internal_largest=%u "
         "psram_free=%u psram_largest=%u\n",
         label,
         static_cast<unsigned>(internalFree),
         static_cast<unsigned>(internalLargest),
         static_cast<unsigned>(psramFree),
         static_cast<unsigned>(psramLargest));
#else
  (void)label;
#endif
}

void copyError(char *out, size_t outSize, const char *message) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const char *text = message != nullptr ? message : "";
  strncpy(out, text, outSize - 1);
  out[outSize - 1] = '\0';
}

bool fileExists(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int16_t floatToPcm16(float sample) {
  sample = std::max(-1.0f, std::min(1.0f, sample));
  return static_cast<int16_t>(sample * 32767.0f);
}

void logFloatAudioStats(const char *label, const std::vector<float> &audio) {
  if (audio.empty()) {
    ttsLog("[tts] audio %s empty\n", label);
    return;
  }
  float minValue = audio[0];
  float maxValue = audio[0];
  float peak = 0.0f;
  double sumSq = 0.0;
  for (float sample : audio) {
    if (!std::isfinite(sample)) {
      continue;
    }
    minValue = std::min(minValue, sample);
    maxValue = std::max(maxValue, sample);
    peak = std::max(peak, std::fabs(sample));
    sumSq += static_cast<double>(sample) * static_cast<double>(sample);
  }
  const float rms = static_cast<float>(std::sqrt(sumSq / audio.size()));
  ttsLog("[tts] audio %s min=%.6f max=%.6f peak=%.6f rms=%.6f\n",
         label, minValue, maxValue, peak, rms);
}

} // namespace

PcmBuffer::~PcmBuffer() { clear(); }

bool PcmBuffer::reserve(size_t nextCapacity) {
  if (nextCapacity <= capacity) {
    return true;
  }
  size_t grown = capacity == 0 ? 8192 : capacity;
  while (grown < nextCapacity) {
    grown *= 2;
  }

  int16_t *next = static_cast<int16_t *>(
      heap_caps_malloc(grown * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (next == nullptr) {
    ttsLog("[tts] psram pcm alloc failed frames=%u bytes=%u\n",
           static_cast<unsigned>(grown),
           static_cast<unsigned>(grown * sizeof(int16_t)));
    return false;
  }
  if (pcm != nullptr && frames > 0) {
    memcpy(next, pcm, frames * sizeof(int16_t));
  }
  heap_caps_free(pcm);
  pcm = next;
  capacity = grown;
  ttsLog("[tts] pcm capacity frames=%u bytes=%u\n",
         static_cast<unsigned>(capacity),
         static_cast<unsigned>(capacity * sizeof(int16_t)));
  return true;
}

bool PcmBuffer::appendMono(const int16_t *samples, size_t count) {
  if (count == 0) {
    return true;
  }
  if (samples == nullptr || !reserve(frames + count)) {
    return false;
  }
  memcpy(pcm + frames, samples, count * sizeof(int16_t));
  frames += count;
  return true;
}

void PcmBuffer::clear() {
  if (pcm != nullptr) {
    heap_caps_free(pcm);
    pcm = nullptr;
  }
  frames = 0;
  capacity = 0;
  sampleRate = 24000;
}

bool defaultModelPaths(ModelPaths &paths) {
  paths.acoustic = kAcousticPath;
  paths.vocoder = kVocoderPath;
  paths.cmudict = kCmuDictPath;
  return true;
}

bool modelFilesPresentForBackend(const ModelPaths &paths, const char *vocoderBackend,
                                 char *missing, size_t missingSize) {
  if (!fileExists(paths.acoustic)) {
    copyError(missing, missingSize, paths.acoustic);
    return false;
  }
  if (!isGriffinLimBackend(vocoderBackend) && !fileExists(paths.vocoder)) {
    copyError(missing, missingSize, paths.vocoder);
    return false;
  }
  if (!fileExists(paths.cmudict)) {
    copyError(missing, missingSize, paths.cmudict);
    return false;
  }
  if (missing != nullptr && missingSize > 0) {
    missing[0] = '\0';
  }
  return true;
}

bool modelFilesPresent(const ModelPaths &paths, char *missing,
                       size_t missingSize) {
  return modelFilesPresentForBackend(paths, buildVocoderBackend(), missing,
                                     missingSize);
}

SynthesisResult synthesizeToPcm(const char *text, const ModelPaths &paths,
                                const SynthesisOptions &options,
                                PcmBuffer &out) {
  SynthesisResult result;
  out.clear();

  const char *input = text != nullptr ? text : "";
  const char *backend = selectedVocoderBackend(options);
  const int griffinLimIterations = std::max(0, options.griffinLimIterations);
  if (input[0] == '\0') {
    copyError(result.error, sizeof(result.error), "No text");
    return result;
  }

  char missing[96];
  if (!modelFilesPresentForBackend(paths, backend, missing, sizeof(missing))) {
    snprintf(result.error, sizeof(result.error), "Missing %s", missing);
    ttsLog("[tts] model check failed error=\"%s\"\n", result.error);
    return result;
  }

  const unsigned long startedAt = millis();
  ttsLog("[tts] synth start text_len=%u speaker=%d chunk_frames=%d\n",
         static_cast<unsigned>(strlen(input)), options.speakerId,
         options.vocoderChunkFrames);
  ttsLog("[tts] build variant low_memory=%d esp_dsp_contig=%d "
         "esp_dsp_strided=%d resblock_im2col=%d backend=%s "
         "griffin_lim_iters=%d profile_acoustic=%d profile_vocoder=%d "
         "acoustic_skip_postnet=%d runtime_backend=%s runtime_gl_iters=%d\n",
#if defined(INFLECT_LOW_MEMORY)
         1,
#else
         0,
#endif
         INFLECT_USE_ESP_DSP_CONTIG,
         INFLECT_USE_ESP_DSP_STRIDED,
         INFLECT_USE_RESBLOCK_IM2COL,
         buildVocoderBackend(),
         INFLECT_GRIFFIN_LIM_ITERS,
         INFLECT_PROFILE_ACOUSTIC_OPS,
         INFLECT_PROFILE_VOCODER_OPS,
         INFLECT_ACOUSTIC_SKIP_POSTNET,
         backend,
         griffinLimIterations);
  ttsLog("[tts] acoustic=%s\n", paths.acoustic);
  ttsLog("[tts] vocoder=%s\n", paths.vocoder);
  ttsLog("[tts] cmudict=%s\n", paths.cmudict);
  logHeapState("start");

  configureInflectRuntime();
  inflect::Synthesizer::init_backend(1);
  inflect::Synthesizer synth;
  logHeapState("after_backend");

  ttsLog("[tts] load acoustic begin\n");
  if (!synth.load_acoustic(paths.acoustic)) {
    copyError(result.error, sizeof(result.error), "Acoustic load failed");
    ttsLog("[tts] load acoustic failed\n");
    return result;
  }
  ttsLog("[tts] load acoustic ok elapsed_ms=%u\n",
         static_cast<unsigned>(millis() - startedAt));
  logHeapState("after_acoustic");

  if (isGriffinLimBackend(backend)) {
    ttsLog("[tts] load vocoder skipped backend=griffin_lim\n");
  } else {
    ttsLog("[tts] load vocoder begin\n");
    if (!synth.load_vocoder(paths.vocoder)) {
      copyError(result.error, sizeof(result.error), "Vocoder load failed");
      ttsLog("[tts] load vocoder failed\n");
      return result;
    }
    ttsLog("[tts] load vocoder ok elapsed_ms=%u\n",
           static_cast<unsigned>(millis() - startedAt));
  }

  ttsLog("[tts] load cmudict begin\n");
  if (!synth.load_cmudict(paths.cmudict)) {
    copyError(result.error, sizeof(result.error), "CMU dict load failed");
    ttsLog("[tts] load cmudict failed\n");
    return result;
  }
  ttsLog("[tts] load cmudict ok elapsed_ms=%u\n",
         static_cast<unsigned>(millis() - startedAt));
  logHeapState("after_cmudict");

  inflect::SynthParams params;
  params.length_scale = options.lengthScale;
  params.pitch_scale = options.pitchScale;
  params.energy_scale = options.energyScale;
  params.speaker_id = options.speakerId;
  params.vocoder_chunk_frames = options.vocoderChunkFrames;
  params.vocoder_backend = backend;
  params.griffin_lim_iterations = griffinLimIterations;

  ttsLog("[tts] inference begin\n");
  logHeapState("before_inference");
  std::vector<float> audio = synth.synthesize(input, params);
  logHeapState("after_inference");
  if (audio.empty()) {
    copyError(result.error, sizeof(result.error), "No audio generated");
    ttsLog("[tts] inference produced no audio elapsed_ms=%u\n",
           static_cast<unsigned>(millis() - startedAt));
    return result;
  }
  ttsLog("[tts] inference raw_samples=%u elapsed_ms=%u\n",
         static_cast<unsigned>(audio.size()),
         static_cast<unsigned>(millis() - startedAt));
  logFloatAudioStats("after_synthesis", audio);

  out.setSampleRateHz(static_cast<uint32_t>(synth.sample_rate()));
  if (!out.reserve(audio.size())) {
    copyError(result.error, sizeof(result.error), "PCM PSRAM allocation failed");
    return result;
  }

  std::vector<int16_t> chunk;
  chunk.resize(512);
  size_t cursor = 0;
  while (cursor < audio.size()) {
    const size_t count = std::min(chunk.size(), audio.size() - cursor);
    for (size_t i = 0; i < count; i++) {
      chunk[i] = floatToPcm16(audio[cursor + i]);
    }
    if (!out.appendMono(chunk.data(), count)) {
      copyError(result.error, sizeof(result.error), "PCM append failed");
      return result;
    }
    cursor += count;
  }

  result.ok = true;
  result.frames = out.frameCount();
  result.sampleRateHz = out.sampleRateHz();
  result.elapsedMs = millis() - startedAt;
  logHeapState("after_pcm");
  ttsLog("[tts] synth complete frames=%u rate=%u elapsed_ms=%u\n",
         static_cast<unsigned>(result.frames),
         static_cast<unsigned>(result.sampleRateHz),
         static_cast<unsigned>(result.elapsedMs));
  return result;
}

} // namespace tts
