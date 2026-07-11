#include "sys/drivers/audio_capture.h"
#include "sys/psram_allocator.h"
#include "sys/wav_encoder.h"

#include "sys/drivers/audio_power.h"
#include "sys/drivers/audio_playback_renderer.h"
#include "sys/drivers/touch_input.h"

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <cstring>
#include <cstdlib>

namespace {
constexpr uint8_t kEs8311Address = 0x18;
constexpr uint32_t kSampleRate = 16000;
constexpr int kMclkPin = 14;
constexpr int kBclkPin = 15;
constexpr int kDataInPin = 16;
constexpr int kWsPin = 38;
constexpr int kDataOutPin = 45;
constexpr size_t kDmaChunkBytes = 1024;
constexpr int kI2cTimeoutMs = 100;

i2c_master_dev_handle_t codecDevice = NULL;
i2s_chan_handle_t rxChannel = NULL;
bool codecReady = false;
bool i2sReady = false;

void copyError(char *error, int errorSize, const char *message) {
  if (error == nullptr || errorSize <= 0) {
    return;
  }
  strncpy(error, message ? message : "", errorSize - 1);
  error[errorSize - 1] = '\0';
}
} // namespace

bool AudioCapture::recordPcm16(AudioCaptureResult &out, uint32_t durationMs,
                               char *error, int errorSize) {
  if (!beginPcm16(out, durationMs, error, errorSize)) {
    return false;
  }
  while (active) {
    if (!pumpPcm16(out, error, errorSize)) {
      return false;
    }
  }
  finishPcm16(out);
  return true;
}

bool AudioCapture::recordWav(AudioCaptureResult &out, uint32_t durationMs,
                             char *error, int errorSize) {
  if (!beginWav(out, durationMs, error, errorSize)) {
    return false;
  }
  while (active) {
    if (!pumpPcm16(out, error, errorSize)) {
      return false;
    }
  }
  finishPcm16(out);
  return true;
}

bool AudioCapture::beginPcm16(AudioCaptureResult &out, uint32_t maxDurationMs,
                              char *error, int errorSize) {
  return begin(out, maxDurationMs, false, error, errorSize);
}

bool AudioCapture::beginWav(AudioCaptureResult &out, uint32_t maxDurationMs,
                            char *error, int errorSize) {
  return begin(out, maxDurationMs, true, error, errorSize);
}

bool AudioCapture::begin(AudioCaptureResult &out, uint32_t maxDurationMs,
                         bool wavOutput, char *error, int errorSize) {
  AudioPlaybackRenderer::stopActive();
  release(out);
  audioSpeakerAmpOff();
  audioPowerOn();
  hardwareActive = true;
  delay(50);
  if (!ensureCodec(error, errorSize) || !ensureI2s(error, errorSize)) {
    shutdownHardware();
    return false;
  }

  const uint64_t sampleCount64 =
      static_cast<uint64_t>(kSampleRate) * maxDurationMs / 1000U;
  if (sampleCount64 == 0 ||
      sampleCount64 >
          (SIZE_MAX - wav::kPcmHeaderSize) / sizeof(int16_t)) {
    setError(error, errorSize, "Bad audio duration");
    shutdownHardware();
    return false;
  }
  const size_t sampleCount = static_cast<size_t>(sampleCount64);
  activeDataOffset = wavOutput ? wav::kPcmHeaderSize : 0;
  activeCapacity = sampleCount * sizeof(int16_t);
  activeLength = 0;
  activeWav = wavOutput;
  // Recordings are intentionally strict PSRAM allocations: falling back to
  // scarce internal DRAM for a multi-second clip can destabilize networking.
  activeBuffer = static_cast<uint8_t *>(
      psram::allocate(activeDataOffset + activeCapacity));
  if (activeBuffer == nullptr) {
    setError(error, errorSize, "No audio buffer");
    shutdownHardware();
    return false;
  }

  dmaBuffer = static_cast<int16_t *>(
      heap_caps_malloc(kDmaChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (dmaBuffer == nullptr) {
    psram::release(activeBuffer);
    activeBuffer = nullptr;
    activeCapacity = 0;
    setError(error, errorSize, "No DMA audio buffer");
    shutdownHardware();
    return false;
  }

  out.data = activeBuffer;
  out.length = activeDataOffset;
  out.sampleRate = kSampleRate;
  out.wav = activeWav;
  active = true;
  return true;
}

bool AudioCapture::pumpPcm16(AudioCaptureResult &out, char *error,
                             int errorSize) {
  if (!active) {
    return true;
  }
  if (!readPcmChunk(error, errorSize)) {
    release(out);
    return false;
  }
  out.data = activeBuffer;
  out.length = activeDataOffset + activeLength;
  out.sampleRate = kSampleRate;
  out.wav = activeWav;
  if (activeLength >= activeCapacity) {
    active = false;
  }
  return true;
}

void AudioCapture::finishPcm16(AudioCaptureResult &out) {
  if (dmaBuffer != nullptr) {
    heap_caps_free(dmaBuffer);
    dmaBuffer = nullptr;
  }
  out.data = activeBuffer;
  out.length = activeDataOffset + activeLength;
  out.sampleRate = kSampleRate;
  out.wav = activeWav;
  if (activeWav &&
      !wav::writePcmHeader(out.data, activeDataOffset, kSampleRate, 1, 16,
                           activeLength)) {
    out.length = 0;
  }
  activeBuffer = nullptr;
  activeCapacity = 0;
  activeLength = 0;
  activeDataOffset = 0;
  active = false;
  activeWav = false;
  shutdownHardware();
}

void AudioCapture::release(AudioCaptureResult &result) {
  if (dmaBuffer != nullptr) {
    heap_caps_free(dmaBuffer);
    dmaBuffer = nullptr;
  }
  if (activeBuffer != nullptr && result.data != activeBuffer) {
    psram::release(activeBuffer);
  }
  activeBuffer = nullptr;
  activeCapacity = 0;
  activeLength = 0;
  activeDataOffset = 0;
  active = false;
  activeWav = false;
  shutdownHardware();
  if (result.data != nullptr) {
    psram::release(result.data);
  }
  result.data = nullptr;
  result.length = 0;
  result.sampleRate = kSampleRate;
  result.wav = false;
}

bool AudioCapture::ensureCodec(char *error, int errorSize) {
  if (codecReady) {
    return true;
  }

  i2c_master_bus_handle_t bus = touchI2cBusHandle();
  if (bus == NULL) {
    setError(error, errorSize, "Touch I2C not ready");
    return false;
  }

  if (codecDevice == NULL) {
    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = kEs8311Address;
    devConfig.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &devConfig, &codecDevice) != ESP_OK) {
      setError(error, errorSize, "ES8311 not found");
      return false;
    }
  }

  // Configure the complete ADC path. The previous demo-sized sequence powered
  // the analog block but never enabled the ADC ramp/serial output, producing
  // correctly sized recordings containing only zero samples.
  uint8_t serialOut = 0;
  if (!writeCodecRegister(0x44, 0x08) ||
      !writeCodecRegister(0x44, 0x08) ||
      !writeCodecRegister(0x01, 0x30) ||
      !writeCodecRegister(0x02, 0x00) ||
      !writeCodecRegister(0x03, 0x10) ||
      !writeCodecRegister(0x04, 0x20) ||
      !writeCodecRegister(0x05, 0x00) ||
      !writeCodecRegister(0x06, 0x03) ||
      !writeCodecRegister(0x07, 0x00) ||
      !writeCodecRegister(0x08, 0xff) ||
      !writeCodecRegister(0x0b, 0x00) ||
      !writeCodecRegister(0x0c, 0x00) ||
      !writeCodecRegister(0x10, 0x1f) ||
      !writeCodecRegister(0x11, 0x7f) ||
      !writeCodecRegister(0x00, 0x80) ||
      !writeCodecRegister(0x01, 0x3f) ||
      !writeCodecRegister(0x13, 0x10) ||
      !writeCodecRegister(0x1b, 0x0a) ||
      !writeCodecRegister(0x1c, 0x6a) ||
      !writeCodecRegister(0x44, 0x58) ||
      !readCodecRegister(0x0a, serialOut) ||
      !writeCodecRegister(0x0a,
                          static_cast<uint8_t>((serialOut & 0xe0) | 0x0c)) ||
      !writeCodecRegister(0x17, 0xbf) ||
      !writeCodecRegister(0x0e, 0x02) ||
      !writeCodecRegister(0x14, 0x1a) ||
      !writeCodecRegister(0x0d, 0x01) ||
      !writeCodecRegister(0x15, 0x40) ||
      !writeCodecRegister(0x16, 0x05) ||
      !writeCodecRegister(0x45, 0x00)) {
    setError(error, errorSize, "ES8311 init failed");
    return false;
  }

  codecReady = true;
  return true;
}

bool AudioCapture::ensureI2s(char *error, int errorSize) {
  if (i2sReady) {
    return true;
  }

  i2s_chan_config_t chanConfig =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chanConfig, NULL, &rxChannel) != ESP_OK) {
    setError(error, errorSize, "I2S channel failed");
    return false;
  }

  i2s_std_config_t stdConfig = {};
  stdConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  stdConfig.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  // ES8311 is a mono codec. Ask the RX driver for one sample per frame so the
  // byte count matches the mono WAV rate. Treating this buffer as interleaved
  // stereo discarded every second sample and made a 7 s manual recording
  // produce roughly 3.5 s of PCM.
  stdConfig.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  stdConfig.gpio_cfg.mclk = static_cast<gpio_num_t>(kMclkPin);
  stdConfig.gpio_cfg.bclk = static_cast<gpio_num_t>(kBclkPin);
  stdConfig.gpio_cfg.ws = static_cast<gpio_num_t>(kWsPin);
  stdConfig.gpio_cfg.dout = static_cast<gpio_num_t>(kDataOutPin);
  stdConfig.gpio_cfg.din = static_cast<gpio_num_t>(kDataInPin);

  if (i2s_channel_init_std_mode(rxChannel, &stdConfig) != ESP_OK ||
      i2s_channel_enable(rxChannel) != ESP_OK) {
    setError(error, errorSize, "I2S init failed");
    return false;
  }

  i2sReady = true;
  return true;
}

void AudioCapture::shutdownHardware() {
  if (!hardwareActive && !i2sReady && rxChannel == NULL) {
    return;
  }
  audioSpeakerAmpOff();
  stopI2s();
  standbyCodec();
  audioPowerOff();
  hardwareActive = false;
}

void AudioCapture::stopI2s() {
  if (!i2sReady && rxChannel == NULL) {
    return;
  }
  if (rxChannel != NULL) {
    i2s_channel_disable(rxChannel);
    i2s_del_channel(rxChannel);
    rxChannel = NULL;
  }
  i2sReady = false;
}

void AudioCapture::standbyCodec() {
  if (codecDevice == NULL) {
    codecReady = false;
    return;
  }

  // Best-effort ES8311 idle sequence. The rail is cut afterward, so failure is
  // not fatal; this just avoids leaving analog blocks active while powered.
  writeCodecRegister(0x32, 0x00);
  writeCodecRegister(0x14, 0x00);
  writeCodecRegister(0x0d, 0x00);
  writeCodecRegister(0x00, 0x00);
  codecReady = false;
}

bool AudioCapture::readPcmChunk(char *error, int errorSize) {
  if (rxChannel == NULL || dmaBuffer == nullptr || activeBuffer == nullptr) {
    setError(error, errorSize, "Audio not active");
    return false;
  }
  size_t bytesRead = 0;
  esp_err_t err = i2s_channel_read(rxChannel, dmaBuffer, kDmaChunkBytes,
                                   &bytesRead, pdMS_TO_TICKS(20));
  if (err != ESP_OK) {
    setError(error, errorSize, "I2S read failed");
    return false;
  }
  if (bytesRead == 0) {
    return true;
  }

  const size_t samples = bytesRead / sizeof(int16_t);
  for (size_t i = 0; i < samples && activeLength + 1 < activeCapacity; i++) {
    const int16_t mono = dmaBuffer[i];
    activeBuffer[activeDataOffset + activeLength++] =
        static_cast<uint8_t>(mono & 0xff);
    activeBuffer[activeDataOffset + activeLength++] =
        static_cast<uint8_t>((mono >> 8) & 0xff);
  }
  return true;
}

bool AudioCapture::readCodecRegister(uint8_t reg, uint8_t &value) {
  value = 0;
  return codecDevice != NULL &&
         i2c_master_transmit_receive(codecDevice, &reg, sizeof(reg), &value,
                                     sizeof(value), kI2cTimeoutMs) == ESP_OK;
}

bool AudioCapture::writeCodecRegister(uint8_t reg, uint8_t value) {
  if (codecDevice == NULL) {
    return false;
  }
  uint8_t data[2] = {reg, value};
  return i2c_master_transmit(codecDevice, data, sizeof(data), kI2cTimeoutMs) ==
         ESP_OK;
}

void AudioCapture::setError(char *error, int errorSize, const char *message) {
  copyError(error, errorSize, message);
}
