#ifndef WAV_ENCODER_H
#define WAV_ENCODER_H

#include <stddef.h>
#include <stdint.h>

namespace wav {

/// Size of a canonical PCM RIFF/WAVE header.
constexpr size_t kPcmHeaderSize = 44;

/// Writes a mono/stereo PCM header in front of already captured sample bytes.
/// The destination must have room for kPcmHeaderSize bytes.
bool writePcmHeader(uint8_t *header, size_t headerSize, uint32_t sampleRate,
                    uint16_t channels, uint16_t bitsPerSample,
                    size_t pcmByteCount);

} // namespace wav

#endif
