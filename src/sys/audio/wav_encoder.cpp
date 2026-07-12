#include "sys/audio/wav_encoder.h"

#include <cstring>

namespace {

void writeLe16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

bool wav::writePcmHeader(uint8_t *header, size_t headerSize,
                         uint32_t sampleRate, uint16_t channels,
                         uint16_t bitsPerSample, size_t pcmByteCount) {
  if (header == nullptr || headerSize < kPcmHeaderSize || sampleRate == 0 ||
      channels == 0 || bitsPerSample == 0 || bitsPerSample % 8U != 0U ||
      pcmByteCount > UINT32_MAX - 36U) {
    return false;
  }

  const uint32_t bytesPerSample = bitsPerSample / 8U;
  const uint64_t byteRate = static_cast<uint64_t>(sampleRate) * channels *
                            bytesPerSample;
  const uint32_t blockAlign = channels * bytesPerSample;
  if (byteRate > UINT32_MAX || blockAlign > UINT16_MAX) {
    return false;
  }

  memset(header, 0, kPcmHeaderSize);
  memcpy(header, "RIFF", 4);
  writeLe32(header + 4, 36U + static_cast<uint32_t>(pcmByteCount));
  memcpy(header + 8, "WAVEfmt ", 8);
  writeLe32(header + 16, 16U);
  writeLe16(header + 20, 1U); // integer PCM
  writeLe16(header + 22, channels);
  writeLe32(header + 24, sampleRate);
  writeLe32(header + 28, static_cast<uint32_t>(byteRate));
  writeLe16(header + 32, static_cast<uint16_t>(blockAlign));
  writeLe16(header + 34, bitsPerSample);
  memcpy(header + 36, "data", 4);
  writeLe32(header + 40, static_cast<uint32_t>(pcmByteCount));
  return true;
}
