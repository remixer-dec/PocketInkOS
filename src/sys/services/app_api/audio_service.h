#ifndef APP_API_AUDIO_SERVICE_H
#define APP_API_AUDIO_SERVICE_H

#include "fs/providers/resonatto/remidi.h"
#include "fs/providers/resonatto/remidi_synth.h"
#include "sys/drivers/audio_playback_renderer.h"
#include "sys/services/app_api/service_result.h"
#include "sys/services/app_api/file_service.h"

#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
#include "sys/audio.h"
#endif

namespace app_api {

struct AudioHandleResult {
  ServiceResult result;
  int32_t handle = -1;
};

class AudioService {
public:
  explicit AudioService(FileService &files);

  bool formatSupported(const char *format) const;
  AudioHandleResult loadPath(const char *path);
  AudioHandleResult loadBuffer(const uint8_t *data, size_t size,
                               const char *format);
  ServiceResult play(uint32_t handle);
  ServiceResult stop(uint32_t handle);
  ServiceResult update(uint32_t handle);
  void release(uint32_t handle);
  void releaseAll();

private:
  static constexpr uint8_t kMaxSlots = 4;

  struct MidiPlayer {
    remidi_sequence sequence = {};
    RemidiSynth synth;
    AudioPlaybackRenderer renderer;
    bool loaded = false;
    bool rendererReaped = true;
  };

  enum class SlotKind : uint8_t { Empty, Midi, Extended };

  struct Slot {
    MidiPlayer midi;
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
    Audio *audio = nullptr;
#endif
    SlotKind kind = SlotKind::Empty;
    bool playbackStarted = false;
  };

  FileService &files;
  Slot slots[kMaxSlots];

  Slot *slot(uint32_t handle);
  Slot *emptySlot(uint32_t &handle);
  void releaseSlot(Slot &slot);
  bool loadMidiBytes(MidiPlayer &player, const uint8_t *data, size_t size);
  bool loadMidiPath(MidiPlayer &player, const char *path);
  static size_t fillMidiPcm(void *user, int16_t *frames, size_t count);
};

} // namespace app_api

#endif
