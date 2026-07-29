/// Short built-in notification cues rendered through the MIDI synthesizer.
#ifndef SYS_AUDIO_NOTIFICATION_SOUND_H
#define SYS_AUDIO_NOTIFICATION_SOUND_H

#include "sys/system_config.h"

/// Plays one cue synchronously and powers the audio path down before returning.
bool playNotificationSound(ConfigNotificationRingtone ringtone);

#endif
