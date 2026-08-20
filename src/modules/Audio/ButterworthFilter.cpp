#include "ButterworthFilter.h"

ButterworthFilter::ButterworthFilter() {}

ButterworthFilter::ButterworthFilter(float frequency, int sampleRate, PassType passType, float resonance)
{
    SetParameters(frequency, sampleRate, passType, resonance);
}

void ButterworthFilter::SetParameters(float frequency, int sampleRate, PassType passType, float resonance)
{
    switch (passType) {
    case PassType::Lowpass:
        c = 1.0f / (float)tan(M_PI * frequency / sampleRate);
        a1 = 1.0f / (1.0f + resonance * c + c * c);
        a2 = 2.0f * a1;
        a3 = a1;
        b1 = 2.0f * (1.0f - c * c) * a1;
        b2 = (1.0f - resonance * c + c * c) * a1;
        break;
    case PassType::Highpass:
        c = (float)tan(M_PI * frequency / sampleRate);
        a1 = 1.0f / (1.0f + resonance * c + c * c);
        a2 = -2.0f * a1;
        a3 = a1;
        b1 = 2.0f * (c * c - 1.0f) * a1;
        b2 = (1.0f - resonance * c + c * c) * a1;
        break;
    }
}

float ButterworthFilter::Update(float newInput)
{
    float newOutput = a1 * newInput + a2 * inputHistory[0] + a3 * inputHistory[1] - b1 * outputHistory[0] - b2 * outputHistory[1];

    inputHistory[1] = inputHistory[0];
    inputHistory[0] = newInput;

    outputHistory[2] = outputHistory[1];
    outputHistory[1] = outputHistory[0];
    outputHistory[0] = newOutput;

    return newOutput;
}