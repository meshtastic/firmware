#include "configuration.h"

#if (defined(USE_LR2021) || defined(ARCH_PORTDUINO)) && RADIOLIB_EXCLUDE_LR2021 != 1
#include "LR20x0Interface.h"
#include "error.h"
#include "mesh/NodeDB.h"

// Keep LR20x0 naming while RadioLib exposes LR2021 symbols.
#ifndef LR20x0
#define LR20x0 LR2021
#endif

#ifdef LR2021_DIO_AS_RF_SWITCH
#include "rfswitch.h"
#elif ARCH_PORTDUINO
#include "PortduinoGlue.h"
#define lr20x0_rfswitch_dio_pins portduino_config.rfswitch_dio_pins
#define lr20x0_rfswitch_table portduino_config.rfswitch_table
#else
static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    {LR20x0::MODE_STBY, {}},  {LR20x0::MODE_RX, {}},    {LR20x0::MODE_TX, {}},
    {LR20x0::MODE_RX_HF, {}}, {LR20x0::MODE_TX_HF, {}}, END_OF_MODE_TABLE,
};
#endif

// Particular boards might define a different max power based on what their hardware can do, default to max power output if not
// specified (may be dangerous if using external PA and LR20x0 power config forgotten)
#if ARCH_PORTDUINO
#define LR2021_MAX_POWER portduino_config.lr2021_max_power
#endif
#ifndef LR2021_MAX_POWER
#define LR2021_MAX_POWER 22
#endif

// the 2.4G part maxes at 12dBm

#if ARCH_PORTDUINO
#define LR2021_MAX_POWER_HF portduino_config.lr2021_max_power_hf
#endif
#ifndef LR2021_MAX_POWER_HF
#define LR2021_MAX_POWER_HF 12
#endif

template <typename T>
LR20x0Interface<T>::LR20x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_WARN("LR20x0Interface(cs=%d, irq=%d, rst=%d, busy=%d)", cs, irq, rst, busy);
}

template <typename T> bool LR20x0Interface<T>::supportsLoRaBandwidth(float bandwidthKHz, bool wideBand)
{
    (void)wideBand;
    return bandwidthKHz == 31.25f || bandwidthKHz == 41.7f || bandwidthKHz == 62.5f || bandwidthKHz == 83.0f ||
           bandwidthKHz == 101.0f || bandwidthKHz == 125.0f || bandwidthKHz == 203.125f || bandwidthKHz == 250.0f ||
           bandwidthKHz == 406.25f || bandwidthKHz == 500.0f || bandwidthKHz == 812.5f || bandwidthKHz == 1000.0f;
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template <typename T> bool LR20x0Interface<T>::init()
{
#ifdef LR2021_POWER_EN
    pinMode(LR2021_POWER_EN, OUTPUT);
    digitalWrite(LR2021_POWER_EN, HIGH);
#endif

#if ARCH_PORTDUINO
    float tcxoVoltage = (float)portduino_config.dio3_tcxo_voltage / 1000;
// FIXME: correct logic to default to not using TCXO if no voltage is specified for LR20x0_DIO3_TCXO_VOLTAGE
#elif defined(LR2021_DIO3_TCXO_VOLTAGE)
    float tcxoVoltage = LR2021_DIO3_TCXO_VOLTAGE;
    LOG_DEBUG("LR2021_DIO3_TCXO_VOLTAGE defined, using DIO3 as TCXO reference voltage at %f V", LR2021_DIO3_TCXO_VOLTAGE);
    // (DIO3 is not free to be used as an IRQ)
#elif defined(TCXO_OPTIONAL)
    float tcxoVoltage = 1.6f; // TCXO_OPTIONAL: try default 1.6 V first, fall back to XTAL on failure
    LOG_DEBUG("TCXO_OPTIONAL: no LR2021_DIO3_TCXO_VOLTAGE defined, trying default TCXO Vref 1.6 V first");
#else
    float tcxoVoltage =
        0; // "TCXO reference voltage to be set on DIO3. Defaults to 1.6 V, set to 0 to skip." per
           // https://github.com/jgromes/RadioLib/blob/690a050ebb46e6097c5d00c371e961c1caa3b52e/src/modules/LR11x0/LR11x0.h#L471C26-L471C104
    // (DIO3 is free to be used as an IRQ)
    LOG_DEBUG("LR2021_DIO3_TCXO_VOLTAGE not defined, not using DIO3 as TCXO reference voltage");
#endif

    if (!RadioLibInterface::init())
        return false;

#ifdef LR2021_IRQ_DIO_NUM
    lora.irqDioNum = LR2021_IRQ_DIO_NUM;
    LOG_DEBUG("Set irqDioNum %d", lora.irqDioNum);
#elif defined(IRQ_DIO_NUM)
    lora.irqDioNum = IRQ_DIO_NUM;
    LOG_DEBUG("Set irqDioNum %d", lora.irqDioNum);
#else
    LOG_DEBUG("Use default irqDioNum %d", lora.irqDioNum);
#endif

    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR2021_MAX_POWER_HF);
    } else {
        limitPower(LR2021_MAX_POWER); // default clamp for non-wide freq range
    }

#ifdef LR2021_RF_SWITCH_SUBGHZ
    pinMode(LR2021_RF_SWITCH_SUBGHZ, OUTPUT);
#endif

#ifdef LR2021_RF_SWITCH_2_4GHZ
    pinMode(LR2021_RF_SWITCH_2_4GHZ, OUTPUT);
#endif
    selectExternalRfPath(getFreq());
    LOG_DEBUG("Set external RF path to %s", getFreq() < 1000.0f ? "SubGHz" : "2.4GHz");

    // Allow extra time for TCXO to stabilize after power-on
    delay(10);

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);

    // Retry if we get SPI command failed - some units need extra TCXO stabilization time
    if (res == RADIOLIB_ERR_SPI_CMD_FAILED) {
        LOG_WARN("LR20x0 init failed with %d (SPI_CMD_FAILED), retrying after delay...", res);
        delay(100);
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
    }

#if defined(TCXO_OPTIONAL)
    // If init failed for any reason other than chip not found, retry without TCXO (XTAL mode)
    if (res != RADIOLIB_ERR_NONE && res != RADIOLIB_ERR_CHIP_NOT_FOUND && tcxoVoltage > 0) {
        LOG_WARN("LR20x0 init failed with TCXO Vref %f V (err %d), retrying without TCXO", tcxoVoltage, res);
        tcxoVoltage = 0;
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        if (res == RADIOLIB_ERR_NONE)
            LOG_INFO("LR20x0 init success without TCXO (XTAL mode)");
    }
#endif

    // \todo Display actual typename of the adapter, not just `LR20x0`
    LOG_INFO("LR20x0 init result %d", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || res == RADIOLIB_ERR_SPI_CMD_FAILED)
        return false;

    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

#ifdef LR2021_DIO_AS_RF_SWITCH
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = portduino_config.has_rfswitch_table;
#else
    bool dioAsRfSwitch = false;
#endif

    if (dioAsRfSwitch) {
        lora.setRfSwitchTable(lr20x0_rfswitch_dio_pins, lr20x0_rfswitch_table);
        LOG_DEBUG("Set DIO RF switch");
    }

    if (res == RADIOLIB_ERR_NONE) {
        if (config.lora.sx126x_rx_boosted_gain) { // the name is unfortunate but historically accurate
            res = lora.setRxBoostedGainMode(true);
            LOG_INFO("Set RX gain to boosted mode; result: %d", res);
        } else {
            res = lora.setRxBoostedGainMode(false);
            LOG_INFO("Set RX gain to power saving mode (boosted mode off); result: %d", res);
        }
    }

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool LR20x0Interface<T>::reconfigure()
{
    bool success = RadioLibInterface::reconfigure();
    const meshtastic_Config_LoRaConfig &loraConfig = getActiveLoRaConfig();

    const auto recordConfigError = [this, &success](const char *operation, int result) {
        if (result == RADIOLIB_ERR_NONE)
            return;
        LOG_ERROR("LR20x0 %s %s%d", operation, radioLibErr, result);
        if (shouldRecordReconfigureFailure())
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
        success = false;
    };

    int err = setStandby(false);
    recordConfigError("standby", err);

    // configure publicly accessible settings
    err = lora.setSpreadingFactor(sf);
    recordConfigError("setSpreadingFactor", err);

    err = lora.setBandwidth(bw); // different form than LR11xx
    recordConfigError("setBandwidth", err);

    err = lora.setCodingRate(cr, cr != 7); // use long interleaving except if CR is 4/7 which doesn't support it
    recordConfigError("setCodingRate", err);

    err = lora.setSyncWord(syncWord);
    recordConfigError("setSyncWord", err);

    if (loraConfig.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR2021_MAX_POWER_HF);
    } else {
        limitPower(LR2021_MAX_POWER); // default clamp for non-wide freq range
    }

    err = lora.setPreambleLength(preambleLength);
    recordConfigError("setPreambleLength", err);

    err = lora.setFrequency(getFreq());
    recordConfigError("setFrequency", err);

    err = lora.setOutputPower(power);
    recordConfigError("setOutputPower", err);

    // Apply RX gain mode - valid in STDBY, matches resetAGC() pattern
    err = lora.setRxBoostedGainMode(loraConfig.sx126x_rx_boosted_gain);
    recordConfigError("setRxBoostedGainMode", err);

    if (success) {
        err = startReceiveForReconfigure();
        recordConfigError("startReceive", err);
    }

    return success;
}

template <typename T> void LR20x0Interface<T>::disableInterrupt()
{
    lora.clearIrqAction();
}

template <typename T> int LR20x0Interface<T>::setStandby(bool completePacket)
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE) {
        LOG_DEBUG("LR20x0 standby failed with error %d", err);
    }

    if (err != RADIOLIB_ERR_NONE)
        return err;

    isReceiving = false; // If we were receiving, not any more
    activeReceiveStart = 0;
    disableInterrupt();
    if (completePacket)
        completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
    return err;
}

template <typename T> void LR20x0Interface<T>::setStandby()
{
    assert(setStandby(true) == RADIOLIB_ERR_NONE);
}

/**
 * Add SNR data to received messages
 */
template <typename T> void LR20x0Interface<T>::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
    mp->has_rx_rssi = true; // rx_rssi has explicit presence - a genuine reading must be marked present to survive encoding
    // LOG_DEBUG("Corrected frequency offset: %f", lora.getFrequencyError()); // not implemented for LR20x0, but noop for LR11x0
    // too(!)
}

/** We override to turn on transmitter power as needed.
 */
template <typename T> void LR20x0Interface<T>::configHardwareForSend()
{
    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void LR20x0Interface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else
    setStandby();

    const int err = startReceiveForReconfigure();
    if (err != RADIOLIB_ERR_NONE)
        LOG_ERROR("StartReceive error: %d", err);
    assert(err == RADIOLIB_ERR_NONE);
#endif
}

template <typename T> int LR20x0Interface<T>::startReceiveForReconfigure()
{
#ifdef SLEEP_ONLY
    sleep();
    return RADIOLIB_ERR_NONE;
#else
    int err = lora.setPreambleLength(preambleLength); // Solve RX ack fail after direct message sent.
    if (err != RADIOLIB_ERR_NONE)
        return err;

    selectExternalRfPath(getFreq());
    err = lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    if (err != RADIOLIB_ERR_NONE)
        return err;

    RadioLibInterface::startReceive();

    // Must be done AFTER starting receive, because startReceive clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
    return RADIOLIB_ERR_NONE;
#endif
}

template <typename T> void LR20x0Interface<T>::selectExternalRfPath(float frequency)
{
#ifdef LR2021_RF_SWITCH_SUBGHZ
    digitalWrite(LR2021_RF_SWITCH_SUBGHZ, frequency < 1000.0f ? HIGH : LOW);
#endif
#ifdef LR2021_RF_SWITCH_2_4GHZ
    digitalWrite(LR2021_RF_SWITCH_2_4GHZ, frequency < 1000.0f ? LOW : HIGH);
#endif
}

/** Is the channel currently active? */
template <typename T> bool LR20x0Interface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel
    ChannelScanConfig_t cfg = {.cad = {.symNum = NUM_SYM_CAD,
                                       .detPeak = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                       .detMin = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                       .exitMode = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                       .timeout = 0,
                                       .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS,
                                       .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK}};
    int16_t result;

    setStandby();
    result = lora.scanChannel(cfg);
    if (result == RADIOLIB_LORA_DETECTED)
        return true;

    assert(result != RADIOLIB_ERR_WRONG_MODEM);

    return false;
}

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
template <typename T> bool LR20x0Interface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation. Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    return receiveDetected(lora.getIrqStatus(), RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID, RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED);
}

#ifdef LR20X0_AGC_RESET
template <typename T> void LR20x0Interface<T>::resetAGC()
{
    // Safety: don't reset mid-packet
    if (sendingPacket != NULL || (isReceiving && isActivelyReceiving()))
        return;

    LOG_DEBUG("LR20x0 AGC reset: warm sleep + Calibrate(0x3F)");

    // 1. Warm sleep - powers down the analog frontend, resetting AGC state
    lora.sleep(true, 0);

    // 2. Wake to RC standby for stable calibration
    lora.standby(RADIOLIB_LR20X0_STANDBY_RC, true);

    // 3. Calibrate all blocks (PLL, ADC, image, RC oscillators)
    //    calibrate() is protected on LR20x0, so use raw SPI (same as internal implementation)
    uint8_t calData = RADIOLIB_LR20X0_CALIBRATE_ALL;
    module.SPIwriteStream(RADIOLIB_LR20X0_CMD_CALIBRATE, &calData, 1, true, true);

    // 4. Re-calibrate image rejection for actual operating frequency
    //    Calibrate(0x3F) defaults to 902-928 MHz which is wrong for other regions.
    lora.calibrateImageRejection(getFreq() - 4.0f, getFreq() + 4.0f);

    // 5. Re-apply RX boosted gain mode
    lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);

    // 6. Resume receiving
    startReceive();
}
#endif

template <typename T> bool LR20x0Interface<T>::sleep()
{
    // \todo Display actual typename of the adapter, not just `LR20x0`
    LOG_DEBUG("LR20x0 entering sleep mode");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = false;
    lora.sleep(keepConfig, 0); // Note: we do not keep the config, full reinit will be needed

#ifdef LR2021_POWER_EN
    digitalWrite(LR2021_POWER_EN, LOW);
#endif

    return true;
}

template <typename T> int16_t LR20x0Interface<T>::getCurrentRSSI()
{
    float rssi = lora.getRSSI(false, true);
    return (int16_t)round(rssi);
}

// Don't leak the aliases into the files InterfacesTemplates.cpp includes after this one.
#undef lr20x0_rfswitch_dio_pins
#undef lr20x0_rfswitch_table
#undef LR20x0
#endif
