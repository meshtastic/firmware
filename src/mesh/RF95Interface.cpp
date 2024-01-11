#include "RF95Interface.h"
#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in
#include "RadioLibRF95.h"
#include "configuration.h"
#include "error.h"

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

#define MAX_POWER 20
// if we use 20 we are limited to 1% duty cycle or hw might overheat.  For continuous operation set a limit of 17
// In theory up to 27 dBm is possible, but the modules installed in most radios can cope with a max of 20.  So BIG WARNING
// if you set power to something higher than 17 or 20 you might fry your board.

#define POWER_DEFAULT 17 // How much power to use if the user hasn't set a power level

RF95Interface::RF95Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                             RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy)
{
    LOG_WARN("RF95Interface(cs=%d, irq=%d, rst=%d, busy=%d)\n", cs, irq, rst, busy);
}

/** Some boards require GPIO control of tx vs rx paths */
void RF95Interface::setTransmitEnable(bool txon)
{
#ifdef RF95_TXEN
    digitalWrite(RF95_TXEN, txon ? 1 : 0);
#elif ARCH_PORTDUINO
    if (settingsMap[txen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[txen], txon ? 1 : 0);
    }
#endif

#ifdef RF95_RXEN
    digitalWrite(RF95_RXEN, txon ? 0 : 1);
#elif ARCH_PORTDUINO
    if (settingsMap[rxen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[rxen], txon ? 0 : 1);
    }
#endif
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
bool RF95Interface::init()
{
    RadioLibInterface::init();

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
#if ARCH_PORTDUINO
    if (settingsMap[txen] != RADIOLIB_NC) {
        pinMode(settingsMap[txen], OUTPUT);
        digitalWrite(settingsMap[txen], 0);
    }
    if (settingsMap[rxen] != RADIOLIB_NC) {
        pinMode(settingsMap[rxen], OUTPUT);
        digitalWrite(settingsMap[rxen], 0);
    }
#endif
    setTransmitEnable(false);

    int res = lora->begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength);
    LOG_INFO("RF95 init result %d\n", res);
    LOG_INFO("Frequency set to %f\n", getFreq());
    LOG_INFO("Bandwidth set to %f\n", bw);
    LOG_INFO("Power output set to %d\n", power);

    if (res == RADIOLIB_ERR_NONE)
        res = lora->setCRC(RADIOLIB_SX126X_LORA_CRC_ON);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
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
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora->setBandwidth(bw);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora->setCodingRate(cr);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora->setSyncWord(syncWord);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora->setCurrentLimit(currentLimit);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora->setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora->setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > MAX_POWER) // This chip has lower power limits than some
        power = MAX_POWER;

    err = lora->setOutputPower(power);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

/**
 * Add SNR data to received messages
 */
void RF95Interface::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    mp->rx_snr = lora->getSNR();
    mp->rx_rssi = lround(lora->getRSSI());
}

void RF95Interface::setStandby()
{
    int err = lora->standby();
    assert(err == RADIOLIB_ERR_NONE);

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
    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting receive, because startReceive clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
}

bool RF95Interface::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel
    int16_t result;
    setTransmitEnable(false);
    setStandby(); // needed for smooth transition
    result = lora->scanChannel();

    if (result == RADIOLIB_PREAMBLE_DETECTED) {
        // LOG_DEBUG("Channel is busy!\n");
        return true;
    }
    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    // LOG_DEBUG("Channel is free!\n");
    return false;
}

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
bool RF95Interface::isActivelyReceiving()
{
    return lora->isReceiving();
}

bool RF95Interface::sleep()
{
    // put chipset into sleep mode
    setStandby(); // First cancel any active receiving/sending
    lora->sleep();

    return true;
}