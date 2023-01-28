#include "configuration.h"
#include "STM32WLxInterface.h"
#include "error.h"

// Particular boards might define a different max power based on what their hardware can do
#ifndef STM32WLx_MAX_POWER
#define STM32WLx_MAX_POWER 22
#endif

template<typename T>
STM32WLxInterface<T>::STM32WLxInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi, const RADIOLIB_PIN_TYPE rfswitch_pins[3], const Module::RfSwitchMode_t rfswitch_table[4])
    : RadioLibInterface(cs, irq, rst, busy, spi, &lora), lora(&module)
{
    LOG_WARN("STM32WLxInterface(cs=%d, irq=%d, rst=%d, busy=%d)\n", cs, irq, rst, busy);
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template<typename T>
bool STM32WLxInterface<T>::init()
{
    RadioLibInterface::init();

    lora.setRfSwitchTable(rfswitch_pins, rfswitch_table); 

    if (power == 0)
        power = STM32WLx_MAX_POWER;

    if (power > STM32WLx_MAX_POWER) // This chip has lower power limits than some
        power = STM32WLx_MAX_POWER;

    limitPower();

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);

    LOG_INFO("STM32WLx init result %d\n", res);

    LOG_INFO("Frequency set to %f\n", getFreq());
    LOG_INFO("Bandwidth set to %f\n", bw);
    LOG_INFO("Power output set to %d\n", power);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template<typename T>
bool STM32WLxInterface<T>::reconfigure()
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

    err = lora.setSyncWord(syncWord);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setCurrentLimit(currentLimit);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > STM32WLx_MAX_POWER) // This chip has lower power limits than some
        power = STM32WLx_MAX_POWER;

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template<typename T>
void INTERRUPT_ATTR STM32WLxInterface<T>::disableInterrupt()
{
    lora.clearDio1Action();
}

template<typename T>
void STM32WLxInterface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE)
        LOG_DEBUG("STM32WLx standby failed with error %d\n", err);

    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/**
 * Add SNR data to received messages
 */
template<typename T>
void STM32WLxInterface<T>::addReceiveMetadata(MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
template<typename T>
void STM32WLxInterface<T>::configHardwareForSend()
{
    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template<typename T>
void STM32WLxInterface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

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
bool STM32WLxInterface<T>::isChannelActive()
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
bool STM32WLxInterface<T>::isActivelyReceiving()
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
bool STM32WLxInterface<T>::sleep()
{
    LOG_DEBUG("STM32WLx entering sleep mode (FIXME, don't keep config)\n");
    setStandby(); // Stop any pending operations

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig); // Note: we do not keep the config, full reinit will be needed

    return true;
}