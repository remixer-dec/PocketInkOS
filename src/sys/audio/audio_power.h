#ifndef SYS_AUDIO_AUDIO_POWER_H
#define SYS_AUDIO_AUDIO_POWER_H

/// Initializes GPIOs that control codec and speaker power.
void audioPowerBegin();
/// Enables shared audio power rails.
void audioPowerOn();
/// Disables shared audio power rails.
void audioPowerOff();
/// Enables the speaker amplifier.
void audioSpeakerAmpOn();
/// Disables the speaker amplifier.
void audioSpeakerAmpOff();
/// Returns true when audio power rails are enabled.
bool audioPowerIsOn();

#endif
