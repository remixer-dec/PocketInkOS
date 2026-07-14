#include "sys/audio/audio_playback_renderer.h"

#include "sys/audio/audio_power.h"
#include "sys/drivers/shared_i2c_bus.h"
#include "sys/services/device_controls.h"

#include <Arduino.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

namespace {

constexpr uint8_t kEs8311Address = 0x18;
constexpr int kMclkPin = 14;
constexpr int kBclkPin = 15;
constexpr int kDataInPin = 16;
constexpr int kWsPin = 38;
constexpr int kDataOutPin = 45;
constexpr unsigned long kCodecPowerDelayMs = 50;
constexpr unsigned long kAmpEnableDelayMs = 60;
constexpr uint32_t kPlaybackTaskStackBytes = 8192;
constexpr UBaseType_t kPlaybackTaskPriority = 1;
constexpr BaseType_t kPlaybackTaskCore = 1;
constexpr unsigned long kTaskStopTimeoutMs = 300;
constexpr int kI2cTimeoutMs = 100;

constexpr uint8_t kRegReset = 0x00;
constexpr uint8_t kRegClock1 = 0x01;
constexpr uint8_t kRegClock2 = 0x02;
constexpr uint8_t kRegClock3 = 0x03;
constexpr uint8_t kRegClock4 = 0x04;
constexpr uint8_t kRegClock5 = 0x05;
constexpr uint8_t kRegClock6 = 0x06;
constexpr uint8_t kRegClock7 = 0x07;
constexpr uint8_t kRegClock8 = 0x08;
constexpr uint8_t kRegSdpIn = 0x09;
constexpr uint8_t kRegSdpOut = 0x0a;
constexpr uint8_t kRegSystem0d = 0x0d;
constexpr uint8_t kRegSystem0e = 0x0e;
constexpr uint8_t kRegSystem12 = 0x12;
constexpr uint8_t kRegSystem13 = 0x13;
constexpr uint8_t kRegAdcEq = 0x1c;
constexpr uint8_t kRegDacMute = 0x31;
constexpr uint8_t kRegDacVolume = 0x32;
constexpr uint8_t kRegDacRamp = 0x37;
constexpr uint8_t kRegGpio = 0x44;
constexpr uint8_t kRegGp = 0x45;
constexpr uint8_t kEs8311Resolution16 = 3U << 2;

i2c_master_dev_handle_t codecDevice = NULL;
i2s_chan_handle_t txChannel = NULL;
bool codecReady = false;
bool i2sReady = false;
AudioPlaybackRenderer *playbackOwner = nullptr;

void copyError(char *error, int errorSize, const char *message) {
  if (error == nullptr || errorSize <= 0) {
    return;
  }
  strncpy(error, message != nullptr ? message : "", errorSize - 1);
  error[errorSize - 1] = '\0';
}

bool supportedSampleRate(uint32_t sampleRate) {
  switch (sampleRate) {
  case 8000:
  case 11025:
  case 12000:
  case 16000:
  case 22050:
  case 24000:
  case 32000:
  case 44100:
  case 48000:
    return true;
  default:
    return false;
  }
}

} // namespace

void AudioPlaybackRenderer::stopActive() {
  if (playbackOwner != nullptr) {
    playbackOwner->stop();
  }
}

bool AudioPlaybackRenderer::begin(const AudioPlaybackStream &stream,
                                  const AudioPlaybackConfig &config,
                                  char *error, int errorSize) {
  stop();
  if (stream.fillPcm16 == nullptr) {
    setError(error, errorSize, "No audio stream");
    return false;
  }
  if (config.sampleRate == 0 || config.chunkFrames == 0) {
    setError(error, errorSize, "Bad audio config");
    return false;
  }

  // Decoder/player objects can be freely preloaded, but the board has one I2S
  // output and one codec. Hand ownership over atomically at play time.
  stopActive();
  playbackOwner = this;

  playbackStream = stream;
  playbackConfig = config;
  playbackVolumePercent = config.volumePercent;
  playbackFinished = false;
  stopRequested = false;
  playbackLastPeak = 0;
  playbackChunksWritten = 0;
  playbackPcmHash = 2166136261U;
  playbackSourcePcmHash = 2166136261U;
  playbackLastI2sWriteUs = 0;
  playbackMaxI2sWriteUs = 0;
  playbackLastFrames = 0;
  playbackSourcePeak = 0;
  playbackLastMean = 0;
  playbackStatus = 0;
  playbackEffectiveVolume = 0;

  dmaBuffer = static_cast<int16_t *>(
      heap_caps_malloc(playbackConfig.chunkFrames * 2U * sizeof(int16_t),
                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (dmaBuffer == nullptr) {
    setError(error, errorSize, "No DMA audio buffer");
    stop();
    return false;
  }

  audioSpeakerAmpOff();
  audioPowerOn();
  delay(kCodecPowerDelayMs);
  if (!ensureI2s(error, errorSize)) {
    stop();
    return false;
  }

  memset(dmaBuffer, 0,
         playbackConfig.chunkFrames * 2U * sizeof(int16_t));
  size_t ignoredBytesWritten = 0;
  i2s_channel_write(txChannel, dmaBuffer,
                    playbackConfig.chunkFrames * 2U * sizeof(int16_t),
                    &ignoredBytesWritten, pdMS_TO_TICKS(50));
  if (!ensureCodec(error, errorSize)) {
    stop();
    return false;
  }
  i2s_channel_write(txChannel, dmaBuffer,
                    playbackConfig.chunkFrames * 2U * sizeof(int16_t),
                    &ignoredBytesWritten, pdMS_TO_TICKS(50));
  delay(kAmpEnableDelayMs);
  audioSpeakerAmpOn();
  playbackActive = true;
  taskRunning = true;
  TaskHandle_t createdTask = NULL;
  if (xTaskCreatePinnedToCore(taskEntry, "audio-play", kPlaybackTaskStackBytes,
                              this, kPlaybackTaskPriority, &createdTask,
                              kPlaybackTaskCore) != pdPASS) {
    taskRunning = false;
    setError(error, errorSize, "Audio task failed");
    stop();
    return false;
  }
  taskHandle = createdTask;
  return true;
}

bool AudioPlaybackRenderer::pump(char *error, int errorSize) {
  if (!playbackActive && !taskRunning) {
    if (playbackFinished) {
      return true;
    }
    setError(error, errorSize,
             playbackStatus.load() == 2U ? "I2S write failed"
                                         : "Audio task stopped");
    return false;
  }
  if (txChannel == NULL || dmaBuffer == nullptr || !taskRunning ||
      playbackStream.fillPcm16 == nullptr) {
    setError(error, errorSize, "Audio not active");
    return !playbackActive;
  }
  return true;
}

bool AudioPlaybackRenderer::writeChunk(char *error, int errorSize) {
  const size_t frames =
      playbackStream.fillPcm16(playbackStream.user, dmaBuffer,
                               playbackConfig.chunkFrames);
  if (frames == 0) {
    playbackFinished = true;
    playbackActive = false;
    playbackStatus = 1U;
    return true;
  }

  const uint32_t volume = playbackConfig.playWhenMutedAtMinimumVolume
                              ? deviceEffectiveAlarmVolumePercent(
                                    playbackVolumePercent.load())
                              : deviceEffectiveOutputVolumePercent(
                                    playbackVolumePercent.load());
  const size_t sampleCount = frames * 2U;
  uint16_t sourcePeak = 0;
  uint16_t peak = 0;
  int64_t sum = 0;
  uint32_t sourceHash = playbackSourcePcmHash.load();
  uint32_t hash = playbackPcmHash.load();
  for (size_t i = 0; i < sampleCount; i++) {
    int32_t sample = dmaBuffer[i];
    const int32_t sourceMagnitude = sample < 0 ? -sample : sample;
    if (sourceMagnitude > sourcePeak) {
      sourcePeak = static_cast<uint16_t>(sourceMagnitude > 32767
                                             ? 32767
                                             : sourceMagnitude);
    }
    sourceHash ^= static_cast<uint16_t>(sample);
    sourceHash *= 16777619U;
    if (volume < 100U) {
      sample = sample * static_cast<int32_t>(volume) / 100;
      dmaBuffer[i] = static_cast<int16_t>(sample);
    }
    const int32_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude > peak) {
      peak = static_cast<uint16_t>(magnitude > 32767 ? 32767 : magnitude);
    }
    sum += sample;
    hash ^= static_cast<uint16_t>(sample);
    hash *= 16777619U;
  }
  size_t bytesWritten = 0;
  const size_t byteCount = sampleCount * sizeof(int16_t);
  const uint32_t writeStartedUs = micros();
  const esp_err_t writeResult = i2s_channel_write(
      txChannel, dmaBuffer, byteCount, &bytesWritten, pdMS_TO_TICKS(50));
  const uint32_t writeUs = micros() - writeStartedUs;
  playbackLastI2sWriteUs = writeUs;
  uint32_t previousMax = playbackMaxI2sWriteUs.load();
  while (writeUs > previousMax &&
         !playbackMaxI2sWriteUs.compare_exchange_weak(previousMax, writeUs)) {
  }
  if (writeResult != ESP_OK ||
      bytesWritten != byteCount) {
    setError(error, errorSize, "I2S write failed");
    playbackStatus = 2U;
    playbackActive = false;
    return false;
  }
  playbackLastPeak = peak;
  playbackLastMean =
      static_cast<int16_t>(sum / static_cast<int64_t>(sampleCount));
  playbackLastFrames = static_cast<uint16_t>(frames);
  playbackSourcePeak = sourcePeak;
  playbackSourcePcmHash = sourceHash;
  playbackPcmHash = hash;
  playbackEffectiveVolume = static_cast<uint8_t>(volume);
  playbackChunksWritten++;
  return true;
}

void AudioPlaybackRenderer::stop() {
  const bool ownsHardware = playbackOwner == this;
  stopRequested = true;
  playbackActive = false;
  const unsigned long start = millis();
  while (taskRunning && millis() - start < kTaskStopTimeoutMs) {
    delay(5);
  }
  if (taskRunning && taskHandle != nullptr) {
    vTaskDelete(static_cast<TaskHandle_t>(taskHandle));
    taskRunning = false;
  }
  taskHandle = nullptr;
  if (dmaBuffer != nullptr) {
    heap_caps_free(dmaBuffer);
    dmaBuffer = nullptr;
  }
  if (ownsHardware) {
    shutdownHardware();
    playbackOwner = nullptr;
  }
}

AudioPlaybackDiagnostics AudioPlaybackRenderer::diagnostics() const {
  AudioPlaybackDiagnostics result;
  result.chunksWritten = playbackChunksWritten.load();
  result.sourcePcmHash = playbackSourcePcmHash.load();
  result.pcmHash = playbackPcmHash.load();
  result.lastI2sWriteUs = playbackLastI2sWriteUs.load();
  result.maxI2sWriteUs = playbackMaxI2sWriteUs.load();
  result.lastFrames = playbackLastFrames.load();
  result.sourcePeak = playbackSourcePeak.load();
  result.lastPeak = playbackLastPeak.load();
  result.lastMean = playbackLastMean.load();
  result.status = playbackStatus.load();
  result.effectiveVolume = playbackEffectiveVolume.load();
  return result;
}

void AudioPlaybackRenderer::setVolumePercent(uint8_t percent) {
  playbackVolumePercent = percent > 100U ? 100U : percent;
}

void AudioPlaybackRenderer::taskEntry(void *user) {
  AudioPlaybackRenderer *renderer = static_cast<AudioPlaybackRenderer *>(user);
  if (renderer != nullptr) {
    renderer->taskLoop();
  }
  vTaskDelete(NULL);
}

void AudioPlaybackRenderer::taskLoop() {
  char error[32];
  while (!stopRequested && playbackActive) {
    if (!writeChunk(error, sizeof(error))) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  playbackActive = false;
  taskRunning = false;
}

bool AudioPlaybackRenderer::ensureCodec(char *error, int errorSize) {
  if (codecReady) {
    return true;
  }

  sharedI2cBegin();
  i2c_master_bus_handle_t bus = sharedI2cBusHandle();
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

  if (!supportedSampleRate(playbackConfig.sampleRate)) {
    setError(error, errorSize, "Audio rate unsupported");
    return false;
  }

  // ES8311 needs a reset release before the power-on command. Writing 0x80
  // first leaves some boards with clocks active but the DAC path muted.
  if (!writeCodecRegister(kRegReset, 0x1f)) {
    setError(error, errorSize, "ES8311 reset failed");
    return false;
  }
  delay(20);
  if (!writeCodecRegister(kRegReset, 0x00) ||
      !writeCodecRegister(kRegReset, 0x80) ||
      !configureCodecClock() ||
      !configureCodecFormat() ||
      !startCodecDac()) {
    setError(error, errorSize, "ES8311 init failed");
    return false;
  }

  codecReady = true;
  return true;
}

bool AudioPlaybackRenderer::ensureI2s(char *error, int errorSize) {
  if (i2sReady) {
    return true;
  }

  i2s_chan_config_t chanConfig =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chanConfig, &txChannel, NULL) != ESP_OK) {
    setError(error, errorSize, "I2S channel failed");
    return false;
  }

  i2s_std_config_t stdConfig = {};
  stdConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(playbackConfig.sampleRate);
  stdConfig.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  stdConfig.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  stdConfig.gpio_cfg.mclk = static_cast<gpio_num_t>(kMclkPin);
  stdConfig.gpio_cfg.bclk = static_cast<gpio_num_t>(kBclkPin);
  stdConfig.gpio_cfg.ws = static_cast<gpio_num_t>(kWsPin);
  stdConfig.gpio_cfg.dout = static_cast<gpio_num_t>(kDataOutPin);
  stdConfig.gpio_cfg.din = static_cast<gpio_num_t>(kDataInPin);

  if (i2s_channel_init_std_mode(txChannel, &stdConfig) != ESP_OK ||
      i2s_channel_enable(txChannel) != ESP_OK) {
    setError(error, errorSize, "I2S init failed");
    return false;
  }

  i2sReady = true;
  return true;
}

bool AudioPlaybackRenderer::configureCodecClock() {
  uint8_t value = 0;

  // Use MCLK from the MCU I2S peripheral and keep all ES8311 clocks enabled.
  if (!writeCodecRegister(kRegClock1, 0x3f) ||
      !readCodecRegister(kRegClock6, value)) {
    return false;
  }
  value &= static_cast<uint8_t>(~(1U << 5));
  if (!writeCodecRegister(kRegClock6, value)) {
    return false;
  }

  // The I2S peripheral supplies MCLK at 256 * sampleRate. For the common audio
  // rates supported above, ES8311 uses the same divider set from Espressif's
  // reference driver.
  if (!readCodecRegister(kRegClock2, value)) {
    return false;
  }
  value &= 0x07;
  if (!writeCodecRegister(kRegClock2, value) ||
      !writeCodecRegister(kRegClock3, 0x10) ||
      !writeCodecRegister(kRegClock4, 0x10) ||
      !writeCodecRegister(kRegClock5, 0x00) ||
      !readCodecRegister(kRegClock6, value)) {
    return false;
  }

  value = static_cast<uint8_t>((value & 0xe0) | 0x03);
  if (!writeCodecRegister(kRegClock6, value) ||
      !readCodecRegister(kRegClock7, value)) {
    return false;
  }
  value &= 0xc0;
  return writeCodecRegister(kRegClock7, value) &&
         writeCodecRegister(kRegClock8, 0xff);
}

bool AudioPlaybackRenderer::configureCodecFormat() {
  uint8_t value = 0;
  if (!readCodecRegister(kRegReset, value)) {
    return false;
  }

  value &= static_cast<uint8_t>(~(1U << 6)); // codec serial port slave mode
  return writeCodecRegister(kRegReset, value) &&
         writeCodecRegister(kRegSdpIn, kEs8311Resolution16) &&
         writeCodecRegister(kRegSdpOut, kEs8311Resolution16);
}

bool AudioPlaybackRenderer::startCodecDac() {
  // Keep the analog output muted until its reference and gain are stable. A
  // partially initialized DAC can otherwise start in a noisy state that lasts
  // until the next full track transition.
  if (!writeCodecRegister(kRegDacMute, 0x60) ||
      !writeCodecRegister(kRegSystem0d, 0x01) ||
      !writeCodecRegister(kRegSystem0e, 0x02) ||
      !writeCodecRegister(kRegSystem12, 0x00) ||
      !writeCodecRegister(kRegSystem13, 0x10) ||
      !writeCodecRegister(kRegAdcEq, 0x6a) ||
      !writeCodecRegister(kRegDacRamp, 0x08) ||
      !writeCodecRegister(kRegDacVolume, 0xc0) ||
      !writeCodecRegister(kRegGpio, 0x58) ||
      !writeCodecRegister(kRegGp, 0x00)) {
    return false;
  }
  delay(10);
  return writeCodecRegister(kRegDacMute, 0x00);
}

bool AudioPlaybackRenderer::readCodecRegister(uint8_t reg, uint8_t &value) {
  value = 0;
  if (codecDevice == NULL) {
    return false;
  }
  return i2c_master_transmit_receive(codecDevice, &reg, sizeof(reg), &value,
                                     sizeof(value), kI2cTimeoutMs) == ESP_OK;
}

bool AudioPlaybackRenderer::writeCodecRegister(uint8_t reg, uint8_t value) {
  if (codecDevice == NULL) {
    return false;
  }
  uint8_t data[2] = {reg, value};
  return i2c_master_transmit(codecDevice, data, sizeof(data), kI2cTimeoutMs) ==
         ESP_OK;
}

void AudioPlaybackRenderer::standbyCodec() {
  if (codecDevice == NULL) {
    codecReady = false;
    return;
  }
  writeCodecRegister(kRegDacMute, 0x60);
  writeCodecRegister(kRegDacVolume, 0x00);
  writeCodecRegister(kRegSystem0d, 0x00);
  writeCodecRegister(kRegReset, 0x00);
  codecReady = false;
}

void AudioPlaybackRenderer::stopI2s() {
  if (!i2sReady && txChannel == NULL) {
    return;
  }
  if (txChannel != NULL) {
    i2s_channel_disable(txChannel);
    i2s_del_channel(txChannel);
    txChannel = NULL;
  }
  i2sReady = false;
}

void AudioPlaybackRenderer::shutdownHardware() {
  audioSpeakerAmpOff();
  // ES8311 standby is clocked; put it to sleep before removing I2S/MCLK.
  standbyCodec();
  stopI2s();
  audioPowerOff();
}

void AudioPlaybackRenderer::setError(char *error, int errorSize,
                                     const char *message) {
  copyError(error, errorSize, message);
}
