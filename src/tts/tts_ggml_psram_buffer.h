#ifndef TTS_GGML_PSRAM_BUFFER_H
#define TTS_GGML_PSRAM_BUFFER_H

#include <ggml-backend.h>

namespace tts {

ggml_backend_buffer_type_t psramCpuBufferType();

} // namespace tts

#endif
