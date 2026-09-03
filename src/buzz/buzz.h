#pragma once

#include <cstdint>

#if defined(NM_EPD_420_BW)
enum class NmEpd420Tone : uint8_t { Boot, Shutdown, LowBattery, Receive, DeliverySuccess, DeliveryFailure };
bool playNmEpd420Tone(NmEpd420Tone tone, bool directMessage = false);
#endif

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
void playLongPressLeadUp();
bool playNextLeadUpNote();  // Play the next note in the lead-up sequence
void resetLeadUpSequence(); // Reset the lead-up sequence to start from beginning
