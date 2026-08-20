#pragma once

#include "configuration.h"
#include <stddef.h>
#include <stdint.h>

// Digital audio in and out for the codec2 module. Pins come from
// moduleConfig.audio; the backend owns everything platform specific so the
// module deals only in int16 frames at 8 kHz.
//
// One backend per architecture, selected by the build. No analogue paths: a
// board whose mic or amp is not I2S, an I2C-configured I2S codec, or PDM does
// not set HAS_AUDIO_CODEC2.

// Largest codec2 frame we carry: 40 ms at 8 kHz.
#define AUDIO_FRAME_SAMPLES_MAX 320

// I2S slot width. Digital MEMS mics of the INMP441 / ICS43434 / MSM261 class
// emit 24 bits inside a 32-bit slot and need 32 here; the backend then takes
// the top 16 bits. Variants override in variant.h.
#ifndef AUDIO_I2S_SLOT_BITS
#define AUDIO_I2S_SLOT_BITS 16
#endif

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples);
void audioIOEnd();

// Both return samples actually transferred, which may be short of the request.
size_t audioIORead(int16_t *dst, size_t samples, uint32_t timeoutMs);
size_t audioIOWrite(const int16_t *src, size_t samples, uint32_t timeoutMs);
