#include "configuration.h"
#include "SX128xInterface.h"
#include "mesh/NodeDB.h"
#include "error.h"

// Particular boards might define a different max power based on what their hardware can do
#ifndef SX128X_MAX_POWER
#define SX128X_MAX_POWER 13
#endif

template<typename T>
SX128xInterface<T>::SX128xInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : RadioLibInterface(cs, irq, rst, busy, spi, &lora), lora(&module)
{
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template<typename T>
bool SX128xInterface<T>::init()
{
#ifdef SX128X_POWER_EN
    digitalWrite(SX128X_POWER_EN, HIGH);
    pinMode(SX128X_POWER_EN, OUTPUT);
#endif

#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // set not rx or tx mode
    digitalWrite(SX128X_RXEN, LOW); // Set low before becoming an output
    pinMode(SX128X_RXEN, OUTPUT);
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    digitalWrite(SX128X_TXEN, LOW);
    pinMode(SX128X_TXEN, OUTPUT);
#endif

    RadioLibInterface::init();

    if (power == 0)
        power = SX128X_MAX_POWER;

    if (power > SX128X_MAX_POWER) // This chip has lower power limits than some
        power = SX128X_MAX_POWER;

    limitPower();

    preambleLength = 12; // 12 is the default for this chip, 32 does not RX at all

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength);
    // \todo Display actual typename of the adapter, not just `SX128x`
    LOG_INFO("SX128x init result %d\n", res);

    if((config.lora.region != Config_LoRaConfig_RegionCode_LORA_24) && (res == RADIOLIB_ERR_INVALID_FREQUENCY)) {
        LOG_WARN("Radio chip only supports 2.4GHz LoRa. Adjusting Region and rebooting.\n");
        config.lora.region = Config_LoRaConfig_RegionCode_LORA_24;
        nodeDB.saveToDisk(SEGMENT_CONFIG);
        delay(2000);
#if defined(ARCH_ESP32)
        ESP.restart();
#elif defined(ARCH_NRF52)
        NVIC_SystemReset();
#else
        LOG_ERROR("FIXME implement reboot for this platform. Skipping for now.\n");
#endif
    }

    LOG_INFO("Frequency set to %f\n", getFreq());    
    LOG_INFO("Bandwidth set to %f\n", bw);    
    LOG_INFO("Power output set to %d\n", power);    

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template<typename T>
bool SX128xInterface<T>::reconfigure()
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

    err = lora.setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > SX128X_MAX_POWER) // This chip has lower power limits than some
        power = SX128X_MAX_POWER;

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template<typename T>
void INTERRUPT_ATTR SX128xInterface<T>::disableInterrupt()
{
    lora.clearDio1Action();
}

template<typename T>
bool SX128xInterface<T>::wideLora()
{
    return true;
}

template<typename T>
void SX128xInterface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby
    
    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX128x standby failed with error %d\n", err);

    assert(err == RADIOLIB_ERR_NONE);

#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn off RX and TX power
    digitalWrite(SX128X_RXEN, LOW);
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    digitalWrite(SX128X_TXEN, LOW);
#endif

    isReceiving = false; // If we were receiving, not any more
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/**
 * Add SNR data to received messages
 */
template<typename T>
void SX128xInterface<T>::addReceiveMetadata(MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
template<typename T>
void SX128xInterface<T>::configHardwareForSend()
{
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on TX power / off RX power
    digitalWrite(SX128X_TXEN, HIGH);
#endif
#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC)
    digitalWrite(SX128X_RXEN, LOW);
#endif

    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template<typename T>
void SX128xInterface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on RX power / off TX power
    digitalWrite(SX128X_RXEN, HIGH);
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    digitalWrite(SX128X_TXEN, LOW);
#endif
  
    int err = lora.startReceive();

    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
#endif
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
template<typename T>
bool SX128xInterface<T>::isChannelActive()
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
bool SX128xInterface<T>::isActivelyReceiving()
{
    uint16_t irq = lora.getIrqStatus();
    bool hasPreamble = (irq & RADIOLIB_SX128X_IRQ_HEADER_VALID);
    return hasPreamble;
}

template<typename T>
bool SX128xInterface<T>::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    // \todo Display actual typename of the adapter, not just `SX128x`
    LOG_DEBUG("SX128x entering sleep mode (FIXME, don't keep config)\n");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    // FIXME - this isn't correct
    // lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig); // Note: we do not keep the config, full reinit will be needed

#ifdef SX128X_POWER_EN
    digitalWrite(SX128X_POWER_EN, LOW);
#endif

    return true;
}
