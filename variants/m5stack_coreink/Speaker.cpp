#include "Speaker.h"

TONE::TONE(void) {
  _volume = 5;
  _begun = false;
}

void TONE::begin() {
  _begun = true;
  ledcSetup(TONE_PIN_CHANNEL, 0, 13);
  ledcAttachPin(PIN_BUZZER, TONE_PIN_CHANNEL);
}

void TONE::end() {
  mute();
  ledcDetachPin(PIN_BUZZER);
  _begun = false;
}

void TONE::tone(uint16_t frequency) {
  if(!_begun) begin();
  ledcWriteTone(TONE_PIN_CHANNEL, frequency);
  ledcWrite(TONE_PIN_CHANNEL, 0x400 >> _volume);
}

void TONE::setVolume(uint8_t volume) {
  _volume = 11 - volume;
}

void TONE::mute() {
  ledcWriteTone(TONE_PIN_CHANNEL, 0);
  digitalWrite(PIN_BUZZER, 0);
}