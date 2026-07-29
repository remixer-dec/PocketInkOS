#include "tts/inflect_tts_wrapper.h"

#include "tts/tts_ggml_psram_buffer.h"
#include "inflect-nano.h"
#include "vocoder_quant_math.h"
#include "sys/psram_allocator.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#if __has_include(<esp_pthread.h>)
#include <esp_pthread.h>
#define TTS_HAS_ESP_PTHREAD 1
#else
#define TTS_HAS_ESP_PTHREAD 0
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef INFLECT_PTHREAD_STACK_BYTES
#define INFLECT_PTHREAD_STACK_BYTES 8192
#endif

#if defined(__XTENSA__) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define TTS_HAS_S8_DOT_SIMD 1
#else
#define TTS_HAS_S8_DOT_SIMD 0
#endif

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

constexpr int kInflectQ8RoundLanes = 4;

void ttsLog(const char *format, ...);

const char *buildVocoderBackend() { return TTS_STRINGIFY(INFLECT_VOCODER_BACKEND); }

uint32_t inflectMillis() { return static_cast<uint32_t>(millis()); }

IRAM_ATTR uint32_t inflectCycles() {
#if defined(__XTENSA__)
  uint32_t cycles;
  __asm__ volatile("rsr.ccount %0" : "=a"(cycles));
  return cycles;
#else
  return static_cast<uint32_t>(micros());
#endif
}

#if defined(INFLECT_LOW_MEMORY) && TTS_HAS_S8_DOT_SIMD && \
    TTS_HAS_ESP_PTHREAD
class InflectPthreadStackScope {
public:
  InflectPthreadStackScope() {
    if (esp_pthread_get_cfg(&previous_) != ESP_OK) {
      previous_ = esp_pthread_get_default_config();
    }
    if (previous_.stack_size >= INFLECT_PTHREAD_STACK_BYTES) {
      return;
    }

    esp_pthread_cfg_t configured = previous_;
    configured.stack_size = INFLECT_PTHREAD_STACK_BYTES;
    if (esp_pthread_set_cfg(&configured) == ESP_OK) {
      restore_ = true;
      ttsLog("[tts] pthread worker stack bytes=%u previous=%u\n",
             static_cast<unsigned>(configured.stack_size),
             static_cast<unsigned>(previous_.stack_size));
    } else {
      ttsLog("[tts] pthread worker stack configuration failed bytes=%u\n",
             static_cast<unsigned>(configured.stack_size));
    }
  }

  ~InflectPthreadStackScope() {
    if (restore_) {
      esp_pthread_set_cfg(&previous_);
    }
  }

  InflectPthreadStackScope(const InflectPthreadStackScope &) = delete;
  InflectPthreadStackScope &
  operator=(const InflectPthreadStackScope &) = delete;

private:
  esp_pthread_cfg_t previous_{};
  bool restore_ = false;
};
#else
class InflectPthreadStackScope {};
#endif

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
  return kind == inflect::ScratchMemoryKind::InternalPreferred
             ? psram::allocateInternalOrPsram(bytes)
             : psram::allocate(bytes);
}

void inflectScratchFree(void *ptr) { psram::release(ptr); }

#if defined(INFLECT_LOW_MEMORY)
void inflectCooperate() {
  static thread_local uint32_t lastCooperateAt = 0;
  const uint32_t current = inflectMillis();
  if (current - lastCooperateAt >= 50) {
    lastCooperateAt = current;
    delay(1);
  }
}

#if TTS_HAS_S8_DOT_SIMD
alignas(16) constexpr int8_t kInflectQ4ZeroPoint[16] = {
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
};

alignas(16) constexpr int8_t kInflectS8Zeros[16] = {};

typedef uint16_t InflectAliasU16 __attribute__((may_alias));
typedef uint32_t InflectAliasU32 __attribute__((may_alias));

#if defined(__GNUC__) && !defined(__clang__)
#define TTS_INFLECT_O3 __attribute__((optimize("O3")))
#else
#define TTS_INFLECT_O3
#endif

IRAM_ATTR __attribute__((always_inline)) inline uint32_t
inflectFloatBits(float value) {
  uint32_t bits;
  __asm__ volatile(
      "rfr %0,%1"
      : "=a"(bits)
      : "f"(value));
  return bits;
}

IRAM_ATTR __attribute__((always_inline)) inline float
inflectFloatFromBits(uint32_t bits) {
  float value;
  __asm__ volatile(
      "wfr %0,%1"
      : "=f"(value)
      : "a"(bits));
  return value;
}

IRAM_ATTR __attribute__((always_inline)) inline int
inflectRoundHalfAwayAdd(float value) {
  const uint32_t bits = inflectFloatBits(value);
  if ((bits & 0x7fffffffU) < 0x3f000000U) {
    return 0;
  }
  const float signedHalf = inflectFloatFromBits(
      (bits & 0x80000000U) | 0x3f000000U);
  return static_cast<int>(value + signedHalf);
}

IRAM_ATTR __attribute__((always_inline)) inline float
inflectCachePositiveScaleFp16(float scale) {
  constexpr float kScaleToInf = 0x1.0p+112f;
  constexpr float kScaleToZero = 0x1.0p-110f;
  float base = (scale * kScaleToInf) * kScaleToZero;

  const uint32_t bits = inflectFloatBits(scale);
  const uint32_t shifted = bits + bits;
  uint32_t bias = shifted & 0xff000000U;
  if (bias < 0x71000000U) {
    bias = 0x71000000U;
  }
  base =
      inflectFloatFromBits(
          (bias >> 1) + 0x07800000U) +
      base;
  const uint32_t baseBits = inflectFloatBits(base);
  const uint16_t half = static_cast<uint16_t>(
      ((baseBits >> 13) & 0x00007c00U) +
      (baseBits & 0x00000fffU));

  const uint32_t word = static_cast<uint32_t>(half) << 16;
  const uint32_t doubled = word + word;
  const float normalized =
      inflectFloatFromBits(
          (doubled >> 4) + (0xe0U << 23)) *
      0x1.0p-112f;
  const float denormalized =
      inflectFloatFromBits(
          (doubled >> 17) | (126U << 23)) -
      0.5f;
  return doubled < (1U << 27)
             ? denormalized
             : normalized;
}

IRAM_ATTR __attribute__((noinline))
void inflectStoreZeroS8Blocks32(
    int8_t *destination,
    size_t blocks) {
  const int8_t *zeros = kInflectS8Zeros;
  int8_t *output = destination;
  __asm__ volatile(
      "ee.vld.128.ip q0,%[zeros],0\n"
      :
      : [zeros] "r"(zeros)
      : "memory");
  for (size_t block = 0; block < blocks; ++block) {
    __asm__ volatile(
        "ee.vst.128.ip q0,%[output],16\n"
        "ee.vst.128.ip q0,%[output],16\n"
        : [output] "+r"(output)
        :
        : "memory");
  }
}

template <bool SkipZeroBlocks>
IRAM_ATTR TTS_INFLECT_O3 __attribute__((always_inline)) inline void
inflectQuantizeF32ToQ8Blocks32Impl(
    const float *source,
    int8_t *destination,
    float *cachedScales,
    size_t blocks,
    uint64_t *maxCycles,
    uint64_t *scaleCycles,
    uint64_t *convertCycles) {
  for (size_t block = 0; block < blocks; ++block) {
#if INFLECT_PROFILE_VOCODER_OPS
    const uint32_t maxStarted = inflectCycles();
#endif
    const float maximum =
        inflect::vocoder_quant::absolute_max_32(source);
#if INFLECT_PROFILE_VOCODER_OPS
    const uint32_t maxFinished = inflectCycles();
#endif
    const float scale = maximum / 127.0f;
    const float inverse =
        scale != 0.0f ? 1.0f / scale : 0.0f;
    cachedScales[block] =
        inflectCachePositiveScaleFp16(scale);
#if INFLECT_PROFILE_VOCODER_OPS
    const uint32_t scaleFinished = inflectCycles();
#endif
    if constexpr (SkipZeroBlocks) {
      if (maximum == 0.0f) {
        inflectStoreZeroS8Blocks32(destination, 1);
#if INFLECT_PROFILE_VOCODER_OPS
        const uint32_t convertFinished = inflectCycles();
        if (maxCycles != nullptr) {
          *maxCycles +=
              static_cast<uint32_t>(maxFinished - maxStarted);
        }
        if (scaleCycles != nullptr) {
          *scaleCycles +=
              static_cast<uint32_t>(scaleFinished - maxFinished);
        }
        if (convertCycles != nullptr) {
          *convertCycles += static_cast<uint32_t>(
              convertFinished - scaleFinished);
        }
#endif
        source += 32;
        destination += 32;
        continue;
      }
    }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC unroll 2
#endif
    for (int index = 0; index < 32; index += kInflectQ8RoundLanes) {
      const float scaled0 = source[index] * inverse;
      const float scaled1 = source[index + 1] * inverse;
      const float scaled2 = source[index + 2] * inverse;
      const float scaled3 = source[index + 3] * inverse;

      const int value0 =
          inflectRoundHalfAwayAdd(scaled0);
      const int value1 =
          inflectRoundHalfAwayAdd(scaled1);
      const int value2 =
          inflectRoundHalfAwayAdd(scaled2);
      const int value3 =
          inflectRoundHalfAwayAdd(scaled3);

      destination[index] = static_cast<int8_t>(value0);
      destination[index + 1] = static_cast<int8_t>(value1);
      destination[index + 2] = static_cast<int8_t>(value2);
      destination[index + 3] = static_cast<int8_t>(value3);
    }
#if INFLECT_PROFILE_VOCODER_OPS
    const uint32_t convertFinished = inflectCycles();
    if (maxCycles != nullptr) {
      *maxCycles +=
          static_cast<uint32_t>(maxFinished - maxStarted);
    }
    if (scaleCycles != nullptr) {
      *scaleCycles +=
          static_cast<uint32_t>(scaleFinished - maxFinished);
    }
    if (convertCycles != nullptr) {
      *convertCycles +=
          static_cast<uint32_t>(convertFinished - scaleFinished);
    }
#else
    (void)maxCycles;
    (void)scaleCycles;
    (void)convertCycles;
#endif
    source += 32;
    destination += 32;
  }
}

IRAM_ATTR TTS_INFLECT_O3 __attribute__((noinline))
void inflectQuantizeF32ToQ8Blocks32(
    const float *source,
    int8_t *destination,
    float *cachedScales,
    size_t blocks,
    bool skipZeroBlocks,
    uint64_t *maxCycles,
    uint64_t *scaleCycles,
    uint64_t *convertCycles) {
  if (skipZeroBlocks) {
    inflectQuantizeF32ToQ8Blocks32Impl<true>(
        source, destination, cachedScales, blocks,
        maxCycles, scaleCycles, convertCycles);
  } else {
    inflectQuantizeF32ToQ8Blocks32Impl<false>(
        source, destination, cachedScales, blocks,
        maxCycles, scaleCycles, convertCycles);
  }
}

IRAM_ATTR __attribute__((always_inline)) inline void
inflectUnpackQ4UnsignedBlock32(
    const uint8_t *packed,
    uint8_t *destination) {
  constexpr uint16_t kNibbleMask16 = 0x0f0fU;
  constexpr uint32_t kNibbleMask32 = 0x0f0f0f0fUL;
  if ((reinterpret_cast<uintptr_t>(packed) & 3U) == 0U) {
    const auto *source =
        reinterpret_cast<const InflectAliasU32 *>(packed);
    auto *low =
        reinterpret_cast<InflectAliasU32 *>(destination);
    auto *high =
        reinterpret_cast<InflectAliasU32 *>(destination + 16);
    for (int index = 0; index < 4; ++index) {
      const uint32_t value = source[index];
      low[index] = value & kNibbleMask32;
      high[index] = (value >> 4) & kNibbleMask32;
    }
    return;
  }

  const auto *source =
      reinterpret_cast<const InflectAliasU16 *>(packed);
  auto *low =
      reinterpret_cast<InflectAliasU16 *>(destination);
  auto *high =
      reinterpret_cast<InflectAliasU16 *>(destination + 16);
  for (int index = 0; index < 8; ++index) {
    const uint16_t value = source[index];
    low[index] =
        static_cast<uint16_t>(value & kNibbleMask16);
    high[index] = static_cast<uint16_t>(
        (value >> 4) & kNibbleMask16);
  }
}

IRAM_ATTR __attribute__((always_inline)) inline void
inflectSubtractQ4ZeroPoint(int8_t *values) {
  const int8_t *source = values;
  int8_t *destination = values;
  const int8_t *zeroPoint = kInflectQ4ZeroPoint;
  __asm__ volatile(
      "ee.vld.128.ip q2,%[zero_point],0\n"
      "ee.vld.128.ip q0,%[source],16\n"
      "ee.vsubs.s8 q0,q0,q2\n"
      "ee.vst.128.ip q0,%[destination],16\n"
      "ee.vld.128.ip q1,%[source],16\n"
      "ee.vsubs.s8 q1,q1,q2\n"
      "ee.vst.128.ip q1,%[destination],16\n"
      : [source] "+r"(source),
        [destination] "+r"(destination)
      : [zero_point] "r"(zeroPoint)
      : "memory");
}

IRAM_ATTR void inflectUnpackQ4Blocks32(
    const uint8_t *packedBlocks,
    size_t packedBlockBytes,
    int8_t *values,
    uint16_t *scaleBits,
    size_t blocks) {
  const uint8_t *packed = packedBlocks;
  int8_t *destination = values;
  for (size_t block = 0; block < blocks; ++block) {
    scaleBits[block] =
        *reinterpret_cast<const InflectAliasU16 *>(packed);
    inflectUnpackQ4UnsignedBlock32(
        packed + sizeof(uint16_t),
        reinterpret_cast<uint8_t *>(destination));
    inflectSubtractQ4ZeroPoint(destination);
    packed += packedBlockBytes;
    destination += 32;
  }
}

IRAM_ATTR __attribute__((always_inline)) inline int32_t
inflectDotS8Block32(const int8_t *&weight, const int8_t *&input) {
  int32_t sum;
  int32_t zero;
  __asm__ volatile(
      "ee.zero.accx\n"
      "ee.vld.128.ip q0,%[weight],16\n"
      "ee.vld.128.ip q1,%[input],16\n"
      "ee.vmulas.s8.accx q0,q1\n"
      "ee.vld.128.ip q0,%[weight],16\n"
      "ee.vld.128.ip q1,%[input],16\n"
      "ee.vmulas.s8.accx q0,q1\n"
      "movi.n %[zero],0\n"
      "nop\n"
      "ee.srs.accx %[sum],%[zero],0\n"
      : [weight] "+r"(weight),
        [input] "+r"(input),
        [sum] "=&r"(sum),
        [zero] "=&r"(zero)
      :
      : "memory");
  return sum;
}

IRAM_ATTR void inflectDotS8Blocks32(
    const int8_t *weights,
    const int8_t *inputs,
    int32_t *sums,
    size_t blocks,
    size_t rows) {
  for (size_t row = 0; row < rows; ++row) {
    const int8_t *weight = weights;
    for (size_t block = 0; block < blocks; ++block) {
      *sums++ = inflectDotS8Block32(weight, inputs);
    }
  }
}

template <bool SkipZeroScaleBlocks, bool Profile>
IRAM_ATTR __attribute__((always_inline)) inline void
inflectDotS8ScaledBlocks32Impl(
    const int8_t *weights,
    const float *weightScales,
    const int8_t *inputs,
    const float *inputScales,
    float *results,
    size_t blocks,
    size_t rows,
    uint64_t *dotCycles,
    uint64_t *scaleCycles) {
  uint64_t measuredDotCycles = 0;
  uint64_t measuredScaleCycles = 0;
  for (size_t row = 0; row < rows; ++row) {
    const int8_t *weight = weights;
    const float *rowInputScales = inputScales + row * blocks;
    float result = 0.0f;
    for (size_t block = 0; block < blocks; ++block) {
      if constexpr (SkipZeroScaleBlocks) {
        if (rowInputScales[block] == 0.0f) {
          weight += 32;
          inputs += 32;
          continue;
        }
      }
      uint32_t dotStarted = 0;
      if constexpr (Profile) {
        dotStarted = inflectCycles();
      }
      const int32_t sum = inflectDotS8Block32(weight, inputs);
      uint32_t dotFinished = 0;
      if constexpr (Profile) {
        dotFinished = inflectCycles();
      }
      result += static_cast<float>(sum) *
                weightScales[block] *
                rowInputScales[block];
      if constexpr (Profile) {
        const uint32_t scaleFinished = inflectCycles();
        measuredDotCycles +=
            static_cast<uint32_t>(dotFinished - dotStarted);
        measuredScaleCycles +=
            static_cast<uint32_t>(scaleFinished - dotFinished);
      }
    }
    results[row] = result;
  }
  if constexpr (Profile) {
    if (dotCycles != nullptr) {
      *dotCycles += measuredDotCycles;
    }
    if (scaleCycles != nullptr) {
      *scaleCycles += measuredScaleCycles;
    }
  }
}

IRAM_ATTR void inflectDotS8ScaledBlocks32(
    const int8_t *weights,
    const float *weightScales,
    const int8_t *inputs,
    const float *inputScales,
    float *results,
    size_t blocks,
    size_t rows,
    bool skipZeroScaleBlocks,
    uint64_t *dotCycles,
    uint64_t *scaleCycles) {
  const bool profile =
      dotCycles != nullptr || scaleCycles != nullptr;
  if (skipZeroScaleBlocks) {
    if (profile) {
      inflectDotS8ScaledBlocks32Impl<true, true>(
          weights, weightScales, inputs, inputScales,
          results, blocks, rows, dotCycles, scaleCycles);
    } else {
      inflectDotS8ScaledBlocks32Impl<true, false>(
          weights, weightScales, inputs, inputScales,
          results, blocks, rows, nullptr, nullptr);
    }
  } else {
    if (profile) {
      inflectDotS8ScaledBlocks32Impl<false, true>(
          weights, weightScales, inputs, inputScales,
          results, blocks, rows, dotCycles, scaleCycles);
    } else {
      inflectDotS8ScaledBlocks32Impl<false, false>(
          weights, weightScales, inputs, inputScales,
          results, blocks, rows, nullptr, nullptr);
    }
  }
}
#endif
#endif

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

void configureInflectRuntime(
    int packedQuantTimeTile,
    bool (*shouldCancel)(void *),
    void *cancelUser) {
  inflect::RuntimeConfig config;
  config.weight_buffer_type = tts::psramCpuBufferType;
  config.now_ms = inflectMillis;
  config.now_cycles = inflectCycles;
  config.scratch_alloc = inflectScratchAlloc;
  config.scratch_free = inflectScratchFree;
  config.trace_heap = inflectTraceHeap;
  config.cancelled = shouldCancel;
  config.cancellation_user = cancelUser;
#if defined(INFLECT_LOW_MEMORY)
  config.cooperate = inflectCooperate;
#if TTS_HAS_S8_DOT_SIMD
  config.dot_s8_blocks_32 = inflectDotS8Blocks32;
  config.dot_s8_scaled_blocks_32 = inflectDotS8ScaledBlocks32;
  config.unpack_q4_0_blocks_32 = inflectUnpackQ4Blocks32;
  config.quantize_f32_to_q8_blocks_32 =
      inflectQuantizeF32ToQ8Blocks32;
  config.store_zero_s8_blocks_32 =
      inflectStoreZeroS8Blocks32;
#endif
  config.packed_quant_time_tile = packedQuantTimeTile;
#endif
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
    "/sdcard/tts/InflectV1/acoustic/inflect_acoustic_q4_0_E.gguf";
constexpr const char *kVocoderPath =
    "/sdcard/tts/InflectV1/vocoder/inflect_vocoder_q4_0_E.gguf";
constexpr const char *kCmuDictPath = "/sdcard/tts/InflectV1/cmudict.bin";
constexpr const char *kV2ModelPath =
    "/sdcard/tts/InflectV2/inflect_nano_v2_q4_0.gguf";
constexpr const char *kV2MicroModelPath =
    "/sdcard/tts/InflectV2/inflect_micro_v2_q4_0.gguf";
constexpr const char *kV2GenericModelPath =
    "/sdcard/tts/InflectV2/model.gguf";
constexpr const char *kV2LexiconPath =
    "/sdcard/tts/InflectV2/lexicon.bin";

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

bool v1FilesPresentForBackend(const ModelPaths &paths,
                              const char *vocoderBackend) {
  return fileExists(paths.acoustic) &&
         (isGriffinLimBackend(vocoderBackend) || fileExists(paths.vocoder)) &&
         fileExists(paths.cmudict);
}

bool v2FilesPresent(const ModelPaths &paths) {
  return fileExists(paths.v2Model) && fileExists(paths.v2Lexicon);
}

bool hasTerminalPunctuation(const std::string &text) {
  if (text.empty()) {
    return false;
  }
  switch (text.back()) {
  case '.':
  case '?':
  case '!':
  case ';':
  case ':':
  case ',':
    return true;
  default:
    return false;
  }
}

ModelVersion selectedModelVersion(const ModelPaths &paths,
                                  const char *vocoderBackend) {
  if (paths.version != ModelVersion::Auto) {
    return paths.version;
  }
  if (v2FilesPresent(paths)) {
    return ModelVersion::V2;
  }
  if (v1FilesPresentForBackend(paths, vocoderBackend)) {
    return ModelVersion::V1;
  }
  if (fileExists(paths.v2Model) || fileExists(paths.v2Lexicon)) {
    return ModelVersion::V2;
  }
  return ModelVersion::V1;
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

  int16_t *next =
      static_cast<int16_t *>(psram::allocate(grown * sizeof(int16_t)));
  if (next == nullptr) {
    ttsLog("[tts] psram pcm alloc failed frames=%u bytes=%u\n",
           static_cast<unsigned>(grown),
           static_cast<unsigned>(grown * sizeof(int16_t)));
    return false;
  }
  if (pcm != nullptr && frames > 0) {
    memcpy(next, pcm, frames * sizeof(int16_t));
  }
  psram::release(pcm);
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
    psram::release(pcm);
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
  paths.v2Model = kV2ModelPath;
  if (fileExists(kV2GenericModelPath)) {
    paths.v2Model = kV2GenericModelPath;
  } else if (!fileExists(kV2ModelPath) && fileExists(kV2MicroModelPath)) {
    paths.v2Model = kV2MicroModelPath;
  }
  paths.v2Lexicon = kV2LexiconPath;
  paths.version = ModelVersion::Auto;
  return true;
}

bool modelFilesPresentForBackend(const ModelPaths &paths, const char *vocoderBackend,
                                 char *missing, size_t missingSize) {
  if (selectedModelVersion(paths, vocoderBackend) == ModelVersion::V2) {
    if (!fileExists(paths.v2Model)) {
      copyError(missing, missingSize, paths.v2Model);
      return false;
    }
    if (!fileExists(paths.v2Lexicon)) {
      copyError(missing, missingSize, paths.v2Lexicon);
      return false;
    }
    if (missing != nullptr && missingSize > 0) {
      missing[0] = '\0';
    }
    return true;
  }
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
  const ModelVersion modelVersion = selectedModelVersion(paths, backend);
  result.modelVersion = modelVersion;
  const int griffinLimIterations = std::max(0, options.griffinLimIterations);
  const int packedQuantTimeTile =
      std::max(1, std::min(64, options.packedQuantTimeTile));
  const auto cancellationRequested = [&options]() {
    return options.shouldCancel != nullptr &&
           options.shouldCancel(options.cancelUser);
  };
  const auto finishCancelled = [&]() {
    out.clear();
    copyError(result.error, sizeof(result.error), "Cancelled");
    ttsLog("[tts] synthesis cancelled\n");
  };
  if (input[0] == '\0') {
    copyError(result.error, sizeof(result.error), "No text");
    return result;
  }
  if (cancellationRequested()) {
    finishCancelled();
    return result;
  }

  char missing[96];
  if (!modelFilesPresentForBackend(paths, backend, missing, sizeof(missing))) {
    snprintf(result.error, sizeof(result.error), "Missing %s", missing);
    ttsLog("[tts] model check failed error=\"%s\"\n", result.error);
    return result;
  }

  std::string normalizedInput;
  if (modelVersion == ModelVersion::V2) {
    normalizedInput.assign(input);
    while (!normalizedInput.empty() &&
           std::isspace(
               static_cast<unsigned char>(normalizedInput.back()))) {
      normalizedInput.pop_back();
    }
    if (!hasTerminalPunctuation(normalizedInput)) {
      normalizedInput.push_back('.');
    }
    input = normalizedInput.c_str();
  }

  const unsigned long startedAt = millis();
  ttsLog("[tts] synth start version=%s text_len=%u speaker=%d "
         "chunk_frames=%d\n",
         modelVersion == ModelVersion::V2 ? "v2" : "v1",
         static_cast<unsigned>(strlen(input)), options.speakerId,
         options.vocoderChunkFrames);
  ttsLog("[tts] build variant low_memory=%d esp_dsp_contig=%d "
         "esp_dsp_strided=%d s8_dot_simd=%d q4_unpack_simd=%d "
         "q8_fused_block=%d q8_round=add-half-trunc q8_lanes=%d "
         "q8_zero_state=scale-only q8_source_reuse=transpose "
         "q8_span_cache=conv1d sparse_dot=transpose backend=%s "
         "griffin_lim_iters=%d profile_acoustic=%d "
         "profile_vocoder=%d acoustic_skip_postnet=%d runtime_backend=%s "
         "runtime_gl_iters=%d packed_quant_tile=%d\n",
#if defined(INFLECT_LOW_MEMORY)
         1,
#else
         0,
#endif
         INFLECT_USE_ESP_DSP_CONTIG,
         INFLECT_USE_ESP_DSP_STRIDED,
         TTS_HAS_S8_DOT_SIMD,
         TTS_HAS_S8_DOT_SIMD,
         TTS_HAS_S8_DOT_SIMD,
         kInflectQ8RoundLanes,
         buildVocoderBackend(),
         INFLECT_GRIFFIN_LIM_ITERS,
         INFLECT_PROFILE_ACOUSTIC_OPS,
         INFLECT_PROFILE_VOCODER_OPS,
         INFLECT_ACOUSTIC_SKIP_POSTNET,
         backend,
         griffinLimIterations,
         packedQuantTimeTile);
  if (modelVersion == ModelVersion::V2) {
    ttsLog("[tts] v2_model=%s\n", paths.v2Model);
    ttsLog("[tts] v2_lexicon=%s\n", paths.v2Lexicon);
  } else {
    ttsLog("[tts] acoustic=%s\n", paths.acoustic);
    ttsLog("[tts] vocoder=%s\n", paths.vocoder);
    ttsLog("[tts] cmudict=%s\n", paths.cmudict);
  }
  logHeapState("start");

  InflectPthreadStackScope pthreadStackScope;
  (void)pthreadStackScope;
  configureInflectRuntime(
      packedQuantTimeTile, options.shouldCancel, options.cancelUser);
#if defined(INFLECT_LOW_MEMORY)
  inflect::Synthesizer::init_backend(2);
#else
  inflect::Synthesizer::init_backend(1);
#endif
  std::unique_ptr<inflect::Synthesizer> transientSynth =
      std::make_unique<inflect::Synthesizer>();
  inflect::Synthesizer *synth = transientSynth.get();
  logHeapState("after_backend");

  if (modelVersion == ModelVersion::V2) {
    ttsLog("[tts] load v2 begin\n");
    if (!synth->load_v2(paths.v2Model, paths.v2Lexicon)) {
      copyError(result.error, sizeof(result.error), "V2 model load failed");
      ttsLog("[tts] load v2 failed\n");
      return result;
    }
    ttsLog("[tts] load v2 ok elapsed_ms=%u\n",
           static_cast<unsigned>(millis() - startedAt));
    logHeapState("after_v2");
  } else {
    ttsLog("[tts] load acoustic begin\n");
    if (!synth->load_acoustic(paths.acoustic)) {
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
      if (!synth->load_vocoder(paths.vocoder)) {
        copyError(result.error, sizeof(result.error), "Vocoder load failed");
        ttsLog("[tts] load vocoder failed\n");
        return result;
      }
      ttsLog("[tts] load vocoder ok elapsed_ms=%u\n",
             static_cast<unsigned>(millis() - startedAt));
    }

    ttsLog("[tts] load cmudict begin\n");
    if (!synth->load_cmudict(paths.cmudict)) {
      copyError(result.error, sizeof(result.error), "CMU dict load failed");
      ttsLog("[tts] load cmudict failed\n");
      return result;
    }
    ttsLog("[tts] load cmudict ok elapsed_ms=%u\n",
           static_cast<unsigned>(millis() - startedAt));
    logHeapState("after_cmudict");
  }

  if (cancellationRequested()) {
    finishCancelled();
    return result;
  }
  ttsLog("[tts] inference begin\n");
  logHeapState("before_inference");
  std::vector<int16_t> chunk;
  bool pcmOk = true;
  size_t rawSamples = 0;
  auto appendAudio = [&](const float *samples, size_t count) {
    if (!pcmOk || samples == nullptr || count == 0) {
      return;
    }
    constexpr size_t kPcmChunkSamples = 512;
    chunk.resize(std::min(kPcmChunkSamples, count));
    size_t cursor = 0;
    while (cursor < count) {
      const size_t nextCount = std::min(chunk.size(), count - cursor);
      for (size_t i = 0; i < nextCount; i++) {
        chunk[i] = floatToPcm16(samples[cursor + i]);
      }
      if (!out.appendMono(chunk.data(), nextCount)) {
        pcmOk = false;
        return;
      }
      rawSamples += nextCount;
      cursor += nextCount;
    }
  };

  out.setSampleRateHz(static_cast<uint32_t>(synth->sample_rate()));
  if (modelVersion == ModelVersion::V2) {
    inflect::V2SynthParams params;
    params.speed = options.v2Speed;
    params.variation = options.v2Variation;
    params.seed = options.v2Seed;
    params.decoder_chunk_frames = options.v2DecoderChunkFrames;
    synth->synthesize_v2_streaming(input, params, appendAudio);
  } else {
    inflect::SynthParams params;
    params.length_scale = options.lengthScale;
    params.pitch_scale = options.pitchScale;
    params.energy_scale = options.energyScale;
    params.speaker_id = options.speakerId;
    params.seed = options.seed;
    params.vocoder_chunk_frames = options.vocoderChunkFrames;
    params.vocoder_backend = backend;
    params.griffin_lim_iterations = griffinLimIterations;
    std::vector<float> audio = synth->synthesize(input, params);
    logFloatAudioStats("after_synthesis", audio);
    appendAudio(audio.data(), audio.size());
  }
  logHeapState("after_inference");
  if (cancellationRequested()) {
    finishCancelled();
    return result;
  }
  if (!pcmOk) {
    out.clear();
    copyError(result.error, sizeof(result.error), "PCM PSRAM allocation failed");
    return result;
  }
  if (rawSamples == 0) {
    copyError(result.error, sizeof(result.error), "No audio generated");
    ttsLog("[tts] inference produced no audio elapsed_ms=%u\n",
           static_cast<unsigned>(millis() - startedAt));
    return result;
  }
  ttsLog("[tts] inference raw_samples=%u elapsed_ms=%u\n",
         static_cast<unsigned>(rawSamples),
         static_cast<unsigned>(millis() - startedAt));

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
