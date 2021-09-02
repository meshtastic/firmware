#include "RF95Interface.h"
#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in
#include "RadioLibRF95.h"
#include "error.h"
#include <configuration.h>

#define MAX_POWER 20
// if we use 20 we are limited to 1% duty cycle or hw might overheat.  For continuous operation set a limit of 17
// In theory up to 27 dBm is possible, but the modules installed in most radios can cope with a max of 20.  So BIG WARNING
// if you set power to something higher than 17 or 20 you might fry your board.

#define POWER_DEFAULT 17 // How much power to use if the user hasn't set a power level

RF95Interface::RF95Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, SPIClass &spi)
    : RadioLibInterface(cs, irq, rst, RADIOLIB_NC, spi)
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

    if (power == 0)
        power = POWER_DEFAULT;

    if (power > MAX_POWER) // This chip has lower power limits than some
        power = MAX_POWER;

    limitPower();

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
        res = lora->setCRC(SX126X_LORA_CRC_OFF); //FIXME

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
    RadioLibInterface::reconfigure();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora->setSpreadingFactor(sf);
    if (err != ERR_NONE)
        recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    err = lora->setBandwidth(bw);
    if (err != ERR_NONE)
        recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    err = lora->setCodingRate(cr);
    if (err != ERR_NONE)
        recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    err = lora->setSyncWord(syncWord);
    assert(err == ERR_NONE);

    err = lora->setCurrentLimit(currentLimit);
    assert(err == ERR_NONE);

    err = lora->setPreambleLength(preambleLength);
    assert(err == ERR_NONE);

    err = lora->setFrequency(freq);
    if (err != ERR_NONE)
        recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    if (power > MAX_POWER) // This chip has lower power limits than some
        power = MAX_POWER;
    err = lora->setOutputPower(power);
    if (err != ERR_NONE)
        recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

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

    // Must be done AFTER, starting receive, because startReceive clears (possibly stale) interrupt pending register bits
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
    setStandby(); // First cancel any active receving/sending
    lora->sleep();

    return true;
}
