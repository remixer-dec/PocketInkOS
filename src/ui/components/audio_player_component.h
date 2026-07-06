#ifndef AUDIO_PLAYER_COMPONENT_H
#define AUDIO_PLAYER_COMPONENT_H

#include <Adafruit_GFX.h>
#include <stdint.h>

struct TouchPoint;

/// Touch actions emitted by the audio player component.
enum class AudioPlayerAction : uint8_t {
  None,
  TogglePlayPause,
  VolumeDown,
  VolumeUp,
  Seek,
};

/// Result of hit-testing the audio player component.
struct AudioPlayerEvent {
  AudioPlayerAction action = AudioPlayerAction::None;
  uint16_t seekPermille = 0;
};

/// Render state for the audio player component.
struct AudioPlayerState {
  const char *title = nullptr;
  const char *subtitle = nullptr;
  const char *detail = nullptr;
  bool playing = false;
  AudioPlayerAction pressedAction = AudioPlayerAction::None;
  uint8_t volumePercent = 80;
  bool muted = false;
  uint32_t positionMs = 0;
  uint32_t durationMs = 0;
};

/// Draws and hit-tests the compact/fullscreen audio playback controls.
class AudioPlayerComponent {
public:
  /// Draws the player in compact or fullscreen layout.
  void draw(Adafruit_GFX &gfx, const AudioPlayerState &state,
            bool fullscreen) const;
  /// Maps a touch point to a player action.
  AudioPlayerEvent hitTest(const TouchPoint &point, bool fullscreen,
                           uint32_t durationMs) const;
};

#endif
