#include "RadioLibRF95.h"

#define RF95_CHIP_VERSION 0x12
#define RF95_ALT_VERSION 0x11 // Supposedly some versions of the chip have id 0x11

// From datasheet but radiolib doesn't know anything about this
#define SX127X_REG_TCXO 0x4B


RadioLibRF95::RadioLibRF95(Module *mod) : SX1278(mod) {}

int16_t RadioLibRF95::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord, int8_t power, uint8_t currentLimit,
                            uint16_t preambleLength, uint8_t gain)
{
    // execute common part
    int16_t state = SX127x::begin(RF95_CHIP_VERSION, syncWord, currentLimit, preambleLength);
    if (state != ERR_NONE)
        state = SX127x::begin(RF95_ALT_VERSION, syncWord, currentLimit, preambleLength);
    RADIOLIB_ASSERT(state);

    // configure settings not accessible by API
    state = config();
    RADIOLIB_ASSERT(state);

#ifdef RF95_TCXO
    state = _mod->SPIsetRegValue(SX127X_REG_TCXO, 0x10 | _mod->SPIgetRegValue(SX127X_REG_TCXO));
    RADIOLIB_ASSERT(state);
#endif

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

#define RH_RF95_MODEM_STATUS_CLEAR 0x10
#define RH_RF95_MODEM_STATUS_HEADER_INFO_VALID 0x08
#define RH_RF95_MODEM_STATUS_RX_ONGOING 0x04
#define RH_RF95_MODEM_STATUS_SIGNAL_SYNCHRONIZED 0x02
#define RH_RF95_MODEM_STATUS_SIGNAL_DETECTED 0x01

bool RadioLibRF95::isReceiving()
{
    // 0x0b == Look for header info valid, signal synchronized or signal detected
    uint8_t reg = readReg(SX127X_REG_MODEM_STAT);
    // Serial.printf("reg %x\n", reg);
    return (reg & (RH_RF95_MODEM_STATUS_SIGNAL_DETECTED | RH_RF95_MODEM_STATUS_SIGNAL_SYNCHRONIZED |
                   RH_RF95_MODEM_STATUS_HEADER_INFO_VALID)) != 0;
}

uint8_t RadioLibRF95::readReg(uint8_t addr)
{
    return _mod->SPIreadRegister(addr);
}