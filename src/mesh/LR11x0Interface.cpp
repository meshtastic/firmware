#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "LR11x0Interface.h"
#include "Throttle.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"

// A variant may define LR11X0_UPDATE_FIRMWARE_TO to a Semtech transceiver firmware version (e.g. 0x0402) to
// bake that image in and update the radio on first boot. Every supported image is 61320 words, so this costs
// ~240 kB of flash regardless of the version chosen - only enable it on a variant with the headroom, and
// only for as long as it takes to update the affected units.
#ifdef LR11X0_UPDATE_FIRMWARE_TO
#if LR11X0_UPDATE_FIRMWARE_TO == 0x0402
#define RADIOLIB_LR1110_FIRMWARE_0402
#elif LR11X0_UPDATE_FIRMWARE_TO == 0x0401
#define RADIOLIB_LR1110_FIRMWARE_0401
#elif LR11X0_UPDATE_FIRMWARE_TO == 0x0307
#define RADIOLIB_LR1110_FIRMWARE_0307
#else
// Note: RadioLib ships lr1110_transceiver_0308.h but has no selector for it in LR11x0_firmware.h.
#error "LR11X0_UPDATE_FIRMWARE_TO must be one of 0x0307, 0x0401, 0x0402"
#endif
#include <modules/LR11x0/LR11x0_firmware.h>
#endif

#ifdef LR11X0_DIO_AS_RF_SWITCH
#include "rfswitch.h"
#elif ARCH_PORTDUINO
#include "PortduinoGlue.h"
#define rfswitch_dio_pins portduino_config.rfswitch_dio_pins
#define rfswitch_table portduino_config.rfswitch_table
#else
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
    {LR11x0::MODE_STBY, {}},  {LR11x0::MODE_RX, {}},   {LR11x0::MODE_TX, {}},   {LR11x0::MODE_TX_HP, {}},
    {LR11x0::MODE_TX_HF, {}}, {LR11x0::MODE_GNSS, {}}, {LR11x0::MODE_WIFI, {}}, END_OF_MODE_TABLE,
};
#endif

// Particular boards might define a different max power based on what their hardware can do, default to max power output if not
// specified (may be dangerous if using external PA and LR11x0 power config forgotten)
#if ARCH_PORTDUINO
#define LR1110_MAX_POWER portduino_config.lr1110_max_power
#endif
#ifndef LR1110_MAX_POWER
#define LR1110_MAX_POWER 22
#endif

// the 2.4G part maxes at 13dBm
#if ARCH_PORTDUINO
#define LR1120_MAX_POWER portduino_config.lr1120_max_power
#endif
#ifndef LR1120_MAX_POWER
#define LR1120_MAX_POWER 13
#endif

// Vref to assume for a board that declares a TCXO may be fitted without saying at what voltage.
// "TCXO reference voltage to be set on DIO3. Defaults to 1.6 V, set to 0 to skip." per
// https://github.com/jgromes/RadioLib/blob/690a050ebb46e6097c5d00c371e961c1caa3b52e/src/modules/LR11x0/LR11x0.h#L471C26-L471C104
#if defined(TCXO_OPTIONAL)
#define LR11X0_TCXO_DEFAULT_VOLTAGE 1.6f
#else
#define LR11X0_TCXO_DEFAULT_VOLTAGE 0
#endif

// A chip that never answers can surface either way depending on where RadioLib gave up: a bounded
// per-command BUSY wait in Module::SPItransferStream() reports SPI_CMD_TIMEOUT rather than
// SPI_CMD_FAILED, so both have to count as "the chip did not talk to us"
static inline bool lr11x0SpiFailed(int res)
{
    return res == RADIOLIB_ERR_SPI_CMD_FAILED || res == RADIOLIB_ERR_SPI_CMD_TIMEOUT;
}

template <typename T>
LR11x0Interface<T>::LR11x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_WARN("LR11x0Interface(cs=%d, irq=%d, rst=%d, busy=%d)", cs, irq, rst, busy);
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

    // An explicit Vref always wins; TCXO_OPTIONAL only supplies a default for boards that declare a
    // TCXO may be fitted without saying at what voltage. Both may appear in the same variant file.
#if ARCH_PORTDUINO
    // Portduino leaves dio3_tcxo_voltage at 0 whenever the YAML omits DIO3_TCXO_VOLTAGE, which is the
    // "no explicit Vref" case, so the TCXO_OPTIONAL default still has to apply there
    float tcxoVoltage =
        portduino_config.dio3_tcxo_voltage > 0 ? (float)portduino_config.dio3_tcxo_voltage / 1000 : LR11X0_TCXO_DEFAULT_VOLTAGE;
#elif defined(LR11X0_DIO3_TCXO_VOLTAGE)
    float tcxoVoltage = LR11X0_DIO3_TCXO_VOLTAGE;
#else
    float tcxoVoltage = LR11X0_TCXO_DEFAULT_VOLTAGE;
#endif

    // DIO3 is free to be used as an IRQ only while no TCXO Vref is driven on it
    if (tcxoVoltage > 0)
        LOG_DEBUG("LR11x0 TCXO Vref %f V on DIO3 (DIO3 unavailable as an IRQ)", tcxoVoltage);
    else
        LOG_DEBUG("LR11x0 no TCXO Vref, XTAL only (DIO3 free as an IRQ)");
#if defined(TCXO_OPTIONAL)
    LOG_DEBUG("TCXO_OPTIONAL: oscillator type unknown, probing XTAL first and using any TCXO Vref only as fallback");
#endif

    RadioLibInterface::init();

    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR1120_MAX_POWER);
    } else {
        limitPower(LR1110_MAX_POWER); // default clamp for non-wide freq range
    }

#ifdef LR11X0_RF_SWITCH_SUBGHZ
    pinMode(LR11X0_RF_SWITCH_SUBGHZ, OUTPUT);
    digitalWrite(LR11X0_RF_SWITCH_SUBGHZ, getFreq() < 1e9 ? HIGH : LOW);
    LOG_DEBUG("Set RF0 switch to %s", getFreq() < 1e9 ? "SubGHz" : "2.4GHz");
#endif

#ifdef LR11X0_RF_SWITCH_2_4GHZ
    pinMode(LR11X0_RF_SWITCH_2_4GHZ, OUTPUT);
    digitalWrite(LR11X0_RF_SWITCH_2_4GHZ, getFreq() < 1e9 ? LOW : HIGH);
    LOG_DEBUG("Set RF1 switch to %s", getFreq() < 1e9 ? "SubGHz" : "2.4GHz");
#endif

    // Allow extra time for TCXO to stabilize after power-on
    delay(10);

    // Timestamped brackets so a hang inside RadioLib leaves a dangling "attempt" line in the boot log
    auto tryBegin = [&](int attempt, float vref) {
        uint32_t attemptStart = millis();
        LOG_INFO("LR11x0 begin() attempt %d: tcxoVoltage=%.3fV at t=%ums", attempt, vref, attemptStart);
        int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, vref);
        LOG_INFO("LR11x0 begin() attempt %d returned %d after %ums", attempt, res, millis() - attemptStart);
        return res;
    };

#if defined(TCXO_OPTIONAL)
    // 1. XTAL, because a TCXO-first attempt hangs RadioLib's unbounded calibration wait on a module
    //    with no TCXO fitted, whereas XTAL fails fast and cleanly on a module that does have one
    float attemptVoltage = 0;
#else
    // 1. Whatever Vref the variant configured, which it declared unconditionally
    float attemptVoltage = tcxoVoltage;
#endif
    int res = tryBegin(1, attemptVoltage);

#if defined(TCXO_OPTIONAL)
    // 2. XTAL failed with the chip present, so fall back to the TCXO if the variant configured one
    if (res != RADIOLIB_ERR_NONE && res != RADIOLIB_ERR_CHIP_NOT_FOUND && tcxoVoltage > 0) {
        LOG_WARN("LR11x0 XTAL init failed (err %d), retrying with TCXO Vref %f V", res, tcxoVoltage);
        attemptVoltage = tcxoVoltage;
        res = tryBegin(2, attemptVoltage);
        if (res == RADIOLIB_ERR_NONE)
            LOG_INFO("LR11x0 init success with TCXO Vref %f V", tcxoVoltage);
    }
#endif

    // 3. Some units need extra settling time, so give whichever oscillator we settled on one retry.
    //    After a step 2 fallback that is a second TCXO attempt, which is where settling actually matters.
    if (lr11x0SpiFailed(res)) {
        LOG_WARN("LR11x0 init failed with %d (SPI command failure), retrying after delay...", res);
        delay(100);
        res = tryBegin(3, attemptVoltage);
    }

    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_INFO("LR11x0 init result %d", res);

    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || lr11x0SpiFailed(res)) {
#ifdef LR11X0_UPDATE_FIRMWARE_TO
        // An interrupted update leaves the radio sitting in bootloader mode, where begin() fails. Retry the
        // flash from here rather than giving up, otherwise the device could never recover on its own.
        LOG_WARN("LR11x0 did not start; attempting firmware recovery in case an update was interrupted");
        if (lora.updateFirmware(lr11xx_firmware_image, LR11XX_FIRMWARE_IMAGE_SIZE, true) == RADIOLIB_ERR_NONE) {
            LOG_INFO("LR1110 firmware recovery succeeded, re-initializing radio");
            res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        }
#endif
        if (res != RADIOLIB_ERR_NONE)
            return false;
    }

    LR11x0VersionInfo_t version;
    res = lora.getVersionInfo(&version);
    if (res == RADIOLIB_ERR_NONE) {
        LOG_DEBUG("LR11x0 Device %d, HW %d, FW %d.%d, WiFi %d.%d, GNSS %d.%d", version.device, version.hardware, version.fwMajor,
                  version.fwMinor, version.fwMajorWiFi, version.fwMinorWiFi, version.fwGNSS, version.almanacGNSS);
        transceiverFw = ((uint16_t)version.fwMajor << 8) | version.fwMinor;
        transceiverDevice = version.device;
    }

#ifdef LR11X0_UPDATE_FIRMWARE_TO
    // One-shot transceiver firmware update, opt-in per variant. Only runs when the part is an LR1110 running
    // older firmware than the baked-in image, so once it has succeeded it is a no-op on subsequent boots.
    if (transceiverDevice == RADIOLIB_LR11X0_DEVICE_LR1110 && transceiverFw != 0 && transceiverFw < LR11X0_UPDATE_FIRMWARE_TO) {
        LOG_WARN("LR1110 transceiver FW %d.%d is older than %d.%d - updating now. DO NOT POWER OFF: this "
                 "erases and rewrites the radio's own flash.",
                 transceiverFw >> 8, transceiverFw & 0xFF, LR11X0_UPDATE_FIRMWARE_TO >> 8, LR11X0_UPDATE_FIRMWARE_TO & 0xFF);

        int upd = lora.updateFirmware(lr11xx_firmware_image, LR11XX_FIRMWARE_IMAGE_SIZE, true);
        if (upd != RADIOLIB_ERR_NONE) {
            // The radio is likely sitting in bootloader mode. It is not bricked - the update is retried on
            // the next boot because the version check above will still see old (or unreadable) firmware.
            LOG_ERROR("LR1110 firmware update FAILED %s%d - power-cycle to retry", radioLibErr, upd);
            return false;
        }

        LOG_INFO("LR1110 firmware update complete, re-initializing radio");
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR11x0 re-init after firmware update failed %s%d", radioLibErr, res);
            return false;
        }

        if (lora.getVersionInfo(&version) == RADIOLIB_ERR_NONE) {
            transceiverFw = ((uint16_t)version.fwMajor << 8) | version.fwMinor;
            transceiverDevice = version.device;
            LOG_INFO("LR1110 now running transceiver FW %d.%d", version.fwMajor, version.fwMinor);
        }
    }
#endif

    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    // FIXME: May want to set depending on a definition, currently all LR1110 variant files use the DC-DC regulator option
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setRegulatorDCDC();

#ifdef LR11X0_DIO_AS_RF_SWITCH
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = portduino_config.has_rfswitch_table;
#else
    bool dioAsRfSwitch = false;
#endif

    if (dioAsRfSwitch) {
        lora.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);
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

template <typename T> bool LR11x0Interface<T>::reconfigure()
{
    RadioLibInterface::reconfigure();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora.setSpreadingFactor(sf);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setBandwidth(bw, wideLora() && (getFreq() > 1000.0f));
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setCodingRate(cr, cr != 7); // use long interleaving except if CR is 4/7 which doesn't support it
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setSyncWord(syncWord);
    assert(err == RADIOLIB_ERR_NONE);

    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR1120_MAX_POWER);
    } else {
        limitPower(LR1110_MAX_POWER); // default clamp for non-wide freq range
    }

    err = lora.setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    // Apply RX gain mode - valid in STDBY, matches resetAGC() pattern
    err = lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);
    if (err != RADIOLIB_ERR_NONE)
        LOG_WARN("LR11x0 setRxBoostedGainMode %s%d", radioLibErr, err);

    startReceive(); // restart receiving

    return true;
}

template <typename T> void LR11x0Interface<T>::disableInterrupt()
{
    lora.clearIrqAction();
}

template <typename T> void LR11x0Interface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE) {
        LOG_DEBUG("LR11x0 standby failed with error %d", err);
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
    // LOG_DEBUG("PacketStatus %x", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
    mp->has_rx_rssi = true; // rx_rssi has explicit presence - a genuine reading must be marked present to survive encoding
    LOG_DEBUG("Corrected frequency offset: %f", lora.getFrequencyError());
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
    int err =
        lora.startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    if (err)
        LOG_ERROR("StartReceive error: %d", err);
    assert(err == RADIOLIB_ERR_NONE);

    RadioLibInterface::startReceive();

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
#endif
}

/** Is the channel currently active? */
template <typename T> bool LR11x0Interface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel
    ChannelScanConfig_t cfg = {.cad = {.symNum = NUM_SYM_CAD,
                                       .detPeak = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
                                       .detMin = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
                                       .exitMode = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
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
template <typename T> bool LR11x0Interface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation. Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    return receiveDetected(lora.getIrqStatus(), RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID,
                           RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
}

#ifdef LR11X0_AGC_RESET
template <typename T> void LR11x0Interface<T>::resetAGC()
{
    // Safety: don't reset mid-packet
    if (sendingPacket != NULL || (isReceiving && isActivelyReceiving()))
        return;

    LOG_DEBUG("LR11x0 AGC reset: warm sleep + Calibrate(0x3F)");

    // 1. Warm sleep - powers down the analog frontend, resetting AGC state
    lora.sleep(true, 0);

    // 2. Wake to RC standby for stable calibration
    lora.standby(RADIOLIB_LR11X0_STANDBY_RC, true);

    // 3. Calibrate all blocks (PLL, ADC, image, RC oscillators)
    //    calibrate() is protected on LR11x0, so use raw SPI (same as internal implementation)
    uint8_t calData = RADIOLIB_LR11X0_CALIBRATE_ALL;
    module.SPIwriteStream(RADIOLIB_LR11X0_CMD_CALIBRATE, &calData, 1, true, true);

    // 4. Re-calibrate image rejection for actual operating frequency
    //    Calibrate(0x3F) defaults to 902-928 MHz which is wrong for other regions.
    lora.calibrateImageRejection(getFreq() - 4.0f, getFreq() + 4.0f);

    // 5. Re-apply RX boosted gain mode
    lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);

    // 6. Resume receiving
    startReceive();
}
#endif

template <typename T> bool LR11x0Interface<T>::sleep()
{
    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_DEBUG("LR11x0 entering sleep mode");
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

template <typename T> int16_t LR11x0Interface<T>::getCurrentRSSI()
{
#ifdef ARCH_PORTDUINO_WASM
    float rssi = lora.getRSSI(); // installed RadioLib's LR11x0 getRSSI() is 0-arg
#else
    float rssi = lora.getRSSI(false, true);
#endif
    return (int16_t)round(rssi);
}

// Don't leak the aliases into the files InterfacesTemplates.cpp includes after this one.
#undef rfswitch_dio_pins
#undef rfswitch_table
#endif
