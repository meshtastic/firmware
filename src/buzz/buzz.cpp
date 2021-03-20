#include "buzz.h"
#include "configuration.h"

#ifdef NRF52_SERIES
#include "variant.h"
#endif

#ifndef PIN_BUZZER

// Noop methods for boards w/o buzzer
void playBeep(){};
void playStartMelody(){};
void playShutdownMelody(){};

#else
#include "Tone.h"

extern "C" void delay(uint32_t dwMs);

struct ToneDuration {
  int frequency_khz;
  int duration_ms;
};

// Some common frequencies.
#define NOTE_C3 131
#define NOTE_CS3 139
#define NOTE_D3 147
#define NOTE_DS3 156
#define NOTE_E3 165
#define NOTE_F3 175
#define NOTE_FS3 185
#define NOTE_G3 196
#define NOTE_GS3 208
#define NOTE_A3 220
#define NOTE_AS3 233
#define NOTE_B3 247

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