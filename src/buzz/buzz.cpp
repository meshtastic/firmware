#include "buzz.h"
#include "configuration.h"

#ifdef NRF52_SERIES
#include "variant.h"
#endif

#ifndef PIN_BUZZER

void playBeep(){};
void playStartMelody(){};
void playShutdownMelody(){};

#else
#include "Tone.h"
#include "pitches.h"

extern "C" void delay(uint32_t dwMs);

struct ToneDuration {
  int frequency_khz;
  int duration_ms;
};

const int DURATION_1_8 = 125;  // 1/8 note
const int DURATION_1_4 = 250;  // 1/4 note

void playTones(const ToneDuration *tone_durations, int size) {
  for (int i = 0; i < size; i++) {
    const auto &tone_duration = tone_durations[i];
    tone(PIN_BUZZER, tone_duration.frequency_khz, tone_duration.duration_ms);
    // to distinguish the notes, set a minimum time between them.
    delay(1.3 * tone_duration.duration_ms);
  }
}

void playBeep() { tone(PIN_BUZZER, NOTE_B3, DURATION_1_4); }

void playStartMelody() {
  ToneDuration melody[] = {{NOTE_B3, DURATION_1_4},
                           {NOTE_B3, DURATION_1_8},
                           {NOTE_B3, DURATION_1_8}};
  playTones(melody, sizeof(melody) / sizeof(ToneDuration));
}

void playShutdownMelody() {
  ToneDuration melody[] = {{NOTE_B3, DURATION_1_4},
                           {NOTE_G3, DURATION_1_8},
                           {NOTE_D3, DURATION_1_8}};
  playTones(melody, sizeof(melody) / sizeof(ToneDuration));
}
#endif