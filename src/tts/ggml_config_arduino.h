#pragma once

#ifndef GGML_VERSION
#define GGML_VERSION "0.15.2"
#endif

#ifndef GGML_COMMIT
#define GGML_COMMIT "vendored"
#endif

#ifndef GGML_CPU_GENERIC
#define GGML_CPU_GENERIC
#endif

// Current fastest measured ESP32-S3 TTS variant uses strided ESP-DSP dot
// products. Keep both knobs build-time configurable for benchmarking.
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
