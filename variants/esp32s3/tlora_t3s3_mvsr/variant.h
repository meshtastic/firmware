#pragma once

// LILYGO T3-S3 MVSR, the LoRa Voice Communication Kit: a T3-S3 with a
// microphone, an amplifier and a vibration motor. Radio wiring, OLED and pinout
// are the base board's, so only the audio hardware differs.
#include "../tlora_t3s3_v1/variant.h"

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

// Codec2 voice.
//
// Capture and playback are on separate I2S buses here, not one shared clock,
// so the backend takes a port each.
//
// MSM261S4030H0R microphone: I2S slave, 24 bits inside a 32-bit slot, so the
// backend keeps the top 16. MAX98357A amplifier on its own bus.
#define HAS_AUDIO_CODEC2
#define AUDIO_I2S_SLOT_BITS 32

#define AUDIO_MIC_BCLK 47
#define AUDIO_MIC_WS 15
#define AUDIO_MIC_DATA 48
#define AUDIO_MIC_EN 35

#define AUDIO_SPK_BCLK 40
#define AUDIO_SPK_LRCLK 41
#define AUDIO_SPK_DATA 39
#define AUDIO_SPK_SD 38 // MAX98357 SD_MODE

// Vibration motor, driven by the external notification module when
// moduleConfig.external_notification.output_vibra points at it.
#define PIN_VIBRATION 46

// No spare button: GPIO0 is the boot strap and belongs to the UI. Set
// moduleConfig.audio.ptt_pin to transmit; until then the module receives only.
