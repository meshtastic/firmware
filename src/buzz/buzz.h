#pragma once

#include <stdint.h>

// Play one tone at an explicit duty cycle, blocking for durationMs; louder than tone()'s fixed 50%
// on a piezo. Clamped to BUZZER_DUTY_MAX_PERCENT; variants opt in with BUZZER_DUTY_PERCENT.
void playToneDuty(uint8_t pin, uint16_t freqHz, uint32_t durationMs, uint8_t dutyPct);

void playBeep();
void playLongBeep();
void playStartMelody();
void playShutdownMelody();
void playGPSEnableBeep();
void playGPSDisableBeep();
void playComboTune();
void play4ClickDown();
void play4ClickUp();
void playBoop();
void playChirp();
void playClick();
bool playNextLeadUpNote();  // Play the next note in the lead-up sequence
void resetLeadUpSequence(); // Reset the lead-up sequence to start from beginning