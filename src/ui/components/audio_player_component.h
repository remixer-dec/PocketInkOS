#ifndef AUDIO_PLAYER_COMPONENT_H
#define AUDIO_PLAYER_COMPONENT_H

#include <Adafruit_GFX.h>
#include <stdint.h>

struct TouchPoint;

enum class AudioPlayerAction : uint8_t {
  None,
  TogglePlayPause,
  VolumeDown,
  VolumeUp,
  Seek,
};

struct AudioPlayerEvent {
  AudioPlayerAction action = AudioPlayerAction::None;
  uint16_t seekPermille = 0;
};

struct AudioPlayerState {
  const char *title = nullptr;
  const char *subtitle = nullptr;
  const char *detail = nullptr;
  bool playing = false;
  uint8_t volumePercent = 80;
  uint32_t positionMs = 0;
  uint32_t durationMs = 0;
};

class AudioPlayerComponent {
public:
  void draw(Adafruit_GFX &gfx, const AudioPlayerState &state,
            bool fullscreen) const;
  AudioPlayerEvent hitTest(const TouchPoint &point, bool fullscreen,
                           uint32_t durationMs) const;
};

#endif
