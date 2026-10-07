#include "NRF52I2SOutput.h"

#if defined(HAS_I2S_SPEAKER_NRF52)

#include <nrf_i2s.h>

NRF52I2SOutput nrf52I2SOutput;

bool NRF52I2SOutput::begin(uint8_t sckPin, uint8_t wsPin, uint8_t sdPin)
{
    nrf_i2s_pins_set(NRF_I2S, sckPin, wsPin, NRF_I2S_PIN_NOT_CONNECTED, sdPin, NRF_I2S_PIN_NOT_CONNECTED);
    bool ok = nrf_i2s_configure(NRF_I2S, NRF_I2S_MODE_MASTER, NRF_I2S_FORMAT_I2S, NRF_I2S_ALIGN_LEFT, NRF_I2S_SWIDTH_16BIT,
                                NRF_I2S_CHANNELS_STEREO, NRF_I2S_MCK_32MDIV8, NRF_I2S_RATIO_256X);
    started = ok;
    return ok;
}

void NRF52I2SOutput::fillBuffer(uint32_t frequencyHz)
{
    size_t samplesPerCycle = kSampleRateHz / frequencyHz;
    if (samplesPerCycle < 4)
        samplesPerCycle = 4;
    if (samplesPerCycle > kMaxSamples)
        samplesPerCycle = kMaxSamples;

    // Square wave, same value in both stereo halves (works regardless of SD_MODE strap).
    const int16_t amplitude = 12000; // headroom below full-scale
    const size_t half = samplesPerCycle / 2;
    for (size_t i = 0; i < samplesPerCycle; i++) {
        int16_t sample = (i < half) ? amplitude : (int16_t)-amplitude;
        buffer[i] = ((uint32_t)(uint16_t)sample << 16) | (uint16_t)sample;
    }

    // Buffer pointer is never updated after START, so EasyDMA just keeps replaying it.
    nrf_i2s_transfer_set(NRF_I2S, (uint16_t)samplesPerCycle, NULL, buffer);
}

void NRF52I2SOutput::playTone(uint32_t frequencyHz, uint32_t durationMs)
{
    if (!started || durationMs == 0)
        return;

    if (frequencyHz <= 1) {
        // Rest note.
        delay(durationMs);
        return;
    }

    fillBuffer(frequencyHz);
    nrf_i2s_enable(NRF_I2S);
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);

    delay(durationMs);

    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
    nrf_i2s_disable(NRF_I2S);
}

void NRF52I2SOutput::startTone(uint32_t frequencyHz)
{
    if (!started)
        return;
    stopTone();
    if (frequencyHz <= 1)
        return;

    fillBuffer(frequencyHz);
    nrf_i2s_enable(NRF_I2S);
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);
}

void NRF52I2SOutput::stopTone()
{
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
    nrf_i2s_disable(NRF_I2S);
}

void NRF52I2SOutput::end()
{
    stopTone();
    started = false;
}

#endif // HAS_I2S_SPEAKER_NRF52
