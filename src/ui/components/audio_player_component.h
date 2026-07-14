#ifndef AUDIO_PLAYER_COMPONENT_H
#define AUDIO_PLAYER_COMPONENT_H

#include <Adafruit_GFX.h>
#include <stdint.h>

struct TouchPoint;

using AudioArtworkRenderer = bool (*)(void *user, Adafruit_GFX &gfx,
                                      int16_t x, int16_t y, int16_t width,
                                      int16_t height);

/// Touch actions emitted by the audio player component.
enum class AudioPlayerAction : uint8_t {
  None,
  TogglePlayPause,
  PreviousTrack,
  NextTrack,
  VolumeDown,
  VolumeUp,
  Seek,
};

/// Visual arrangements corresponding to the four built-in player concepts.
enum class AudioPlayerLayoutStyle : uint8_t {
  Artwork,
  Minimal,
  Mixer,
  Controls,
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
  bool canPrevious = false;
  bool canNext = false;
  /// Optional pre-rendered 1-bit cover. Decoding and caching stay caller-owned.
  const uint8_t *artwork = nullptr;
  uint16_t artworkWidth = 0;
  uint16_t artworkHeight = 0;
  /// Optional caller-owned renderer for encoded or cached cover artwork.
  AudioArtworkRenderer renderArtwork = nullptr;
  void *artworkUser = nullptr;
};

/// Returns the shell-wide player layout selected in system settings.
AudioPlayerLayoutStyle audioPlayerConfiguredLayout();

/// Shows the current volume in the corner on the player's next frame.
void audioPlayerShowVolumeFeedback();

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
