#include "sys/audio_power.h"

#include "sys/global.h"

#include <Arduino.h>

namespace {

bool audioRailOn = false;
bool speakerAmpOn = false;

} // namespace

void audioPowerBegin() {
  pinMode(PA_CTRL_PIN, OUTPUT);
  pinMode(AUDIO_PWR_PIN, OUTPUT);
  audioSpeakerAmpOff();
  audioPowerOff();
}

void audioPowerOn() {
  pinMode(AUDIO_PWR_PIN, OUTPUT);
  digitalWrite(AUDIO_PWR_PIN, LOW);
  audioRailOn = true;
}

void audioPowerOff() {
  audioSpeakerAmpOff();
  pinMode(AUDIO_PWR_PIN, OUTPUT);
  digitalWrite(AUDIO_PWR_PIN, HIGH);
  audioRailOn = false;
}

void audioSpeakerAmpOn() {
  pinMode(PA_CTRL_PIN, OUTPUT);
  digitalWrite(PA_CTRL_PIN, HIGH);
  speakerAmpOn = true;
}

void audioSpeakerAmpOff() {
  pinMode(PA_CTRL_PIN, OUTPUT);
  digitalWrite(PA_CTRL_PIN, LOW);
  speakerAmpOn = false;
}

bool audioPowerIsOn() { return audioRailOn || speakerAmpOn; }
