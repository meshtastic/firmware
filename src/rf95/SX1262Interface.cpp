#include "SX1262Interface.h"
#include <configuration.h>

SX1262Interface::SX1262Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : RadioLibInterface(cs, irq, rst, busy, spi, &lora), lora(&module)
{
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
bool SX1262Interface::init()
{
    if (!RadioLibInterface::init())
        return false;

    // FIXME, move this to main
    SPI.begin();

    float tcxoVoltage = 0;        // None - we use an XTAL
    bool useRegulatorLDO = false; // Seems to depend on the connection to pin 9/DCC_SW - if an inductor DCDC?

    applyModemConfig();
    if (power > 22) // This chip has lower power limits than some
        power = 22;
    int res = lora.begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength, tcxoVoltage, useRegulatorLDO);
    DEBUG_MSG("LORA init result %d\n", res);

    if (res == ERR_NONE)
        res = lora.setCRC(SX126X_LORA_CRC_ON);

    return res == ERR_NONE;
}

bool SX1262Interface::reconfigure()
{
    applyModemConfig();

    // set mode to standby
    int err = lora.standby();
    assert(err == ERR_NONE);

    // configure publicly accessible settings
    err = lora.setSpreadingFactor(sf);
    assert(err == ERR_NONE);

    err = lora.setBandwidth(bw);
    assert(err == ERR_NONE);

    err = lora.setCodingRate(cr);
    assert(err == ERR_NONE);

    err = lora.setSyncWord(syncWord);
    assert(err == ERR_NONE);

    err = lora.setCurrentLimit(currentLimit);
    assert(err == ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    assert(err == ERR_NONE);

    err = lora.setFrequency(freq);
    assert(err == ERR_NONE);

    if (power > 22) // This chip has lower power limits than some
        power = 22;
    err = lora.setOutputPower(power);
    assert(err == ERR_NONE);

    assert(0); // FIXME - set mode back to receive?

    return true;
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
bool SX1262Interface::canSendImmediately()
{
    return true; // FIXME
#if 0
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    if (_mode == RHModeIdle || isReceiving()) {
        // if the radio is idle, we can send right away
        DEBUG_MSG("immediate send on mesh fr=0x%x,to=0x%x,id=%d\n (txGood=%d,rxGood=%d,rxBad=%d)\n", p->from, p->to, p->id,
                  txGood(), rxGood(), rxBad());
    }
#endif 
}