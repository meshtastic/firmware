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

// Only reached with HAS_AUDIO_CODEC2 set; without a backend the build would
// otherwise fail at link with an undefined audioIOBegin.
#if !defined(ARCH_ESP32) && !defined(ARCH_NRF52) && !defined(__PLAT_RP2350__)
#error "HAS_AUDIO_CODEC2: no audio backend for this architecture"
#endif

// Largest codec2 frame we carry: 40 ms at 8 kHz.
#define AUDIO_FRAME_SAMPLES_MAX 320

// I2S slot width. Digital MEMS mics of the INMP441 / ICS43434 / MSM261 class
// emit 24 bits inside a 32-bit slot and need 32 here; the backend then takes
// the top 16 bits. Variants override in variant.h.
#ifndef AUDIO_I2S_SLOT_BITS
#define AUDIO_I2S_SLOT_BITS 16
#endif

// Boards wire capture and playback to separate I2S buses as often as to one.
// A variant defining AUDIO_MIC_* and AUDIO_SPK_* gets two ports; otherwise the
// pins come from moduleConfig.audio as a single shared bus.
#if defined(AUDIO_MIC_BCLK) && defined(AUDIO_SPK_BCLK)
#define AUDIO_SPLIT_BUS 1
#endif

bool audioIOBegin(uint32_t sampleRateHz, size_t frameSamples);
void audioIOEnd();

// Power the mic and the amplifier where the board has enable lines. Half duplex
// means only one is ever wanted, and an idle amplifier still draws current.
void audioIOEnable(bool capture, bool playback);

// Both return samples actually transferred, which may be short of the request.
size_t audioIORead(int16_t *dst, size_t samples, uint32_t timeoutMs);
size_t audioIOWrite(const int16_t *src, size_t samples, uint32_t timeoutMs);

// Capture halves lost because the caller did not drain in time. Diagnostic.
extern uint32_t audioPdmOverruns;
