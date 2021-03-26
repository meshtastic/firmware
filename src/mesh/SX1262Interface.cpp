#include "SX1262Interface.h"
#include "error.h"
#include <configuration.h>

// Particular boards might define a different max power based on what their hardware can do
#ifndef SX1262_MAX_POWER
#define SX1262_MAX_POWER 22
#endif

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
#ifdef SX1262_POWER_EN
    digitalWrite(SX1262_POWER_EN, HIGH);
    pinMode(SX1262_POWER_EN, OUTPUT);
#endif

    RadioLibInterface::init();

#ifdef SX1262_RXEN                  // set not rx or tx mode
    digitalWrite(SX1262_RXEN, LOW); // Set low before becoming an output
    pinMode(SX1262_RXEN, OUTPUT);
#endif
#ifdef SX1262_TXEN
    digitalWrite(SX1262_TXEN, LOW);
    pinMode(SX1262_TXEN, OUTPUT);
#endif

#ifndef SX1262_E22
    float tcxoVoltage = 0; // None - we use an XTAL
#else
    // Use DIO3 to power tcxo per https://github.com/jgromes/RadioLib/issues/12#issuecomment-520695575
    float tcxoVoltage = 1.8; 
#endif
    bool useRegulatorLDO = false; // Seems to depend on the connection to pin 9/DCC_SW - if an inductor DCDC?

    applyModemConfig();

    if (power == 0)
        power = SX1262_MAX_POWER;

    if (power > SX1262_MAX_POWER) // This chip has lower power limits than some
        power = SX1262_MAX_POWER;

    limitPower();

    int res = lora.begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength, tcxoVoltage, useRegulatorLDO);
    DEBUG_MSG("SX1262 init result %d\n", res);

#ifdef SX1262_TXEN
    // lora.begin sets Dio2 as RF switch control, which is not true if we are manually controlling RX and TX
    if (res == ERR_NONE)
        res = lora.setDio2AsRfSwitch(false);
#endif

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
    if(err != ERR_NONE) recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    err = lora.setBandwidth(bw);
    if(err != ERR_NONE) recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    err = lora.setCodingRate(cr);
    if(err != ERR_NONE) recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    // Hmm - seems to lower SNR when the signal levels are high.  Leaving off for now...
    err = lora.setRxGain(true);
    assert(err == ERR_NONE);

    err = lora.setSyncWord(syncWord);
    assert(err == ERR_NONE);

    err = lora.setCurrentLimit(currentLimit);
    assert(err == ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    assert(err == ERR_NONE);

    err = lora.setFrequency(freq);
    if(err != ERR_NONE) recordCriticalError(CriticalErrorCode_InvalidRadioSetting);

    if (power > 22) // This chip has lower power limits than some
        power = 22;
    err = lora.setOutputPower(power);
    assert(err == ERR_NONE);

    startReceive(); // restart receiving

    return ERR_NONE;
}

void INTERRUPT_ATTR SX1262Interface::disableInterrupt()
{
    lora.clearDio1Action();
}

void SX1262Interface::setStandby()
{
    int err = lora.standby();
    assert(err == ERR_NONE);

#ifdef SX1262_RXEN // we have RXEN/TXEN control - turn off RX and TX power
    digitalWrite(SX1262_RXEN, LOW);
#endif
#ifdef SX1262_TXEN
    digitalWrite(SX1262_TXEN, LOW);
#endif

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/**
 * Add SNR data to received messages
 */
void SX1262Interface::addReceiveMetadata(MeshPacket *mp)
{
    // DEBUG_MSG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
void SX1262Interface::configHardwareForSend()
{
#ifdef SX1262_TXEN // we have RXEN/TXEN control - turn on TX power / off RX power
    digitalWrite(SX1262_TXEN, HIGH);
#endif

    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

void SX1262Interface::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

#ifdef SX1262_RXEN // we have RXEN/TXEN control - turn on RX power / off TX power
    digitalWrite(SX1262_RXEN, HIGH);
#endif

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
    // The IRQ status will be cleared when we start our read operation.  Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    // FIXME: it would be better to check for preamble, but we currently have our ISR not set to fire for packets that
    // never even get a valid header, so we don't want preamble to get set and stay set due to noise on the network.

    uint16_t irq = lora.getIrqStatus();
    bool hasPreamble = (irq & SX126X_IRQ_HEADER_VALID);

    // this is not correct - often always true - need to add an extra conditional
    // size_t bytesPending = lora.getPacketLength();

    // if (hasPreamble) DEBUG_MSG("rx hasPreamble\n");
    return hasPreamble;
}

bool SX1262Interface::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    DEBUG_MSG("sx1262 entering sleep mode (FIXME, don't keep config)\n");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    // FIXME - this isn't correct
    // lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig); // Note: we do not keep the config, full reinit will be needed

#ifdef SX1262_POWER_EN
    digitalWrite(SX1262_POWER_EN, LOW);
#endif

    return true;
}