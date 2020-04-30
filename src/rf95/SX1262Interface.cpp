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
    int res = lora.begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength, tcxoVoltage, useRegulatorLDO);
    DEBUG_MSG("LORA init result %d\n", res);

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

    err = lora.setOutputPower(power);
    assert(err == ERR_NONE);

    assert(0); // FIXME - set mode back to receive?

    return true;
}
