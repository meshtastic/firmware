#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "LR11x0Interface.h"
#include "Throttle.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"
#ifdef LR11X0_DIO_AS_RF_SWITCH
#include "rfswitch.h"
#else
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
    {LR11x0::MODE_STBY, {}},  {LR11x0::MODE_RX, {}},   {LR11x0::MODE_TX, {}},   {LR11x0::MODE_TX_HP, {}},
    {LR11x0::MODE_TX_HF, {}}, {LR11x0::MODE_GNSS, {}}, {LR11x0::MODE_WIFI, {}}, END_OF_MODE_TABLE,
};
#endif

#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

// Particular boards might define a different max power based on what their hardware can do, default to max power output if not
// specified (may be dangerous if using external PA and LR11x0 power config forgotten)
#ifndef LR1110_MAX_POWER
#define LR1110_MAX_POWER 22
#endif

// the 2.4G part maxes at 13dBm

#ifndef LR1120_MAX_POWER
#define LR1120_MAX_POWER 13
#endif

template <typename T>
LR11x0Interface<T>::LR11x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_WARN("LR11x0Interface(cs=%d, irq=%d, rst=%d, busy=%d)\n", cs, irq, rst, busy);
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template <typename T> bool LR11x0Interface<T>::init()
{
#ifdef LR11X0_POWER_EN
    pinMode(LR11X0_POWER_EN, OUTPUT);
    digitalWrite(LR11X0_POWER_EN, HIGH);
#endif

// FIXME: correct logic to default to not using TCXO if no voltage is specified for LR11x0_DIO3_TCXO_VOLTAGE
#if !defined(LR11X0_DIO3_TCXO_VOLTAGE)
    float tcxoVoltage =
        0; // "TCXO reference voltage to be set on DIO3. Defaults to 1.6 V, set to 0 to skip." per
           // https://github.com/jgromes/RadioLib/blob/690a050ebb46e6097c5d00c371e961c1caa3b52e/src/modules/LR11x0/LR11x0.h#L471C26-L471C104
    // (DIO3 is free to be used as an IRQ)
    LOG_DEBUG("LR11X0_DIO3_TCXO_VOLTAGE not defined, not using DIO3 as TCXO reference voltage\n");
#else
    float tcxoVoltage = LR11X0_DIO3_TCXO_VOLTAGE;
    LOG_DEBUG("LR11X0_DIO3_TCXO_VOLTAGE defined, using DIO3 as TCXO reference voltage at %f V\n", LR11X0_DIO3_TCXO_VOLTAGE);
    // (DIO3 is not free to be used as an IRQ)
#endif

    RadioLibInterface::init();

    if (power > LR1110_MAX_POWER) // Clamp power to maximum defined level
        power = LR1110_MAX_POWER;

    if ((power > LR1120_MAX_POWER) &&
        (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) // clamp again if wide freq range
        power = LR1120_MAX_POWER;

    limitPower();

#ifdef LR11X0_RF_SWITCH_SUBGHZ
    pinMode(LR11X0_RF_SWITCH_SUBGHZ, OUTPUT);
    digitalWrite(LR11X0_RF_SWITCH_SUBGHZ, getFreq() < 1e9 ? HIGH : LOW);
    LOG_DEBUG("Setting RF0 switch to %s\n", getFreq() < 1e9 ? "SubGHz" : "2.4GHz");
#endif

#ifdef LR11X0_RF_SWITCH_2_4GHZ
    pinMode(LR11X0_RF_SWITCH_2_4GHZ, OUTPUT);
    digitalWrite(LR11X0_RF_SWITCH_2_4GHZ, getFreq() < 1e9 ? LOW : HIGH);
    LOG_DEBUG("Setting RF1 switch to %s\n", getFreq() < 1e9 ? "SubGHz" : "2.4GHz");
#endif

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_INFO("LR11x0 init result %d\n", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND)
        return false;

    LR11x0VersionInfo_t version;
    res = lora.getVersionInfo(&version);
    if (res == RADIOLIB_ERR_NONE)
        LOG_DEBUG("LR11x0 Device %d, HW %d, FW %d.%d, WiFi %d.%d, GNSS %d.%d\n", version.device, version.hardware,
                  version.fwMajor, version.fwMinor, version.fwMajorWiFi, version.fwMinorWiFi, version.fwGNSS,
                  version.almanacGNSS);

    LOG_INFO("Frequency set to %f\n", getFreq());
    LOG_INFO("Bandwidth set to %f\n", bw);
    LOG_INFO("Power output set to %d\n", power);

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    // FIXME: May want to set depending on a definition, currently all LR1110 variant files use the DC-DC regulator option
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setRegulatorDCDC();

#ifdef LR11X0_DIO_AS_RF_SWITCH
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = false;
    if (settingsMap[dio2_as_rf_switch]) {
        dioAsRfSwitch = true;
    }
#else
    bool dioAsRfSwitch = false;
#endif

    if (dioAsRfSwitch) {
        lora.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);
        LOG_DEBUG("Setting DIO RF switch\n", res);
    }

    if (res == RADIOLIB_ERR_NONE) {
        if (config.lora.sx126x_rx_boosted_gain) { // the name is unfortunate but historically accurate
            res = lora.setRxBoostedGainMode(true);
            LOG_INFO("Set RX gain to boosted mode; result: %d\n", res);
        } else {
            res = lora.setRxBoostedGainMode(false);
            LOG_INFO("Set RX gain to power saving mode (boosted mode off); result: %d\n", res);
        }
    }

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool LR11x0Interface<T>::reconfigure()
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
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > LR1110_MAX_POWER) // This chip has lower power limits than some
        power = LR1110_MAX_POWER;
    if ((power > LR1120_MAX_POWER) && (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) // 2.4G power limit
        power = LR1120_MAX_POWER;

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template <typename T> void INTERRUPT_ATTR LR11x0Interface<T>::disableInterrupt()
{
    lora.clearIrqAction();
}

template <typename T> void LR11x0Interface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE) {
        LOG_DEBUG("LR11x0 standby failed with error %d\n", err);
    }

    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    activeReceiveStart = 0;
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
}

/**
 * Add SNR data to received messages
 */
template <typename T> void LR11x0Interface<T>::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
template <typename T> void LR11x0Interface<T>::configHardwareForSend()
{
    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void LR11x0Interface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

    lora.setPreambleLength(preambleLength); // Solve RX ack fail after direct message sent.  Not sure why this is needed.

    // We use a 16 bit preamble so this should save some power by letting radio sit in standby mostly.
    // Furthermore, we need the PREAMBLE_DETECTED and HEADER_VALID IRQ flag to detect whether we are actively receiving
    int err = lora.startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    assert(err == RADIOLIB_ERR_NONE);

    RadioLibInterface::startReceive();

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
#endif
}

/** Is the channel currently active? */
template <typename T> bool LR11x0Interface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel
    int16_t result;

    setStandby();
    result = lora.scanChannel();
    if (result == RADIOLIB_LORA_DETECTED)
        return true;

    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    return false;
}

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
template <typename T> bool LR11x0Interface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation. Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    return receiveDetected(lora.getIrqStatus(), RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID,
                           RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
}

template <typename T> bool LR11x0Interface<T>::sleep()
{
    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_DEBUG("LR11x0 entering sleep mode\n");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = false;
    lora.sleep(keepConfig, 0); // Note: we do not keep the config, full reinit will be needed

#ifdef LR11X0_POWER_EN
    digitalWrite(LR11X0_POWER_EN, LOW);
#endif

    return true;
}
#endif