#pragma once

#include <Arduino.h>
#include <stdint.h>

#define NOTE_G1   49
#define NOTE_G2   98
#define NOTE_G3   196
#define NOTE_GS2  104
#define NOTE_GS3  208
#define NOTE_GS4  415
#define NOTE_GS5  831
#define NOTE_G4   392
#define NOTE_C2   65
#define NOTE_C3   131
#define NOTE_C4   262
#define NOTE_C5   523
#define NOTE_E6   1319
#define NOTE_CS3H 139
#define NOTE_CS5  554
#define NOTE_CS6  1109
#define NOTE_CS7  2217
#define TONES_REPEAT 0xffff

class ArduboyTones {
  bool (*_enabled)();

  void play(uint16_t frequency, uint16_t duration) {
#if defined(PIN_BUZZER)
    if (!_enabled || _enabled())
      ::tone(PIN_BUZZER, frequency, duration);
#else
    (void)frequency;
    (void)duration;
#endif
  }

public:
  explicit ArduboyTones(bool (*enabled)()) : _enabled(enabled) {}

  void tone(uint16_t f1, uint16_t d1) { play(f1, d1); }
  void tone(uint16_t f1, uint16_t d1, uint16_t, uint16_t) { play(f1, d1); }
  void tone(uint16_t f1, uint16_t d1, uint16_t, uint16_t, uint16_t, uint16_t) { play(f1, d1); }
  void tones(const uint16_t *sequence) {
    if (sequence)
      play(pgm_read_word(sequence), pgm_read_word(sequence + 1));
  }
};
