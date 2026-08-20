#pragma once

#include <math.h>

class ButterworthFilter
{
  public:
    enum PassType {
        Highpass,
        Lowpass,
    };

    ButterworthFilter();
    ButterworthFilter(float frequency, int sampleRate, PassType passType, float resonance);
    void SetParameters(float frequency, int sampleRate, PassType passType, float resonance);
    float Update(float newInput);

  private:
    float c, a1, a2, a3, b1, b2;

    // Array of input values, latest are in front
    float inputHistory[2];

    // Array of output values, latest are in front
    float outputHistory[3];
};
