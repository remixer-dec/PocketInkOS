/// Session-owned audio handle table for external apps. It normalizes MIDI and
/// edition-specific codecs behind load/play/update/release operations while
/// bounding the number of simultaneously preloaded sources.
#ifndef EXT_APP_API_AUDIO_SERVICE_H
#define EXT_APP_API_AUDIO_SERVICE_H

#include "fs/providers/resonatto/remidi.h"
#include "fs/providers/resonatto/remidi_synth.h"
#include "sys/audio/audio_playback_renderer.h"
#include "sys/ext_app_api/service_result.h"
#include "sys/ext_app_api/file_service.h"

#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
#include "sys/audio/audio.h"
#endif

namespace ext_app_api {

/// Result of loading a source into the bounded session handle table.
struct AudioHandleResult {
  /// Structured outcome of the load attempt.
  ServiceResult result;
  /// Non-negative slot index on success, otherwise -1.
  int32_t handle = -1;
};

/// Owns the bounded set of audio sources preloaded by one external app session.
class AudioService {
public:
  /// Creates a session audio service using files from the supplied facade.
  explicit AudioService(FileService &files);

  /// Reports whether this firmware edition can preload the named format.
  bool formatSupported(const char *format) const;
  /// Preloads an SD/provider path and returns its reusable session handle.
  AudioHandleResult loadPath(const char *path);
  /// Preloads a copied encoded buffer and returns its reusable session handle.
  AudioHandleResult loadBuffer(const uint8_t *data, size_t size,
                               const char *format);
  /// Starts the selected preloaded source from its beginning.
  ServiceResult play(uint32_t handle);
  /// Stops the selected source without discarding its preload state.
  ServiceResult stop(uint32_t handle);
  /// Reaps completed playback and reports whether the source has ended.
  ServiceResult update(uint32_t handle);
  /// Releases one handle and all decoder or renderer state behind it.
  void release(uint32_t handle);
  /// Releases every handle at the external-app session boundary.
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

} // namespace ext_app_api

#endif
