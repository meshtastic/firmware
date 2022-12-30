#include "configuration.h"
#include "SX126xInterface.h"
#include "error.h"

// Particular boards might define a different max power based on what their hardware can do
#ifndef SX126X_MAX_POWER
#define SX126X_MAX_POWER 22
#endif

template<typename T>
SX126xInterface<T>::SX126xInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : RadioLibInterface(cs, irq, rst, busy, spi, &lora), lora(&module)
{
    LOG_WARN("SX126xInterface(cs=%d, irq=%d, rst=%d, busy=%d)\n", cs, irq, rst, busy);
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template<typename T>
bool SX126xInterface<T>::init()
{
#ifdef SX126X_POWER_EN
    digitalWrite(SX126X_POWER_EN, HIGH);
    pinMode(SX126X_POWER_EN, OUTPUT);
#endif

#if defined(SX126X_RXEN) && (SX126X_RXEN != RADIOLIB_NC) // set not rx or tx mode
    digitalWrite(SX126X_RXEN, LOW); // Set low before becoming an output
    pinMode(SX126X_RXEN, OUTPUT);
#endif
#if defined(SX126X_TXEN) && (SX126X_TXEN != RADIOLIB_NC)
    digitalWrite(SX126X_TXEN, LOW);
    pinMode(SX126X_TXEN, OUTPUT);
#endif

#ifndef SX126X_E22
    float tcxoVoltage = 0; // None - we use an XTAL
#else
    // Use DIO3 to power tcxo per https://github.com/jgromes/RadioLib/issues/12#issuecomment-520695575
    float tcxoVoltage = 1.8;
#endif
    bool useRegulatorLDO = false; // Seems to depend on the connection to pin 9/DCC_SW - if an inductor DCDC?

    RadioLibInterface::init();

    if (power == 0)
        power = SX126X_MAX_POWER;

    if (power > SX126X_MAX_POWER) // This chip has lower power limits than some
        power = SX126X_MAX_POWER;

    limitPower();

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO);
    // \todo Display actual typename of the adapter, not just `SX126x`
    LOG_INFO("SX126x init result %d\n", res);

    LOG_INFO("Frequency set to %f\n", getFreq());
    LOG_INFO("Bandwidth set to %f\n", bw);
    LOG_INFO("Power output set to %d\n", power);

    // current limit was removed from module' ctor
    // override default value (60 mA)
    res = lora.setCurrentLimit(currentLimit);
    LOG_DEBUG("Current limit set to %f\n", currentLimit);
    LOG_DEBUG("Current limit set result %d\n", res);

#if defined(SX126X_TXEN) && (SX126X_TXEN != RADIOLIB_NC)
    // lora.begin sets Dio2 as RF switch control, which is not true if we are manually controlling RX and TX
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setDio2AsRfSwitch(false);
#endif

#if 0
    // Read/write a register we are not using (only used for FSK mode) to test SPI comms
    uint8_t crcLSB = 0;
    int err = lora.readRegister(SX126X_REG_CRC_POLYNOMIAL_LSB, &crcLSB, 1);
    if(err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    //if(crcLSB != 0x0f)
    //    RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    crcLSB = 0x5a;
    err = lora.writeRegister(SX126X_REG_CRC_POLYNOMIAL_LSB, &crcLSB, 1);
    if(err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    err = lora.readRegister(SX126X_REG_CRC_POLYNOMIAL_LSB, &crcLSB, 1);
    if(err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    if(crcLSB != 0x5a)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);
    // If we got this far register accesses (and therefore SPI comms) are good
#endif

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(RADIOLIB_SX126X_LORA_CRC_ON);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template<typename T>
bool SX126xInterface<T>::reconfigure()
{
    RadioLibInterface::reconfigure();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora.setSpreadingFactor(sf);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setBandwidth(bw);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setCodingRate(cr);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_INVALID_RADIO_SETTING);

    // Hmm - seems to lower SNR when the signal levels are high.  Leaving off for now...
    // TODO: Confirm gain registers are okay now
    // err = lora.setRxGain(true);
    // assert(err == RADIOLIB_ERR_NONE);

    err = lora.setSyncWord(syncWord);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setCurrentLimit(currentLimit);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > SX126X_MAX_POWER) // This chip has lower power limits than some
        power = SX126X_MAX_POWER;

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template<typename T>
void INTERRUPT_ATTR SX126xInterface<T>::disableInterrupt()
{
    lora.clearDio1Action();
}

template<typename T>
void SX126xInterface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE)
        LOG_DEBUG("SX126x standby failed with error %d\n", err);

    assert(err == RADIOLIB_ERR_NONE);

#if defined(SX126X_RXEN) && (SX126X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn off RX and TX power
    digitalWrite(SX126X_RXEN, LOW);
#endif
#if defined(SX126X_TXEN) && (SX126X_TXEN != RADIOLIB_NC)
    digitalWrite(SX126X_TXEN, LOW);
#endif

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/**
 * Add SNR data to received messages
 */
template<typename T>
void SX126xInterface<T>::addReceiveMetadata(MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
template<typename T>
void SX126xInterface<T>::configHardwareForSend()
{
#if defined(SX126X_TXEN) && (SX126X_TXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on TX power / off RX power
    digitalWrite(SX126X_TXEN, HIGH);
#endif
#if defined(SX126X_RXEN) && (SX126X_RXEN != RADIOLIB_NC)
    digitalWrite(SX126X_RXEN, LOW);
#endif

    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template<typename T>
void SX126xInterface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

#if defined(SX126X_RXEN) && (SX126X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on RX power / off TX power
    digitalWrite(SX126X_RXEN, HIGH);
#endif
#if defined(SX126X_TXEN) && (SX126X_TXEN != RADIOLIB_NC)
    digitalWrite(SX126X_TXEN, LOW);
#endif

    // int err = lora.startReceive();
    int err = lora.startReceiveDutyCycleAuto(); // We use a 32 bit preamble so this should save some power by letting radio sit in
                                                // standby mostly.
    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
#endif
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
template<typename T>
bool SX126xInterface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel
    int16_t result;

    setStandby();
    result = lora.scanChannel();
    if (result == RADIOLIB_PREAMBLE_DETECTED)
        return true;

    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    return false;
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
template<typename T>
bool SX126xInterface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation.  Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    // FIXME: it would be better to check for preamble, but we currently have our ISR not set to fire for packets that
    // never even get a valid header, so we don't want preamble to get set and stay set due to noise on the network.

    uint16_t irq = lora.getIrqStatus();
    bool hasPreamble = (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID);

    // this is not correct - often always true - need to add an extra conditional
    // size_t bytesPending = lora.getPacketLength();

    // if (hasPreamble) LOG_DEBUG("rx hasPreamble\n");
    return hasPreamble;
}

template<typename T>
bool SX126xInterface<T>::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    // \todo Display actual typename of the adapter, not just `SX126x`
    LOG_DEBUG("sx126x entering sleep mode (FIXME, don't keep config)\n");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    // FIXME - this isn't correct
    // lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig); // Note: we do not keep the config, full reinit will be needed

#ifdef SX126X_POWER_EN
    digitalWrite(SX126X_POWER_EN, LOW);
#endif

    return true;
}
