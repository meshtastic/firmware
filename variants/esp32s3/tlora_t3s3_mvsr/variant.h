#pragma once

// LILYGO T3-S3 MVSR. The T3-S3 with a microphone, an amplifier and a vibration
// motor fitted, so the radio wiring and pinout are shared - only the audio
// hardware and the OLED pins differ.
#include "../tlora_t3s3_v1/variant.h"

// Codec2 voice.
//
// MSM261S4030H0R microphone: I2S slave, 24 bits inside a 32-bit slot, so the
// backend takes the top 16 bits. MAX98357A amplifier, plain I2S.
#define HAS_AUDIO_CODEC2
#define AUDIO_I2S_SLOT_BITS 32

// I2S and PTT pins are not in the public documentation for this board. Until
// they are confirmed from the schematic, set them at runtime through
// moduleConfig.audio (i2s_ws, i2s_sd, i2s_din, i2s_sck, ptt_pin), which the
// module reads in preference to any compile-time value.
