#include "RadioLibRF95.h"

#define RFM95_CHIP_VERSION 0x12
#define RFM95_ALT_VERSION 0x11 // Supposedly some versions of the chip have id 0x11

RadioLibRF95::RadioLibRF95(Module *mod) : SX1278(mod) {}

int16_t RadioLibRF95::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, uint8_t currentLimit,
                            uint16_t preambleLength, uint8_t gain)
{
    // execute common part
    int16_t state = SX127x::begin(RFM95_CHIP_VERSION, syncWord, currentLimit, preambleLength);
    if (state != ERR_NONE)
        state = SX127x::begin(RFM95_ALT_VERSION, syncWord, currentLimit, preambleLength);
    RADIOLIB_ASSERT(state);

    // configure settings not accessible by API
    state = config();
    RADIOLIB_ASSERT(state);

    // configure publicly accessible settings
    state = setFrequency(freq);
    RADIOLIB_ASSERT(state);

    state = setBandwidth(bw);
    RADIOLIB_ASSERT(state);

    state = setSpreadingFactor(sf);
    RADIOLIB_ASSERT(state);

    state = setCodingRate(cr);
    RADIOLIB_ASSERT(state);

    state = setOutputPower(power);
    RADIOLIB_ASSERT(state);

    state = setGain(gain);

    return (state);
}

int16_t RadioLibRF95::setFrequency(float freq)
{
    // RADIOLIB_CHECK_RANGE(freq, 862.0, 1020.0, ERR_INVALID_FREQUENCY);

    // set frequency
    return (SX127x::setFrequencyRaw(freq));
}
