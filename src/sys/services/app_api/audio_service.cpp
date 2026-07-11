#include "sys/services/app_api/audio_service.h"

#include "sys/psram_allocator.h"

#include <cstring>
#include <new>
#include <strings.h>

namespace app_api {
namespace {

struct MidiMemoryReader {
  const uint8_t *data = nullptr;
  size_t size = 0;
  size_t offset = 0;
};

int readMidiMemoryByte(void *user) {
  MidiMemoryReader *reader = static_cast<MidiMemoryReader *>(user);
  if (reader == nullptr || reader->data == nullptr ||
      reader->offset >= reader->size) {
    return -1;
  }
  return reader->data[reader->offset++];
}

int readMidiFileByte(void *user) {
  File *file = static_cast<File *>(user);
  return file != nullptr && *file ? file->read() : -1;
}

bool extensionEquals(const char *path, const char *extension) {
  const char *dot = path != nullptr ? strrchr(path, '.') : nullptr;
  return dot != nullptr && strcasecmp(dot + 1, extension) == 0;
}

bool midiFormat(const char *format) {
  return format != nullptr &&
         (strcasecmp(format, "mid") == 0 || strcasecmp(format, "midi") == 0 ||
          strcasecmp(format, "kar") == 0);
}

} // namespace

AudioService::AudioService(FileService &files) : files(files) {}

size_t AudioService::fillMidiPcm(void *user, int16_t *frames, size_t count) {
  MidiPlayer *player = static_cast<MidiPlayer *>(user);
  return player != nullptr ? player->synth.render(frames, count) : 0;
}

AudioService::Slot *AudioService::slot(uint32_t handle) {
  return handle > 0 && handle <= kMaxSlots ? &slots[handle - 1U] : nullptr;
}

AudioService::Slot *AudioService::emptySlot(uint32_t &handle) {
  for (uint8_t i = 0; i < kMaxSlots; i++) {
    if (slots[i].kind == SlotKind::Empty) {
      handle = i + 1U;
      return &slots[i];
    }
  }
  return nullptr;
}

void AudioService::releaseSlot(Slot &slot) {
  if (slot.kind == SlotKind::Midi) {
    slot.midi.renderer.stop();
    remidi_free_sequence(&slot.midi.sequence);
    slot.midi.synth.reset();
    slot.midi.loaded = false;
    slot.midi.rendererReaped = true;
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  if (slot.kind == SlotKind::Extended && slot.audio != nullptr) {
    slot.audio->~Audio();
    psram::release(slot.audio);
    slot.audio = nullptr;
  }
#endif
  slot.kind = SlotKind::Empty;
  slot.playbackStarted = false;
}

bool AudioService::loadMidiBytes(MidiPlayer &player, const uint8_t *data,
                                 size_t size) {
  player.renderer.stop();
  remidi_free_sequence(&player.sequence);
  player.synth.reset();
  player.loaded = false;
  MidiMemoryReader source{data, size, 0};
  remidi_reader reader{&source, readMidiMemoryByte};
  player.loaded = data != nullptr && size > 0 &&
                  remidi_read_sequence(&reader, &player.sequence) == REMIDI_OK;
  return player.loaded;
}

bool AudioService::loadMidiPath(MidiPlayer &player, const char *path) {
  player.renderer.stop();
  remidi_free_sequence(&player.sequence);
  player.synth.reset();
  player.loaded = false;
  File file;
  if (files.openRead(path, file).state == ServiceState::Failed) {
    return false;
  }
  remidi_reader reader{&file, readMidiFileByte};
  player.loaded = remidi_read_sequence(&reader, &player.sequence) == REMIDI_OK;
  return player.loaded;
}

bool AudioService::formatSupported(const char *format) const {
  if (midiFormat(format)) {
    return true;
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  return format != nullptr &&
         (strcasecmp(format, "wav") == 0 || strcasecmp(format, "mp3") == 0 ||
          strcasecmp(format, "aac") == 0 || strcasecmp(format, "m4a") == 0 ||
          strcasecmp(format, "pcm16-mono-16k") == 0);
#else
  return false;
#endif
}

AudioHandleResult AudioService::loadPath(const char *path) {
  File probe;
  ServiceResult opened = files.openRead(path, probe);
  if (opened.state == ServiceState::Failed) {
    return {opened, -1};
  }
  probe.close();
  uint32_t handle = 0;
  Slot *target = emptySlot(handle);
  if (target == nullptr) {
    return {ServiceResult::fail(ServiceError::Busy, "no free audio slot"), -1};
  }
  if (extensionEquals(path, "mid") || extensionEquals(path, "midi") ||
      extensionEquals(path, "kar")) {
    if (!loadMidiPath(target->midi, path)) {
      return {ServiceResult::fail(ServiceError::Io, "MIDI preload failed"), -1};
    }
    target->kind = SlotKind::Midi;
    return {ServiceResult::ok(), static_cast<int32_t>(handle)};
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  void *memory = psram::allocate(sizeof(Audio));
  if (memory == nullptr) {
    return {ServiceResult::fail(ServiceError::OutOfMemory,
                                "audio allocation failed"), -1};
  }
  target->audio = new (memory) Audio(path, files.providerId());
  if (!target->audio->preload()) {
    target->audio->~Audio();
    psram::release(target->audio);
    target->audio = nullptr;
    return {ServiceResult::fail(ServiceError::Io, "audio preload failed"), -1};
  }
  target->kind = SlotKind::Extended;
  return {ServiceResult::ok(), static_cast<int32_t>(handle)};
#else
  return {ServiceResult::fail(ServiceError::Unsupported,
                              "audio format unavailable"), -1};
#endif
}

AudioHandleResult AudioService::loadBuffer(const uint8_t *data, size_t size,
                                           const char *format) {
  if (data == nullptr || size == 0 || format == nullptr) {
    return {ServiceResult::fail(ServiceError::InvalidArgument,
                                "invalid audio buffer"), -1};
  }
  uint32_t handle = 0;
  Slot *target = emptySlot(handle);
  if (target == nullptr) {
    return {ServiceResult::fail(ServiceError::Busy, "no free audio slot"), -1};
  }
  if (midiFormat(format)) {
    if (!loadMidiBytes(target->midi, data, size)) {
      return {ServiceResult::fail(ServiceError::Io, "MIDI preload failed"), -1};
    }
    target->kind = SlotKind::Midi;
    return {ServiceResult::ok(), static_cast<int32_t>(handle)};
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  AudioCodec codec = AudioCodec::Auto;
  if (strcasecmp(format, "wav") == 0) {
    codec = AudioCodec::Wav;
  } else if (strcasecmp(format, "mp3") == 0) {
    codec = AudioCodec::Mp3;
  } else if (strcasecmp(format, "aac") == 0 || strcasecmp(format, "m4a") == 0) {
    codec = AudioCodec::Aac;
  } else if (strcasecmp(format, "pcm16-mono-16k") == 0) {
    codec = AudioCodec::Pcm16Mono16k;
  }
  if (codec == AudioCodec::Auto) {
    return {ServiceResult::fail(ServiceError::Unsupported,
                                "audio format unavailable"), -1};
  }
  void *memory = psram::allocate(sizeof(Audio));
  if (memory == nullptr) {
    return {ServiceResult::fail(ServiceError::OutOfMemory,
                                "audio allocation failed"), -1};
  }
  target->audio = new (memory) Audio(data, size, codec);
  if (!target->audio->preload()) {
    target->audio->~Audio();
    psram::release(target->audio);
    target->audio = nullptr;
    return {ServiceResult::fail(ServiceError::Io, "audio preload failed"), -1};
  }
  target->kind = SlotKind::Extended;
  return {ServiceResult::ok(), static_cast<int32_t>(handle)};
#else
  return {ServiceResult::fail(ServiceError::Unsupported,
                              "audio format unavailable"), -1};
#endif
}

ServiceResult AudioService::play(uint32_t handle) {
  Slot *target = slot(handle);
  if (target == nullptr || target->kind == SlotKind::Empty) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid audio handle");
  }
  if (target->kind == SlotKind::Midi) {
    target->midi.renderer.stop();
    if (!target->midi.loaded ||
        !target->midi.synth.begin(&target->midi.sequence, 16000)) {
      return ServiceResult::fail(ServiceError::Hardware, "MIDI synth failed");
    }
    AudioPlaybackStream stream{&target->midi, fillMidiPcm};
    AudioPlaybackConfig config;
    config.sampleRate = 16000;
    config.chunkFrames = 256;
    config.volumePercent = 100;
    char error[48] = {};
    if (!target->midi.renderer.begin(stream, config, error, sizeof(error))) {
      return ServiceResult::fail(ServiceError::Hardware, "audio output failed");
    }
    target->midi.rendererReaped = false;
    target->playbackStarted = true;
    return ServiceResult::ok();
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  if (target->audio != nullptr) {
    target->playbackStarted = target->audio->play();
    return target->playbackStarted
               ? ServiceResult::ok()
               : ServiceResult::fail(ServiceError::Hardware,
                                     "audio playback failed");
  }
#endif
  return ServiceResult::fail(ServiceError::Unsupported,
                             "audio slot is unavailable");
}

ServiceResult AudioService::stop(uint32_t handle) {
  Slot *target = slot(handle);
  if (target == nullptr || target->kind == SlotKind::Empty) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid audio handle");
  }
  if (target->kind == SlotKind::Midi) {
    target->midi.renderer.stop();
    target->midi.rendererReaped = true;
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  else if (target->audio != nullptr) {
    target->audio->stop();
  }
#endif
  target->playbackStarted = false;
  return ServiceResult::ok();
}

ServiceResult AudioService::update(uint32_t handle) {
  Slot *target = slot(handle);
  if (target == nullptr || target->kind == SlotKind::Empty) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid audio handle");
  }
  if (!target->playbackStarted) {
    return ServiceResult::ok();
  }
  if (target->kind == SlotKind::Midi) {
    if (target->midi.renderer.active()) {
      return ServiceResult::pending();
    }
    const bool finished = target->midi.renderer.finished();
    if (!target->midi.rendererReaped) {
      target->midi.renderer.stop();
      target->midi.rendererReaped = true;
    }
    target->playbackStarted = false;
    return finished ? ServiceResult::end() : ServiceResult::ok();
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  if (target->audio != nullptr) {
    target->audio->update();
    if (target->audio->playing()) {
      return ServiceResult::pending();
    }
    const bool finished = target->audio->finished();
    target->playbackStarted = false;
    return finished ? ServiceResult::end() : ServiceResult::ok();
  }
#endif
  return ServiceResult::fail(ServiceError::Unsupported,
                             "audio slot is unavailable");
}

void AudioService::release(uint32_t handle) {
  Slot *target = slot(handle);
  if (target != nullptr) {
    releaseSlot(*target);
  }
}

void AudioService::releaseAll() {
  for (Slot &target : slots) {
    releaseSlot(target);
  }
}

} // namespace app_api
