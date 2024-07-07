#include "SX128xInterface.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

// Particular boards might define a different max power based on what their hardware can do
#ifndef SX128X_MAX_POWER
#define SX128X_MAX_POWER 13
#endif

template <typename T>
SX128xInterface<T>::SX128xInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_DEBUG("SX128xInterface(cs=%d, irq=%d, rst=%d, busy=%d)\n", cs, irq, rst, busy);
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
    if (settingsMap[rxen] != RADIOLIB_NC) {
        pinMode(settingsMap[rxen], OUTPUT);
        digitalWrite(settingsMap[rxen], LOW); // Set low before becoming an output
    }
    if (settingsMap[txen] != RADIOLIB_NC) {
        pinMode(settingsMap[txen], OUTPUT);
        digitalWrite(settingsMap[txen], LOW); // Set low before becoming an output
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

    if (power > SX128X_MAX_POWER) // This chip has lower power limits than some
        power = SX128X_MAX_POWER;

    limitPower();

    preambleLength = 12; // 12 is the default for this chip, 32 does not RX at all

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength);
    // \todo Display actual typename of the adapter, not just `SX128x`
    LOG_INFO("SX128x init result %d\n", res);

    if ((config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && (res == RADIOLIB_ERR_INVALID_FREQUENCY)) {
        LOG_WARN("Radio chip only supports 2.4GHz LoRa. Adjusting Region and rebooting.\n");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
        nodeDB->saveToDisk(SEGMENT_CONFIG);
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

#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC) && defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC)
    if (res == RADIOLIB_ERR_NONE) {
        lora.setRfSwitchPins(SX128X_RXEN, SX128X_TXEN);
    }
#elif ARCH_PORTDUINO
    if (res == RADIOLIB_ERR_NONE && settingsMap[rxen] != RADIOLIB_NC && settingsMap[txen] != RADIOLIB_NC) {
        lora.setRfSwitchPins(settingsMap[rxen], settingsMap[txen]);
    }
#endif

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

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

    err = lora.setCodingRate(cr);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setSyncWord(syncWord);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("Radiolib error %d when attempting SX128X setSyncWord!\n", err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("Radiolib error %d when attempting SX128X setPreambleLength!\n", err);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > SX128X_MAX_POWER) // This chip has lower power limits than some
        power = SX128X_MAX_POWER;

    err = lora.setOutputPower(power);
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("Radiolib error %d when attempting SX128X setOutputPower!\n", err);
    assert(err == RADIOLIB_ERR_NONE);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template <typename T> void INTERRUPT_ATTR SX128xInterface<T>::disableInterrupt()
{
    lora.clearDio1Action();
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
        LOG_ERROR("SX128x standby failed with error %d\n", err);
    assert(err == RADIOLIB_ERR_NONE);
#if ARCH_PORTDUINO
    if (settingsMap[rxen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[rxen], LOW);
    }
    if (settingsMap[txen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[txen], LOW);
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
    // LOG_DEBUG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
template <typename T> void SX128xInterface<T>::configHardwareForSend()
{
#if ARCH_PORTDUINO
    if (settingsMap[txen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[txen], HIGH);
    }
    if (settingsMap[rxen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[rxen], LOW);
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
    if (settingsMap[rxen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[rxen], HIGH);
    }
    if (settingsMap[txen] != RADIOLIB_NC) {
        digitalWrite(settingsMap[txen], LOW);
    }

#else
#if defined(SX128X_RXEN) && (SX128X_RXEN != RADIOLIB_NC) // we have RXEN/TXEN control - turn on RX power / off TX power
    digitalWrite(SX128X_RXEN, HIGH);
#endif
#if defined(SX128X_TXEN) && (SX128X_TXEN != RADIOLIB_NC)
    digitalWrite(SX128X_TXEN, LOW);
#endif
#endif

    // We use the PREAMBLE_DETECTED and HEADER_VALID IRQ flag to detect whether we are actively receiving
    int err =
        lora.startReceive(RADIOLIB_SX128X_RX_TIMEOUT_INF, RADIOLIB_SX128X_IRQ_RX_DEFAULT | RADIOLIB_SX128X_IRQ_PREAMBLE_DETECTED |
                                                              RADIOLIB_SX128X_IRQ_HEADER_VALID);

    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("Radiolib error %d when attempting SX128X startReceive!\n", err);
    assert(err == RADIOLIB_ERR_NONE);

    RadioLibInterface::startReceive();

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
#endif
}

/** Is the channel currently active? */
template <typename T> bool SX128xInterface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel
    int16_t result;

    setStandby();
    result = lora.scanChannel();
    if (result == RADIOLIB_LORA_DETECTED)
        return true;
    if (result != RADIOLIB_CHANNEL_FREE)
        LOG_ERROR("Radiolib error %d when attempting SX128X scanChannel!\n", result);
    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    return false;
}

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
template <typename T> bool SX128xInterface<T>::isActivelyReceiving()
{
    uint16_t irq = lora.getIrqStatus();
    bool detected = (irq & (RADIOLIB_SX128X_IRQ_HEADER_VALID | RADIOLIB_SX128X_IRQ_PREAMBLE_DETECTED));

    // Handle false detections
    if (detected) {
        uint32_t now = millis();
        if (!activeReceiveStart) {
            activeReceiveStart = now;
        } else if ((now - activeReceiveStart > 2 * preambleTimeMsec) && !(irq & RADIOLIB_SX128X_IRQ_HEADER_VALID)) {
            // The HEADER_VALID flag should be set by now if it was really a packet, so ignore PREAMBLE_DETECTED flag
            activeReceiveStart = 0;
            LOG_DEBUG("Ignore false preamble detection.\n");
            return false;
        } else if (now - activeReceiveStart > maxPacketTimeMsec) {
            // We should have gotten an RX_DONE IRQ by now if it was really a packet, so ignore HEADER_VALID flag
            activeReceiveStart = 0;
            LOG_DEBUG("Ignore false header detection.\n");
            return false;
        }
    }

    return detected;
}

template <typename T> bool SX128xInterface<T>::sleep()
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