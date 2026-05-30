#include "sys/audio_capture.h"

#include "sys/touch_input.h"

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

i2c_master_dev_handle_t codecDevice = NULL;
i2s_chan_handle_t rxChannel = NULL;
i2s_chan_handle_t txChannel = NULL;
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

bool AudioCapture::beginPcm16(AudioCaptureResult &out, uint32_t maxDurationMs,
                              char *error, int errorSize) {
  release(out);
  if (!ensureCodec(error, errorSize) || !ensureI2s(error, errorSize)) {
    return false;
  }

  const size_t sampleCount =
      static_cast<size_t>(kSampleRate) * maxDurationMs / 1000U;
  activeCapacity = sampleCount * sizeof(int16_t);
  activeLength = 0;
  activeBuffer = static_cast<uint8_t *>(
      heap_caps_malloc(activeCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (activeBuffer == nullptr) {
    activeBuffer = static_cast<uint8_t *>(malloc(activeCapacity));
  }
  if (activeBuffer == nullptr) {
    setError(error, errorSize, "No audio buffer");
    return false;
  }

  dmaBuffer = static_cast<int16_t *>(
      heap_caps_malloc(kDmaChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (dmaBuffer == nullptr) {
    free(activeBuffer);
    activeBuffer = nullptr;
    activeCapacity = 0;
    setError(error, errorSize, "No DMA audio buffer");
    return false;
  }

  out.data = activeBuffer;
  out.length = 0;
  out.sampleRate = kSampleRate;
  active = true;
  return true;
}

bool AudioCapture::pumpPcm16(AudioCaptureResult &out, char *error,
                             int errorSize) {
  if (!active) {
    return true;
  }
  if (!readPcmChunk(error, errorSize)) {
    active = false;
    return false;
  }
  out.data = activeBuffer;
  out.length = activeLength;
  out.sampleRate = kSampleRate;
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
  out.length = activeLength;
  out.sampleRate = kSampleRate;
  activeBuffer = nullptr;
  activeCapacity = 0;
  activeLength = 0;
  active = false;
}

void AudioCapture::release(AudioCaptureResult &result) {
  if (dmaBuffer != nullptr) {
    heap_caps_free(dmaBuffer);
    dmaBuffer = nullptr;
  }
  if (activeBuffer != nullptr && result.data != activeBuffer) {
    free(activeBuffer);
  }
  activeBuffer = nullptr;
  activeCapacity = 0;
  activeLength = 0;
  active = false;
  if (result.data != nullptr) {
    free(result.data);
  }
  result.data = nullptr;
  result.length = 0;
  result.sampleRate = kSampleRate;
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

  if (!writeCodecRegister(0x00, 0x80) ||
      !writeCodecRegister(0x00, 0x1f) ||
      !writeCodecRegister(0x0d, 0x01) ||
      !writeCodecRegister(0x01, 0x30) ||
      !writeCodecRegister(0x14, 0x1a) ||
      !writeCodecRegister(0x17, 0xc0) ||
      !writeCodecRegister(0x32, 0xc0)) {
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
  if (i2s_new_channel(&chanConfig, &txChannel, &rxChannel) != ESP_OK) {
    setError(error, errorSize, "I2S channel failed");
    return false;
  }

  i2s_std_config_t stdConfig = {};
  stdConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  stdConfig.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  stdConfig.gpio_cfg.mclk = static_cast<gpio_num_t>(kMclkPin);
  stdConfig.gpio_cfg.bclk = static_cast<gpio_num_t>(kBclkPin);
  stdConfig.gpio_cfg.ws = static_cast<gpio_num_t>(kWsPin);
  stdConfig.gpio_cfg.dout = static_cast<gpio_num_t>(kDataOutPin);
  stdConfig.gpio_cfg.din = static_cast<gpio_num_t>(kDataInPin);

  if (i2s_channel_init_std_mode(txChannel, &stdConfig) != ESP_OK ||
      i2s_channel_init_std_mode(rxChannel, &stdConfig) != ESP_OK ||
      i2s_channel_enable(txChannel) != ESP_OK ||
      i2s_channel_enable(rxChannel) != ESP_OK) {
    setError(error, errorSize, "I2S init failed");
    return false;
  }

  i2sReady = true;
  return true;
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

  const size_t frames = bytesRead / (sizeof(int16_t) * 2);
  for (size_t i = 0; i < frames && activeLength + 1 < activeCapacity; i++) {
    int16_t mono = dmaBuffer[i * 2];
    activeBuffer[activeLength++] = static_cast<uint8_t>(mono & 0xff);
    activeBuffer[activeLength++] = static_cast<uint8_t>((mono >> 8) & 0xff);
  }
  return true;
}

bool AudioCapture::writeCodecRegister(uint8_t reg, uint8_t value) {
  if (codecDevice == NULL) {
    return false;
  }
  uint8_t data[2] = {reg, value};
  return i2c_master_transmit(codecDevice, data, sizeof(data), 100) == ESP_OK;
}

void AudioCapture::setError(char *error, int errorSize, const char *message) {
  copyError(error, errorSize, message);
}
