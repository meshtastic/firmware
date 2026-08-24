#if RADIOLIB_EXCLUDE_SX128X != 1
#include "SX128xInterface.h"
#include "Throttle.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

// Peak-to-noise decision threshold for the CAD, the only CAD sensitivity control on this part.
// Reset value 0x32, well above every threshold AN1200.77 recommends.
#define SX1280_REG_CAD_DET_PEAK 0x0942

// Particular boards might define a different max power based on what their hardware can do
#if ARCH_PORTDUINO
#define SX128X_MAX_POWER portduino_config.sx128x_max_power
#endif
#ifndef SX128X_MAX_POWER
#define SX128X_MAX_POWER 13
#endif

template <typename T>
SX128xInterface<T>::SX128xInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_DEBUG("SX128xInterface(cs=%d, irq=%d, rst=%d, busy=%d)", cs, irq, rst, busy);
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template <typename T> bool SX128xInterface<T>::init()
{
#ifdef SX128X_POWER_EN
    pinMode(SX128X_POWER_EN, OUTPUT);
    digitalWrite(SX128X_POWER_EN, HIGH);
#endif

#ifdef RF95_FAN_EN
    pinMode(RF95_FAN_EN, OUTPUT);
    digitalWrite(RF95_FAN_EN, 1);
#endif

#if ARCH_PORTDUINO
    if (portduino_config.lora_rxen_pin.pin != RADIOLIB_NC) {
        pinMode(portduino_config.lora_rxen_pin.pin, OUTPUT);
        digitalWrite(portduino_config.lora_rxen_pin.pin, LOW); // Set low before becoming an output
    }
    if (portduino_config.lora_txen_pin.pin != RADIOLIB_NC) {
        pinMode(portduino_config.lora_txen_pin.pin, OUTPUT);
        digitalWrite(portduino_config.lora_txen_pin.pin, LOW); // Set low before becoming an output
    }
#else
#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // set not rx or tx mode
    pinMode(SX128X_RXEN, OUTPUT);
    digitalWrite(SX128X_RXEN, LOW); // Set low before becoming an output
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    pinMode(SX128X_TXEN, OUTPUT);
    digitalWrite(SX128X_TXEN, LOW);
#endif
#endif

    RadioLibInterface::init();

    limitPower(SX128X_MAX_POWER);

    preambleLength = 12; // 12 is the default for this chip, 32 does not RX at all

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength);
    // \todo Display actual typename of the adapter, not just `SX128x`
    LOG_INFO("SX128x init result %d", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || res == RADIOLIB_ERR_SPI_CMD_FAILED)
        return false;

    if ((config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && (res == RADIOLIB_ERR_INVALID_FREQUENCY)) {
        LOG_WARN("Radio only supports 2.4GHz LoRa. Adjusting Region and rebooting");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        delay(2000);
#if defined(ARCH_ESP32)
        ESP.restart();
#elif defined(ARCH_NRF52)
        NVIC_SystemReset();
#else
        LOG_ERROR("FIXME implement reboot for this platform. Skip for now");
#endif
    }

    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);

#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC) && defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC)
    if (res == RADIOLIB_ERR_NONE) {
        lora.setRfSwitchPins(SX128X_RXEN, SX128X_TXEN);
    }
#elif ARCH_PORTDUINO
    if (res == RADIOLIB_ERR_NONE && portduino_config.lora_rxen_pin.pin != RADIOLIB_NC &&
        portduino_config.lora_txen_pin.pin != RADIOLIB_NC) {
        lora.setRfSwitchPins(portduino_config.lora_rxen_pin.pin, portduino_config.lora_txen_pin.pin);
    }
#endif

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    if (res == RADIOLIB_ERR_NONE) {
        applyCadDetPeak(); // init() does not route through reconfigure(), so set it here too
        startReceive();    // start receiving
    }

    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool SX128xInterface<T>::reconfigure()
{
    RadioLibInterface::reconfigure();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora.setSpreadingFactor(sf);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setBandwidth(bw);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setCodingRate(cr, cr != 7); // use long interleaving except if CR is 4/7 which doesn't support it
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setSyncWord(syncWord);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX128X setSyncWord %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX128X setPreambleLength %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    limitPower(SX128X_MAX_POWER);

    err = lora.setOutputPower(power);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX128X setOutputPower %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    applyCadDetPeak(); // depends on the sf/bw just set

    startReceive(); // restart receiving

    return true;
}

template <typename T> void SX128xInterface<T>::clearRadioIsr()
{
    lora.clearDio1Action();
}

/** Set the CAD peak-to-noise threshold for the current sf/bw. */
template <typename T> void SX128xInterface<T>::applyCadDetPeak()
{
    // AN1200.77 Table 1 calibrated PNR thresholds, rows BW 200/400/800/1600 kHz, columns SF5..SF12.
    // The note measures these for a 4-symbol window but states they apply to any window duration.
    // A new bandwidth or SF outside these needs a row adding; unmatched leaves the chip's own default.
    static constexpr uint8_t CAD_DET_PEAK[4][8] = {
        {22, 22, 23, 25, 25, 28, 32, 36}, // 203.125 kHz
        {20, 21, 22, 23, 25, 25, 29, 32}, // 406.25 kHz
        {20, 21, 21, 23, 25, 26, 27, 29}, // 812.5 kHz
        {19, 21, 21, 23, 24, 24, 27, 28}, // 1625 kHz
    };
    static constexpr float TABLE_BW_KHZ[4] = {203.125f, 406.25f, 812.5f, 1625.0f};
    int bwIdx = -1;
    for (int i = 0; i < 4; i++) {
        if (bw > TABLE_BW_KHZ[i] - 1.0f && bw < TABLE_BW_KHZ[i] + 1.0f)
            bwIdx = i;
    }
    // Written through our own Module rather than lora.writeRegister(), which RadioLib keeps protected
    // unless RADIOLIB_LOW_LEVEL unlocks raw register access across every driver. Same SPI transaction:
    // SX128x::writeRegister() is a straight SPIwriteRegisterBurst, and begin() has already pointed
    // spiConfig at the 0x18 WriteRegister opcode by the time reconfigure() runs.
    if (bwIdx >= 0 && sf >= 5 && sf <= 12) {
        const uint8_t detPeak = CAD_DET_PEAK[bwIdx][sf - 5];
        module.SPIwriteRegisterBurst(SX1280_REG_CAD_DET_PEAK, &detPeak, 1);
    }
}

template <typename T> bool SX128xInterface<T>::wideLora()
{
    return true;
}

template <typename T> void SX128xInterface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX128x standby %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);
#if ARCH_PORTDUINO
    if (portduino_config.lora_rxen_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_rxen_pin.pin, LOW);
    }
    if (portduino_config.lora_txen_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_txen_pin.pin, LOW);
    }
#else
#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn off RX and TX power
    digitalWrite(SX128X_RXEN, LOW);
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    digitalWrite(SX128X_TXEN, LOW);
#endif
#endif
    isReceiving = false; // If we were receiving, not any more
    activeReceiveStart = 0;
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
}

/**
 * Add SNR data to received messages
 */
template <typename T> void SX128xInterface<T>::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
    mp->has_rx_rssi = true; // rx_rssi has explicit presence - a genuine reading must be marked present to survive encoding
    LOG_DEBUG("Corrected frequency offset: %f", lora.getFrequencyError());
}

/** We override to turn on transmitter power as needed.
 */
template <typename T> void SX128xInterface<T>::configHardwareForSend()
{
#if ARCH_PORTDUINO
    if (portduino_config.lora_txen_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_txen_pin.pin, HIGH);
    }
    if (portduino_config.lora_rxen_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_rxen_pin.pin, LOW);
    }

#else
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on TX power / off RX power
    digitalWrite(SX128X_TXEN, HIGH);
#endif
#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC)
    digitalWrite(SX128X_RXEN, LOW);
#endif
#endif

    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void SX128xInterface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

#if ARCH_PORTDUINO
    if (portduino_config.lora_rxen_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_rxen_pin.pin, HIGH);
    }
    if (portduino_config.lora_txen_pin.pin != RADIOLIB_NC) {
        digitalWrite(portduino_config.lora_txen_pin.pin, LOW);
    }

#else
#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on RX power / off TX power
    digitalWrite(SX128X_RXEN, HIGH);
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    digitalWrite(SX128X_TXEN, LOW);
#endif
#endif

    int err = lora.startReceive(RADIOLIB_SX128X_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS);

    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("SX128X startReceive %s%d", radioLibErr, err);
    assert(err == RADIOLIB_ERR_NONE);

    RadioLibInterface::startReceive();

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
#endif
}

/** Is the channel currently active? */
template <typename T> bool SX128xInterface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel.
    // symNum is the encoded SET_CAD_PARAMS value (symbol count in bits 7:5), not a raw count - pass the
    // CAD_ON_n_SYMB constant matching what getCadSymbolCount() reports, or the slot stops matching.
    // The PNR threshold is written once per config by applyCadDetPeak(), not here: nothing between the
    // scan and the transmit that follows it should cost an extra SPI round trip.

    // detPeak/detMin/exitMode below are ignored: SetCadParams (0x88) carries only cadSymbolNum, and
    // RadioLib's setCad() writes just that one byte. CAD always exits to STDBY_RC, so a busy channel
    // needs the caller's full re-arm; there is no in-chip CAD->RX handoff on this part.
    ChannelScanConfig_t cfg = {.cad = {.symNum = RADIOLIB_SX128X_CAD_ON_8_SYMB,
                                       .detPeak = 0,
                                       .detMin = 0,
                                       .exitMode = 0,
                                       .timeout = 0,
                                       .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS,
                                       .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK}};
    int16_t result;

    setStandby();
    result = lora.scanChannel(cfg);
    if (result == RADIOLIB_LORA_DETECTED)
        return true;
    if (result != RADIOLIB_CHANNEL_FREE)
        LOG_ERROR("SX128X scanChannel %s%d", radioLibErr, result);
    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    return false;
}

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
template <typename T> bool SX128xInterface<T>::isActivelyReceiving()
{
    return receiveDetected(lora.getIrqStatus(), RADIOLIB_SX128X_IRQ_HEADER_VALID, RADIOLIB_SX128X_IRQ_PREAMBLE_DETECTED);
}

template <typename T> bool SX128xInterface<T>::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    // \todo Display actual typename of the adapter, not just `SX128x`
    LOG_DEBUG("SX128x entering sleep mode"); // (FIXME, don't keep config)
    setStandby();                            // Stop any pending operations

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

template <typename T> int16_t SX128xInterface<T>::getCurrentRSSI()
{
    float rssi = lora.getRSSI(false);
    return (int16_t)round(rssi);
}
#endif