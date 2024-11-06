#if RADIOLIB_EXCLUDE_SX127X != 1
#include "RF95Interface.h"
#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in
#include "RadioLibRF95.h"
#include "configuration.h"
#include "error.h"

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

#ifndef RF95_MAX_POWER
#define RF95_MAX_POWER 20
#endif

// if we use 20 we are limited to 1% duty cycle or hw might overheat.  For continuous operation set a limit of 17
// In theory up to 27 dBm is possible, but the modules installed in most radios can cope with a max of 20.  So BIG WARNING
// if you set power to something higher than 17 or 20 you might fry your board.

#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
// Structure to hold DAC and DB values
typedef struct {
    uint8_t dac;
    uint8_t db;
} DACDB;

// Interpolation function
DACDB interpolate(uint8_t dbm, uint8_t dbm1, uint8_t dbm2, DACDB val1, DACDB val2)
{
    DACDB result;
    double fraction = (double)(dbm - dbm1) / (dbm2 - dbm1);
    result.dac = (uint8_t)(val1.dac + fraction * (val2.dac - val1.dac));
    result.db = (uint8_t)(val1.db + fraction * (val2.db - val1.db));
    return result;
}

// Function to find the correct DAC and DB values based on dBm using interpolation
DACDB getDACandDB(uint8_t dbm)
{
    // Predefined values
    static const struct {
        uint8_t dbm;
        DACDB values;
    }
#ifdef RADIOMASTER_900_BANDIT_NANO
    dbmToDACDB[] = {
        {20, {168, 2}}, // 100mW
        {24, {148, 6}}, // 250mW
        {27, {128, 9}}, // 500mW
        {30, {90, 12}}  // 1000mW
    };
#endif
#ifdef RADIOMASTER_900_BANDIT
    dbmToDACDB[] = {
        {20, {165, 2}}, // 100mW
        {24, {155, 6}}, // 250mW
        {27, {142, 9}}, // 500mW
        {30, {110, 10}} // 1000mW
    };
#endif
    const int numValues = sizeof(dbmToDACDB) / sizeof(dbmToDACDB[0]);

    // Find the interval dbm falls within and interpolate
    for (int i = 0; i < numValues - 1; i++) {
        if (dbm >= dbmToDACDB[i].dbm && dbm <= dbmToDACDB[i + 1].dbm) {
            return interpolate(dbm, dbmToDACDB[i].dbm, dbmToDACDB[i + 1].dbm, dbmToDACDB[i].values, dbmToDACDB[i + 1].values);
        }
    }

    // Return a default value if no match is found and default to 100mW
#ifdef RADIOMASTER_900_BANDIT_NANO
    DACDB defaultValue = {168, 2};
#endif
#ifdef RADIOMASTER_900_BANDIT
    DACDB defaultValue = {165, 2};
#endif
    return defaultValue;
}
#endif

RF95Interface::RF95Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                             RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy)
{
    LOG_DEBUG("RF95Interface(cs=%d, irq=%d, rst=%d, busy=%d)", cs, irq, rst, busy);
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

#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
    // DAC and DB values based on dBm using interpolation
    DACDB dacDbValues = getDACandDB(power);
    int8_t powerDAC = dacDbValues.dac;
    power = dacDbValues.db;
#endif

    if (power > RF95_MAX_POWER) // This chip has lower power limits than some
        power = RF95_MAX_POWER;

    limitPower();

    iface = lora = new RadioLibRF95(&module);

#ifdef RF95_TCXO
    pinMode(RF95_TCXO, OUTPUT);
    digitalWrite(RF95_TCXO, 1);
#endif

    // enable PA
#ifdef RF95_PA_EN
#if defined(RF95_PA_DAC_EN)
#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
    // Use calculated DAC value
    dacWrite(RF95_PA_EN, powerDAC);
#else
    // Use Value set in /*/variant.h
    dacWrite(RF95_PA_EN, RF95_PA_LEVEL);
#endif
#endif
#endif

    /*
    #define RF95_TXEN (22) // If defined, this pin should be set high prior to transmit (controls an external analog switch)
    #define RF95_RXEN (23) // If defined, this pin should be set high prior to receive (controls an external analog switch)
    */

#ifdef RF95_TXEN
    pinMode(RF95_TXEN, OUTPUT);
    digitalWrite(RF95_TXEN, 0);
#endif

#ifdef RF95_FAN_EN
    pinMode(RF95_FAN_EN, OUTPUT);
    digitalWrite(RF95_FAN_EN, 1);
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
    LOG_INFO("RF95 init result %d", res);
    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);
#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
    LOG_INFO("DAC output set to %d", powerDAC);
#endif

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
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("RF95 setSyncWord %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora->setCurrentLimit(currentLimit);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("RF95 setCurrentLimit %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora->setPreambleLength(preambleLength);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("RF95 setPreambleLength %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora->setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > RF95_MAX_POWER) // This chip has lower power limits than some
        power = RF95_MAX_POWER;

#ifdef USE_RF95_RFO
    err = lora->setOutputPower(power, true);
#else
    err = lora->setOutputPower(power);
#endif
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
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("RF95 standby %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
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
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("RF95 startReceive %s%d", radioLibErr, err);
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
        // LOG_DEBUG("Channel is busy!");
        return true;
    }
    if (result != RADIOLIB_CHANNEL_FREE)
        LOG_ERROR("RF95 isChannelActive %s%d", radioLibErr, result);
    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    // LOG_DEBUG("Channel is free!");
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

#ifdef RF95_FAN_EN
    digitalWrite(RF95_FAN_EN, 0);
#endif

    return true;
}
#endif