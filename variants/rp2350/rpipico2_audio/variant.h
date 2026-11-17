#pragma once

// Raspberry Pi Pico 2 with codec2 voice on wired I2S audio. Radio, button and
// battery sense are the base variant's; only the audio hardware is added.
#include "../rpipico2/variant.h"

// Capture and playback sit on separate buses. The RP2350 I2S is PIO driven and
// takes its word clock from the pin after BCLK, so each pair has to be
// consecutive; AudioIO_RP2350.cpp static_asserts that.
//
// Microphone: INMP441 / ICS43434 / MSM261 class, 24 bits in a 32-bit slot.
// Amplifier: MAX98357A, 16 bit, SD_MODE tied to a GPIO so it can sleep.
#define HAS_AUDIO_CODEC2
#define AUDIO_I2S_SLOT_BITS 32

#define AUDIO_MIC_BCLK 6
#define AUDIO_MIC_WS 7 // must be AUDIO_MIC_BCLK + 1
#define AUDIO_MIC_DATA 8

#define AUDIO_SPK_BCLK 18
#define AUDIO_SPK_LRCLK 19 // must be AUDIO_SPK_BCLK + 1
#define AUDIO_SPK_DATA 21
#define AUDIO_SPK_SD 16 // MAX98357 SD_MODE

// BUTTON_PIN is the UI button, so there is no spare key for PTT. Set
// moduleConfig.audio.ptt_pin to a free GPIO to transmit; until then the module
// receives only.
