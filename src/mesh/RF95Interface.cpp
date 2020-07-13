#include "RF95Interface.h"
#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in
#include "RadioLibRF95.h"
#include <configuration.h>

#define MAX_POWER 17
// if we use 20 we are limited to 1% duty cycle or hw might overheat.  For continuous operation set a limit of 17

RF95Interface::RF95Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, SPIClass &spi)
    : RadioLibInterface(cs, irq, rst, 0, spi)
{
    // FIXME - we assume devices never get destroyed
}

/** Some boards require GPIO control of tx vs rx paths */
void RF95Interface::setTransmitEnable(bool txon)
{
#ifdef RF95_TXEN
    digitalWrite(RF95_TXEN, txon ? 1 : 0);
#endif

#ifdef RF95_RXEN
    digitalWrite(RF95_RXEN, txon ? 0 : 1);
#endif
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
bool RF95Interface::init()
{
    RadioLibInterface::init();

    applyModemConfig();
    if (power > MAX_POWER) // This chip has lower power limits than some
        power = MAX_POWER;

    iface = lora = new RadioLibRF95(&module);

#ifdef RF95_TCXO
    pinMode(RF95_TCXO, OUTPUT);
    digitalWrite(RF95_TCXO, 1);
#endif

    /*
    #define RF95_TXEN (22) // If defined, this pin should be set high prior to transmit (controls an external analog switch)
    #define RF95_RXEN (23) // If defined, this pin should be set high prior to receive (controls an external analog switch)
    */

#ifdef RF95_TXEN
    pinMode(RF95_TXEN, OUTPUT);
    digitalWrite(RF95_TXEN, 0);
#endif

#ifdef RF95_RXEN
    pinMode(RF95_RXEN, OUTPUT);
    digitalWrite(RF95_RXEN, 1);
#endif
    setTransmitEnable(false);

    int res = lora->begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength);
    DEBUG_MSG("RF95 init result %d\n", res);

    if (res == ERR_NONE)
        res = lora->setCRC(SX126X_LORA_CRC_ON);

    if (res == ERR_NONE)
        startReceive(); // start receiving

    return res == ERR_NONE;
}

void INTERRUPT_ATTR RF95Interface::disableInterrupt()
{
    lora->clearDio0Action();
}

bool RF95Interface::reconfigure()
{
    applyModemConfig();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora->setSpreadingFactor(sf);
    assert(err == ERR_NONE);

    err = lora->setBandwidth(bw);
    assert(err == ERR_NONE);

    err = lora->setCodingRate(cr);
    assert(err == ERR_NONE);

    err = lora->setSyncWord(syncWord);
    assert(err == ERR_NONE);

    err = lora->setCurrentLimit(currentLimit);
    assert(err == ERR_NONE);

    err = lora->setPreambleLength(preambleLength);
    assert(err == ERR_NONE);

    err = lora->setFrequency(freq);
    assert(err == ERR_NONE);

    if (power > MAX_POWER) // This chip has lower power limits than some
        power = MAX_POWER;
    err = lora->setOutputPower(power);
    assert(err == ERR_NONE);

    startReceive(); // restart receiving

    return ERR_NONE;
}

/**
 * Add SNR data to received messages
 */
void RF95Interface::addReceiveMetadata(MeshPacket *mp)
{
    mp->rx_snr = lora->getSNR();
}

void RF95Interface::setStandby()
{
    int err = lora->standby();
    assert(err == ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/** We override to turn on transmitter power as needed.
 */
void RF95Interface::configHardwareForSend()
{
    setTransmitEnable(true);

    RadioLibInterface::configHardwareForSend();
}

void RF95Interface::startReceive()
{
    setTransmitEnable(false);
    setStandby();
    int err = lora->startReceive();
    assert(err == ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
bool RF95Interface::isActivelyReceiving()
{
    return lora->isReceiving();
}

bool RF95Interface::sleep()
{
    // put chipset into sleep mode
    disableInterrupt();
    lora->sleep();

    return true;
}
