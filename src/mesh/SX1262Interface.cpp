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
    RadioLibInterface::init();

    float tcxoVoltage = 0;        // None - we use an XTAL
    bool useRegulatorLDO = false; // Seems to depend on the connection to pin 9/DCC_SW - if an inductor DCDC?

    applyModemConfig();
    if (power > 22) // This chip has lower power limits than some
        power = 22;
    int res = lora.begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength, tcxoVoltage, useRegulatorLDO);
    DEBUG_MSG("LORA init result %d\n", res);

    if (res == ERR_NONE)
        res = lora.setCRC(SX126X_LORA_CRC_ON);

    if (res == ERR_NONE)
        startReceive(); // start receiving

    return res == ERR_NONE;
}

bool SX1262Interface::reconfigure()
{
    applyModemConfig();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora.setSpreadingFactor(sf);
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

    startReceive(); // restart receiving

    return ERR_NONE;
}

void SX1262Interface::setStandby()
{
    int err = lora.standby();
    assert(err == ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/**
 * Add SNR data to received messages
 */
void SX1262Interface::addReceiveMetadata(MeshPacket *mp)
{
    mp->rx_snr = lora.getSNR();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

void SX1262Interface::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else
    setStandby();
    // int err = lora.startReceive();
    int err = lora.startReceiveDutyCycleAuto(); // We use a 32 bit preamble so this should save some power by letting radio sit in
                                                // standby mostly.
    assert(err == ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
#endif
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
bool SX1262Interface::isActivelyReceiving()
{
    return false; // FIXME
    // FIXME this is not correct - often always true - need to add an extra conditional
    // return lora.getPacketLength() > 0;
}

bool SX1262Interface::sleep()
{
    // put chipset into sleep mode
    disableInterrupt();
    lora.sleep();

    return true;
}