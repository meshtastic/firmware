#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "LR11x0Interface.h"
#include "BenchClock.h"
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

// Switch-capable DIOs in slot order with this part's constants; no DIO9, so slot 4 is DIO10.
static const int8_t lr11x0_switch_dio_nums[] = {5, 6, 7, 8, 10};
static const uint32_t lr11x0_switch_dio_consts[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                                    RADIOLIB_LR11X0_DIO8, RADIOLIB_LR11X0_DIO10};
static_assert(sizeof(lr11x0_switch_dio_nums) / sizeof(lr11x0_switch_dio_nums[0]) ==
                  sizeof(lr11x0_switch_dio_consts) / sizeof(lr11x0_switch_dio_consts[0]),
              "LR11x0 switch DIO numbers and constants must describe the same slots");

// This part has MODE_TX_HP/MODE_GNSS/MODE_WIFI and no MODE_RX_HF.
static const int32_t lr11x0_rfswitch_mode_map[RFSW_MODE_COUNT] = {
    LR11x0::MODE_STBY,  LR11x0::MODE_RX,       LR11x0::MODE_TX,   LR11x0::MODE_TX_HP,
    LR11x0::MODE_TX_HF, RFSW_MODE_UNSUPPORTED, LR11x0::MODE_GNSS, LR11x0::MODE_WIFI,
};

static uint32_t rfswitch_dio_pins[Module::RFSWITCH_MAX_PINS];
static Module::RfSwitchMode_t rfswitch_table[RFSW_MODE_COUNT + 1];
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
static inline float lr11x0TcxoDefaultVoltage()
{
    if (TCXO_OPTIONAL_ENABLED)
        return TCXO_OPTIONAL_DEFAULT_VOLTAGE;
    return 0;
}

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
        portduino_config.dio3_tcxo_voltage > 0 ? (float)portduino_config.dio3_tcxo_voltage / 1000 : lr11x0TcxoDefaultVoltage();
#elif defined(LR11X0_DIO3_TCXO_VOLTAGE)
    float tcxoVoltage = LR11X0_DIO3_TCXO_VOLTAGE;
#else
    float tcxoVoltage = lr11x0TcxoDefaultVoltage();
#endif

    // DIO3 is free to be used as an IRQ only while no TCXO Vref is driven on it
    if (tcxoVoltage > 0)
        LOG_DEBUG("LR11x0 TCXO Vref %f V on DIO3 (DIO3 unavailable as IRQ)", tcxoVoltage);
    else
        LOG_DEBUG("LR11x0 no TCXO Vref, XTAL only (DIO3 free as IRQ)");
    if (TCXO_OPTIONAL_ENABLED)
        LOG_DEBUG("TCXO_OPTIONAL: osc type unknown, probe XTAL first, TCXO Vref as fallback");

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

    // 1. XTAL first when probing (see TCXO_OPTIONAL_ENABLED), else the configured Vref. Not a
    // ternary: cppcheck sees both branches as 0 when tcxoVoltage above already folded to it.
    float attemptVoltage = tcxoVoltage;
    if (TCXO_OPTIONAL_ENABLED)
        attemptVoltage = 0;
    int res = tryBegin(1, attemptVoltage);

    // 2. XTAL failed with the chip present, so fall back to the TCXO if one was configured
    if (TCXO_OPTIONAL_ENABLED && res != RADIOLIB_ERR_NONE && res != RADIOLIB_ERR_CHIP_NOT_FOUND && tcxoVoltage > 0) {
        LOG_WARN("LR11x0 XTAL init failed (err %d), retry with TCXO Vref %f V", res, tcxoVoltage);
        attemptVoltage = tcxoVoltage;
        res = tryBegin(2, attemptVoltage);
        if (res == RADIOLIB_ERR_NONE)
            LOG_INFO("LR11x0 init success with TCXO Vref %f V", tcxoVoltage);
    }

    // 3. Some units need extra settling time, so give whichever oscillator we settled on one retry.
    //    After a step 2 fallback that is a second TCXO attempt, which is where settling actually matters.
    if (lr11x0SpiFailed(res)) {
        LOG_WARN("LR11x0 init failed with %d (SPI cmd failure), retry after delay", res);
        delay(100);
        res = tryBegin(3, attemptVoltage);
    }

    resolvedTcxoVoltage = attemptVoltage;

    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_INFO("LR11x0 init result %d", res);

    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || lr11x0SpiFailed(res)) {
#ifdef LR11X0_UPDATE_FIRMWARE_TO
        // An interrupted update leaves the radio sitting in bootloader mode, where begin() fails. Retry the
        // flash from here rather than giving up, otherwise the device could never recover on its own.
        LOG_WARN("LR11x0 did not start; firmware recovery in case update was interrupted");
        if (lora.updateFirmware(lr11xx_firmware_image, LR11XX_FIRMWARE_IMAGE_SIZE, true) == RADIOLIB_ERR_NONE) {
            LOG_INFO("LR1110 firmware recovery OK, re-init radio");
            res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
            if (res == RADIOLIB_ERR_NONE)
                resolvedTcxoVoltage = tcxoVoltage;
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
        LOG_WARN("LR1110 transceiver FW %d.%d older than %d.%d - updating. DO NOT POWER OFF: "
                 "rewrites radio's own flash",
                 transceiverFw >> 8, transceiverFw & 0xFF, LR11X0_UPDATE_FIRMWARE_TO >> 8, LR11X0_UPDATE_FIRMWARE_TO & 0xFF);

        int upd = lora.updateFirmware(lr11xx_firmware_image, LR11XX_FIRMWARE_IMAGE_SIZE, true);
        if (upd != RADIOLIB_ERR_NONE) {
            // The radio is likely sitting in bootloader mode. It is not bricked - the update is retried on
            // the next boot because the version check above will still see old (or unreadable) firmware.
            LOG_ERROR("LR1110 firmware update FAILED %s%d - power-cycle to retry", radioLibErr, upd);
            return false;
        }

        LOG_INFO("LR1110 firmware update complete, re-init radio");
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR11x0 re-init after firmware update failed %s%d", radioLibErr, res);
            return false;
        }
        resolvedTcxoVoltage = tcxoVoltage;

        if (lora.getVersionInfo(&version) == RADIOLIB_ERR_NONE) {
            transceiverFw = ((uint16_t)version.fwMajor << 8) | version.fwMinor;
            transceiverDevice = version.device;
            LOG_INFO("LR1110 now running transceiver FW %d.%d", version.fwMajor, version.fwMinor);
        }
    }
#endif

    applyBenchTcxoDelay(res);
#ifdef LR11X0_STANDBY_XOSC
    if (res == RADIOLIB_ERR_NONE)
        keepTcxoOnInStandby();
#endif
#ifdef LR11X0_TX_LAUNCH_OVERRIDE
    benchClockStart();
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
    if (dioAsRfSwitch)
        buildRfSwitchTable(rfswitch_dio_pins, rfswitch_table, RFSW_MODE_COUNT + 1, lr11x0_switch_dio_nums,
                           lr11x0_switch_dio_consts, sizeof(lr11x0_switch_dio_nums) / sizeof(lr11x0_switch_dio_nums[0]),
                           lr11x0_rfswitch_mode_map);
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
            LOG_INFO("Set RX gain to power saving mode; result: %d", res);
        }
    }

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template <typename T> int16_t LR11x0Interface<T>::programModemParams()
{
    // configure publicly accessible settings
    int16_t err = lora.setSpreadingFactor(sf);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setSpreadingFactor(%u) %s%d", sf, radioLibErr, err);
        return err;
    }

    err = lora.setBandwidth(bw, wideLora() && (getFreq() > 1000.0f));
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setBandwidth(%.1f) %s%d", bw, radioLibErr, err);
        return err;
    }

    err = lora.setCodingRate(cr, cr != 7); // use long interleaving except if CR is 4/7 which doesn't support it
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setCodingRate(%u) %s%d", cr, radioLibErr, err);
        return err;
    }

    err = lora.setSyncWord(syncWord);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setSyncWord %s%d", radioLibErr, err);
        return err;
    }

    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR1120_MAX_POWER);
    } else {
        limitPower(LR1110_MAX_POWER); // default clamp for non-wide freq range
    }

    err = lora.setPreambleLength(preambleLength);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setPreambleLength(%u) %s%d", preambleLength, radioLibErr, err);
        return err;
    }

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setFrequency(%.3f) %s%d", getFreq(), radioLibErr, err);
        return err;
    }

    err = lora.setOutputPower(power);
    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("LR11x0 setOutputPower(%d) %s%d", power, radioLibErr, err);
        return err;
    }

    // Apply RX gain mode - valid in STDBY, matches resetAGC() pattern
    err = lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);
    if (err != RADIOLIB_ERR_NONE)
        LOG_WARN("LR11x0 setRxBoostedGainMode %s%d", radioLibErr, err);

    return RADIOLIB_ERR_NONE;
}

template <typename T> void LR11x0Interface<T>::applyBenchTcxoDelay(int res)
{
#ifdef LR11X0_TCXO_DELAY_US
    // Bench: begin() leaves RadioLib's 5000 us TCXO start-up wait, and a clear CAD drops the chip to STBY_RC, so every
    // CAD and every TX after one waits that long for the oscillator. Reapplied after each begin(), which resets it.
    if (res == RADIOLIB_ERR_NONE && resolvedTcxoVoltage > 0) {
        const int16_t tcxoErr = lora.setTCXO(resolvedTcxoVoltage, (uint32_t)(LR11X0_TCXO_DELAY_US));
        LOG_INFO("LR11x0 TCXO start-up delay %u us %s%d", (unsigned)(LR11X0_TCXO_DELAY_US), radioLibErr, tcxoErr);
    } else if (res == RADIOLIB_ERR_NONE) {
        LOG_INFO("LR11x0 TCXO start-up delay not set: no TCXO Vref");
    }
#else
    (void)res;
#endif
}

template <typename T> bool LR11x0Interface<T>::reinitChip()
{
    // Clamp here, not just in programModemParams(): applyModemConfig() resets `power` to the raw
    // config value, and the recovery path reaches begin() without passing through the params clamp
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR1120_MAX_POWER);
    } else {
        limitPower(LR1110_MAX_POWER); // default clamp for non-wide freq range
    }

#ifdef LR11X0_RESUME_CONTINUOUS_RX
    rxArmedContinuous = false; // begin() resets the chip
#endif
    forgetChipState(); // begin() resets it
    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, resolvedTcxoVoltage);
    applyBenchTcxoDelay(res);
#ifdef LR11X0_STANDBY_XOSC
    if (res == RADIOLIB_ERR_NONE)
        keepTcxoOnInStandby();
#endif
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setRegulatorDCDC();

#ifdef LR11X0_DIO_AS_RF_SWITCH
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = portduino_config.has_rfswitch_table;
#else
    bool dioAsRfSwitch = false;
#endif

    // setRfSwitchTable() pushed the DIO switch config to the chip when init() called it; a reset chip has
    // lost it and begin() does not restore it
    if (res == RADIOLIB_ERR_NONE && dioAsRfSwitch)
        lora.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);

    if (res != RADIOLIB_ERR_NONE)
        LOG_ERROR("LR11x0 re-init failed %s%d", radioLibErr, res);
    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool LR11x0Interface<T>::reconfigure()
{
    RadioLibInterface::reconfigure();
    forgetChipState(); // the modem parameters are about to be reprogrammed

    // set mode to standby - a chip that lost its state to a reset/brownout can time out here (-707),
    // so don't let setStandby()'s assert fire before the recovery below gets a chance
    int16_t err = trySetStandby();
    if (err == RADIOLIB_ERR_NONE)
        err = programModemParams();

    if (err != RADIOLIB_ERR_NONE) {
        // A chip that fails standby or rejects parameter programming (typically WRONG_MODEM, -20) has
        // lost its runtime configuration - packet type included - to a chip-internal reset or brownout.
        // Recover in place: begin() hardware-resets the chip and restores the LoRa packet type. Crashing
        // here instead would reboot before MeshService persists the config change that triggered us.
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
        LOG_ERROR("LR11x0 rejected modem params, chip state lost? Full re-init");
        if (!reinitChip() || (err = programModemParams()) != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR11x0 unrecoverable %s%d, radio down until reboot", radioLibErr, err);
            return false;
        }
        LOG_INFO("LR11x0 recovered after re-init");
    }

    startReceive(); // restart receiving

    return true;
}

template <typename T> void LR11x0Interface<T>::clearRadioIsr()
{
    lora.clearIrqAction();
}

template <typename T> int16_t LR11x0Interface<T>::trySetStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

#ifdef LR11X0_STANDBY_XOSC
    // SetStandby 0x01 is STBY_XOSC. RadioLib (to 7.8.1) defines RADIOLIB_LR11X0_STANDBY_XOSC as 0x00, which is STBY_RC.
    int16_t err = lora.standby(0x01);
#else
    int16_t err = lora.standby();
#endif

    if (err != RADIOLIB_ERR_NONE) {
        LOG_DEBUG("LR11x0 standby failed, err %d", err);
    }

    isReceiving = false; // If we were receiving, not any more
#ifdef LR11X0_RESUME_CONTINUOUS_RX
    rxArmedContinuous = false;
#endif
    rxSighting.reset();
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
    return err;
}

template <typename T> void LR11x0Interface<T>::setStandby()
{
    int16_t err = trySetStandby();
    assert(err == RADIOLIB_ERR_NONE);
}

/**
 * Add SNR data to received messages
 */
template <typename T> void LR11x0Interface<T>::forgetChipState()
{
#ifdef LR11X0_TX_STAGE_EARLY
    earlyStagedLen = 0;
#endif
#ifdef LR11X0_CAD_SLIM
    cadParamsValid = false;
#endif
}

#ifdef LR11X0_STANDBY_XOSC
template <typename T> void LR11x0Interface<T>::keepTcxoOnInStandby()
{
    // From STBY_RC, every SetCad, SetRx and SetTx first waits out the TCXO start-up; STBY_XOSC keeps the TCXO running
    const int16_t res = lora.setRxTxFallbackMode(RADIOLIB_LR11X0_FALLBACK_MODE_STBY_XOSC);
    LOG_DEBUG("LR11x0 keep TCXO on in standby, result: %d", res);
}
#endif

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
#ifdef LR11X0_RX_REARM_AT_TX_DONE
    rearmState = REARM_NONE; // only this TX's TX_DONE may re-arm, never a stale one from a TX the poll completed
    rxArmedBeforeTxDone = false;
#endif
    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void LR11x0Interface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    int16_t err = trySetStandby();

    if (err == RADIOLIB_ERR_NONE) {
        lora.setPreambleLength(preambleLength); // Solve RX ack fail after direct message sent.  Not sure why this is needed.

        // We use a 16 bit preamble so this should save some power by letting radio sit in standby mostly.
        err =
            lora.startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("StartReceive error: %d", err);
        if (maybeRecoverChipStateLoss()) {
            lora.setPreambleLength(preambleLength);
            err = lora.startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS,
                                    RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
        }
    }

    if (err != RADIOLIB_ERR_NONE) {
        // No assert: leave RX off rather than reboot; periodicRadioMaintenance() re-arms it, throttled
        LOG_ERROR("LR11x0 RX offline %s%d", radioLibErr, err);
        rxOffline = true;
        return;
    }

    RadioLibInterface::startReceive();
#ifdef LR11X0_RESUME_CONTINUOUS_RX
    rxArmedContinuous = true; // RADIOLIB_LR11X0_RX_TIMEOUT_INF: continuous
#endif
#ifdef LR11X0_READ_CHIP_MODE
    // Positive control for the mode read: the chip was just put in RX, so this should say 0x08.
    static uint8_t rxStarts = 0;
    if ((rxStarts++ & 0x3F) == 0)
        LOG_DEBUG("LR11x0 chip mode after RX start: stat2 mode 0x%02x (RX is 0x08)", readChipMode());
#endif

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
#endif
}

#ifdef LR11X0_RESUME_CONTINUOUS_RX
template <typename T> bool LR11x0Interface<T>::resumeRunningReceive()
{
    // A continuous RX keeps listening after RX_DONE and after CRC or header errors, so pick it back up instead of a
    // standby and restart, which is deaf through a TCXO start-up. Checked on the chip: this LR11x0 behaviour is the
    // round's question.
    if (!rxArmedContinuous)
        return false;
    const uint8_t mode = readChipMode();
    if (mode != RADIOLIB_LR11X0_STAT_2_MODE_RX) {
        LOG_WARN("LR11x0 RX not running after a frame (stat2 mode 0x%02x), restarting it", mode);
        rxArmedContinuous = false;
        return false;
    }
    // readData() clears these, but handleReceiveInterrupt()'s early outs do not, and a latched one would hold the pin high
    // past the re-arm. With the readout task, it clears what it reads, and a clear here could take an unread RX_DONE.
    if (!rxReadoutActive())
        lora.clearIrqFlags(RADIOLIB_LR11X0_IRQ_RX_DONE | RADIOLIB_LR11X0_IRQ_CRC_ERR | RADIOLIB_LR11X0_IRQ_HEADER_ERR |
                           RADIOLIB_LR11X0_IRQ_TIMEOUT);
    if (deafSinceMs) {
        LOG_RADIO_EDGE("RX still running, re-arm skipped after %s, readout %u ms", deafFor,
                       (unsigned)(Time::getMillis() - deafSinceMs));
        deafSinceMs = 0; // the chip never stopped listening, so there is no deaf window to report
    }
    rxSighting.reset(); // RX_DONE ends the frame's hold, as the standby it replaces would
    RadioLibInterface::startReceive();
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag(); // an RX_DONE that beat the arm
    return true;
}
#endif

#ifdef LR11X0_RX_REARM_AT_TX_DONE
template <typename T> bool LR11x0Interface<T>::rearmReceiveFromIsr()
{
    // After TX_DONE the chip sits in standby until the radio thread re-arms it, and a main-loop hold can make that
    // hundreds of ms. The interrupt cannot call RadioLib, so the readout task, above the loop, re-arms as soon as the
    // interrupt returns.
    rearmState = REARM_PENDING;
    if (requestRearmFromIsr())
        return true;
    rearmState = REARM_NONE;
    return false;
}

template <typename T> void LR11x0Interface<T>::rearmReceiveFromTask()
{
    // Only a TX_DONE the thread has not yet handled: where adopt gave up waiting, the thread's own startReceive() has RX
    if (rearmState != REARM_PENDING)
        return;
    // What startReceive() sends, less the standby: after TX_DONE the chip has already fallen back to standby.
    const uint32_t t0 = benchClock();
    int16_t err = lora.setPreambleLength(preambleLength);
    if (err == RADIOLIB_ERR_NONE)
        err =
            lora.startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    rearmUs = benchClockToUs(benchClock() - t0);
    rearmErr = err;
    rearmTicks = xTaskGetTickCount();
    if (err != RADIOLIB_ERR_NONE) {
        rearmState = REARM_FAILED;
        return;
    }
    rearmState = REARM_ARMED;
    // The TX_DONE interrupt detached the radio's interrupt. Attach the RX one now, so a frame that ends before the radio
    // thread runs wakes this task to read it, rather than waiting in the chip for the next frame to overwrite it.
    rxArmedBeforeTxDone = true;
#ifdef LR11X0_RESUME_CONTINUOUS_RX
    rxArmedContinuous = true; // so a frame the task reads before the thread adopts finds the chip still listening
#endif
    enableInterrupt(isrRxLevel0);
}

template <typename T> bool LR11x0Interface<T>::adoptReceiveArmedFromIsr()
{
    // On one core the task, above this thread, has already run. Where it has not (blocked on a lock), give it a moment.
    for (unsigned waited = 0; rearmState == REARM_PENDING && waited < 20; waited++)
        vTaskDelay(pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
    const uint8_t state = rearmState;
    rearmState = REARM_NONE;
    rxArmedBeforeTxDone = false; // this is the TX_DONE; frames the task read meanwhile were delivered ahead of it
    if (state == REARM_PENDING) {
        // Leave the task nothing to do: the thread's startReceive() takes it from here
        LOG_WARN("RX re-arm at TX_DONE: readout task did not run");
        return false;
    }
    if (state == REARM_FAILED) {
        LOG_WARN("RX re-arm at TX_DONE failed %s%d, restarting RX", radioLibErr, rearmErr);
        return false;
    }
    if (state != REARM_ARMED)
        return false;
    const uint32_t heldMs = (uint32_t)(((uint64_t)(xTaskGetTickCount() - rearmTicks) * 1000) / configTICK_RATE_HZ);
    LOG_RADIO_EDGE("Radio back in RX at TX_DONE, re-arm %u us, %u ms before the handler ran", (unsigned)rearmUs,
                   (unsigned)heldMs);
    deafSinceMs = 0; // listening since the task re-armed: no deaf window to report
    RadioLibInterface::startReceive();
#ifdef LR11X0_RESUME_CONTINUOUS_RX
    rxArmedContinuous = true; // the task armed a continuous RX
#endif
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag(); // an RX_DONE that completed while the handler waited
    return true;
}
#endif

/** Is the channel currently active? */
template <typename T> bool LR11x0Interface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel.
    // symNum is SetCadParams SymbolNum - a plain count - so take it straight from getCadSymbolCount(),
    // which follows the band (8 on 2.4 GHz, as SX1280 scans) and is what sizes the CW slot.
    const uint8_t symNum = getCadSymbolCount();
    // detPeak: Semtech SWSD003 lr11xx/apps/cad/main_cad.c optimized_parameters[symbols][BW][SF5..SF12],
    // its measured best CAD detection rates. RadioLib's  default is this table's [2 symbols][BW250] row.
    // 50 = SWSD003's CAD_DETECT_PEAK fallback, used where it has no measured value.
    static constexpr uint8_t CAD_DET_PEAK[4][4][8] = {
        // Each block is one symbol count. Within a block the 4 rows are BW 62.5 / 125 / 250 / 500 kHz,
        // and the 8 columns are SF5..SF12.
        // 2 symbols:
        {{39, 45, 47, 53, 59, 61, 64, 63},
         {44, 51, 49, 55, 56, 60, 62, 68},
         {48, 48, 50, 55, 55, 59, 61, 65},
         {76, 80, 71, 77, 69, 50, 50, 50}}, // SF10-12: SWSD003 has no measurement, 50 is its fallback
        // 4 symbols:
        {{43, 45, 45, 50, 53, 57, 59, 63},
         {44, 46, 49, 53, 53, 55, 57, 62},
         {45, 47, 47, 51, 51, 56, 59, 62},
         {58, 66, 58, 65, 62, 55, 60, 57}},
        // 8 symbols:
        {{43, 44, 46, 48, 51, 53, 56, 59},
         {45, 43, 44, 50, 52, 55, 56, 61},
         {42, 44, 45, 48, 50, 53, 55, 60},
         {49, 52, 50, 59, 56, 57, 57, 60}},
        // 16 symbols:
        {{41, 44, 43, 46, 49, 52, 54, 60},
         {42, 42, 43, 48, 49, 53, 55, 59},
         {41, 42, 43, 48, 48, 53, 54, 58},
         {44, 47, 45, 53, 52, 53, 57, 62}}};
    // SWSD003 characterises symbol counts 2/4/8/16 against the sub-GHz LoRa modem's four bandwidths.
    // Use exact matches to avoid mixing widelora (406.25/812.5/1625 kHz)
    // Anything unmatched uses RadioLib default.
    // Whole sub-GHz set today; a narrower BW or a symbol count outside 2/4/8/16 needs a row adding.
    static constexpr float TABLE_BW_KHZ[4] = {62.5f, 125.0f, 250.0f, 500.0f};
    const int symIdx = symNum == 2 ? 0 : symNum == 4 ? 1 : symNum == 8 ? 2 : symNum == 16 ? 3 : -1;
    int bwIdx = -1;
    for (int i = 0; i < 4; i++) {
        if (bw > TABLE_BW_KHZ[i] - 1.0f && bw < TABLE_BW_KHZ[i] + 1.0f)
            bwIdx = i;
    }
    const uint8_t detPeak = (symIdx < 0 || bwIdx < 0) ? (uint8_t)RADIOLIB_LR11X0_CAD_PARAM_DEFAULT
                                                      : CAD_DET_PEAK[symIdx][bwIdx][(sf >= 5 && sf <= 12) ? sf - 5 : 6];
    // Keep preamble/header off the pin - they would fire the ISR mid-frame. Same set the normal RX path
    // already programs (stageMode sends irqFlags & irqMask), so isActivelyReceiving() sees no change.
    const uint32_t cadIrqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT) |
                                 (1UL << RADIOLIB_IRQ_CRC_ERR) | (1UL << RADIOLIB_IRQ_HEADER_ERR);
    // UM Table 8-8: the timeout bounds the RX that follows a detection, so use one max-length airtime.
    // RadioLib scales it by 30.52 us against a real 31.25, landing ~2% long - not worth pre-compensating.
    const RadioLibTime_t cadRxTimeoutUsec =
        (RadioLibTime_t)getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader), false) * 1000;
    ChannelScanConfig_t cfg = {.cad = {.symNum = symNum,
                                       .detPeak = detPeak,
                                       // PARAM_DEFAULT lands on 10 in RadioLib, which is SWSD003's CAD_DETECT_MIN
                                       .detMin = RADIOLIB_LR11X0_CAD_PARAM_DEFAULT,
                                       .exitMode = RADIOLIB_LR11X0_CAD_EXIT_MODE_RX,
                                       .timeout = cadRxTimeoutUsec,
                                       .irqFlags = cadIrqFlags,
                                       .irqMask = cadIrqFlags}}; // ignored: startChannelScan() sends irqFlags twice
#ifdef LR11X0_TX_LAUNCH_OVERRIDE
    prestagedLen = 0; // only a clear verdict from this scan may launch what it stages
#endif
#ifdef LR11X0_TX_STAGE_EARLY
    (void)takeEarlyTxStage(); // written during the backoff: nothing to write in the standby below
#endif
    int16_t result = trySetStandby();
    if (result == RADIOLIB_ERR_NONE) {
#ifdef LR11X0_TX_PRESTAGE
        // Write the payload now, while nothing is listening anyway, rather than after the verdict. Only the buffer:
        // the packet params keep RX's maximum length, so a detection's RX still takes a full-length frame. It is the
        // TX buffer, which neither the CAD nor a detection's RX touches.
        if (scanForTx && !prestagedLen) {
            const size_t numbytes = encodeRadioBuffer(scanForTx);
            if (lora.writeBuffer8((uint8_t *)&radioBuffer, numbytes) == RADIOLIB_ERR_NONE) {
                prestagedLen = numbytes;
                prestagedId = scanForTx->id;
#ifdef LR11X0_TX_STAGE_EARLY
                noteTxBuffer(numbytes); // so a busy verdict's rescan need not write it again
#endif
            }
        }
#ifdef LR11X0_CAD_EXIT_KEYUP
        if (scanForTx && prestagedLen) {
            // Arm the whole TX, so a chip that honours TX-when-clear sends this payload at the verdict. The TX length also
            // bounds a detection's RX, so the probe line logs it. Under LBT the CAD timeout is also the TX timeout, so give
            // it a quarter more than one max-length frame.
            if (lora.setPacketParamsLoRa(preambleLength, RADIOLIB_LRXXXX_LORA_HEADER_EXPLICIT, (uint8_t)prestagedLen,
                                         RADIOLIB_LRXXXX_LORA_CRC_ENABLED,
                                         RADIOLIB_LR11X0_LORA_IQ_STANDARD) == RADIOLIB_ERR_NONE) {
                cfg.cad.exitMode = LR11X0_CAD_EXIT_KEYUP;
                cfg.cad.timeout = cadRxTimeoutUsec * 5 / 4;
                cfg.cad.irqFlags |= 1UL << RADIOLIB_IRQ_TX_DONE;
            }
        }
#endif
#endif
#ifdef LR11X0_CAD_SLIM
        result = scanChannelForTx(cfg);
#else
        result = lora.scanChannel(cfg);
#endif
#ifdef LR11X0_TX_LAUNCH_OVERRIDE
        cadVerdictClock = benchClock();
#endif
#ifdef LR11X0_CAD_EXIT_KEYUP
        chipKeyedUp = false;
        if (cfg.cad.exitMode != RADIOLIB_LR11X0_CAD_EXIT_MODE_RX) {
            const uint8_t mode = readChipMode();
            const char *modeName = mode == RADIOLIB_LR11X0_STAT_2_MODE_TX        ? "TX"
                                   : mode == RADIOLIB_LR11X0_STAT_2_MODE_RX      ? "RX"
                                   : mode == RADIOLIB_LR11X0_STAT_2_MODE_STBY_RC ? "STBY_RC"
                                   : mode == RADIOLIB_LR11X0_STAT_2_MODE_FS      ? "FS"
                                                                                 : "other";
            LOG_DEBUG("CAD exit 0x%02x: %s, chip in %s (stat2 mode 0x%02x), staged len %u", (unsigned)cfg.cad.exitMode,
                      result == RADIOLIB_CHANNEL_FREE    ? "clear"
                      : result == RADIOLIB_LORA_DETECTED ? "busy"
                                                         : "error",
                      modeName, mode, (unsigned)prestagedLen);
            if (result == RADIOLIB_CHANNEL_FREE && mode == RADIOLIB_LR11X0_STAT_2_MODE_TX) {
                chipKeyedUp = true;
            } else if (result == RADIOLIB_CHANNEL_FREE && mode != RADIOLIB_LR11X0_STAT_2_MODE_STBY_RC) {
                lora.standby(); // TX-when-clear not honoured and the chip is somewhere else: launch from standby
            } else if (result == RADIOLIB_LORA_DETECTED && mode != RADIOLIB_LR11X0_STAT_2_MODE_RX) {
                // No RX on detection (LBT's standby, or 0x11 not honoured): no handoff to adopt, so the caller's
                // rearmReceive() restarts RX
                lora.clearIrqFlags(RADIOLIB_LR11X0_IRQ_CAD_DONE | RADIOLIB_LR11X0_IRQ_CAD_DETECTED);
                prestagedLen = 0;
                return true;
            } else if (result != RADIOLIB_CHANNEL_FREE && result != RADIOLIB_LORA_DETECTED &&
                       result != RADIOLIB_ERR_WRONG_MODEM) {
                // The chip refused the exit mode or the scan failed: report busy rather than TX without a CAD. A lost
                // modem type still takes the recovery below.
                LOG_WARN("CAD exit 0x%02x: scan returned %d", (unsigned)cfg.cad.exitMode, result);
                prestagedLen = 0;
                return true;
            }
        }
#endif
#ifdef LR11X0_TX_LAUNCH_OVERRIDE
        if (result != RADIOLIB_CHANNEL_FREE)
            prestagedLen = 0; // no TX follows this scan
#endif
        if (result == RADIOLIB_LORA_DETECTED) {
            // The chip auto-entered RX. Drop the latched CAD verdict so the pin releases and the coming
            // RX_DONE is a clean edge.
            lora.clearIrqFlags(RADIOLIB_LR11X0_IRQ_CAD_DONE | RADIOLIB_LR11X0_IRQ_CAD_DETECTED);
            noteCadHandoffToRx(); // nothing below arms the radio; the caller's rearmReceive() adopts it
            return true;
        }
        if (result != RADIOLIB_ERR_WRONG_MODEM)
            return false;
    }

    // standby failed or the LoRa modem type is gone - the chip lost its runtime state
    maybeRecoverChipStateLoss();
    return false; // report the channel free: a recovered chip can TX, a dead one fails startSend safely
}

#ifdef LR11X0_READ_CHIP_MODE
template <typename T> uint8_t LR11x0Interface<T>::readChipMode() const
{
    // The chip answers any NOP transfer with stat1, stat2 and the IRQ word, but SPItransferStream() drops the configured
    // status width (8 bits on LR11x0) from the front of what it hands back, so stat2 lands in buff[0], not buff[1].
    // Read the width rather than change it: the readout task and the thread share this Module. FS is the chip on its
    // way to TX or RX, so look again for up to 1 ms.
    uint8_t buff[6] = {0};
    uint8_t mode = 0xFF;
    for (int tries = 0; tries < 10; tries++) {
        const uint8_t skipped = statusModule->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS] / 8;
        if (skipped > 1 || statusModule->SPItransferStream(NULL, 0, false, NULL, buff, sizeof(buff), true) != RADIOLIB_ERR_NONE)
            return 0xFF;
        mode = buff[1 - skipped] & 0x0E;
        if (mode != RADIOLIB_LR11X0_STAT_2_MODE_FS)
            break;
        delayMicroseconds(100);
    }
    return mode;
}
#endif

#ifdef LR11X0_TX_LAUNCH_OVERRIDE
template <typename T> int16_t LR11x0Interface<T>::launchTransmit(size_t numbytes)
{
    // Everything from the CAD verdict to SET_TX is time the channel goes unwatched. Time each step; with a prestaged
    // payload, send only the rest of RadioLib's TX staging.
    const uint32_t t0 = benchClock();
    const bool prestaged = prestagedLen != 0 && prestagedLen == numbytes && sendingPacket && sendingPacket->id == prestagedId;
    prestagedLen = 0;
#ifdef LR11X0_TX_STAGE_EARLY
    earlyStagedLen = 0; // this packet is on its way; RadioLib's staging would also write over the buffer
#endif
    int16_t res;
#ifdef LR11X0_CAD_EXIT_KEYUP
    if (chipKeyedUp) {
        chipKeyedUp = false;
        if (prestaged) {
            // The chip went from the clear CAD straight to TX with this payload. Only release the latched CAD flags,
            // so the pin drops and TX_DONE is a fresh edge for the TX interrupt startSend() attaches next.
            lora.clearIrqFlags(RADIOLIB_LR11X0_IRQ_CAD_DONE | RADIOLIB_LR11X0_IRQ_CAD_DETECTED);
            LOG_TRACE("Tx launch steps: chip, len %u, verdict to launch %u us", (unsigned)numbytes,
                      (unsigned)benchClockToUs(benchClock() - cadVerdictClock));
            return RADIOLIB_ERR_NONE;
        }
        // Should not happen: the chip is sending a different packet from the one being launched. Stop it and send ours.
        LOG_WARN("CAD exit: chip keyed up with a stale payload, restarting TX");
        lora.standby();
    }
#endif
#ifdef LR11X0_TX_PRESTAGE
    if (prestaged) {
        // What stageMode(TX) sends, less the buffer (already written) and the packet-type read. The packet params
        // are the ones init() gives RadioLib (explicit header, CRC on, standard IQ), with our length.
        res = lora.setPacketParamsLoRa(preambleLength, RADIOLIB_LRXXXX_LORA_HEADER_EXPLICIT, (uint8_t)numbytes,
                                       RADIOLIB_LRXXXX_LORA_CRC_ENABLED, RADIOLIB_LR11X0_LORA_IQ_STANDARD);
        if (res == RADIOLIB_ERR_NONE)
            res = lora.setDioIrqParams(RADIOLIB_LR11X0_IRQ_TX_DONE | RADIOLIB_LR11X0_IRQ_TIMEOUT);
        if (res == RADIOLIB_ERR_NONE)
            res = lora.clearIrqState(RADIOLIB_LR11X0_IRQ_ALL);
        if (res == RADIOLIB_ERR_NONE)
            lora.stagedMode = RADIOLIB_RADIO_MODE_TX; // what stageMode() leaves for launchMode()
    } else
#endif
    {
        RadioModeConfig_t cfg = {.transmit = {.data = (uint8_t *)&radioBuffer, .len = numbytes, .addr = 0}};
        res = lora.stageMode(RADIOLIB_RADIO_MODE_TX, &cfg);
    }
    const uint32_t tStage = benchClock();
    if (res == RADIOLIB_ERR_NONE)
        res = lora.launchMode(); // RF switch, SET_TX, then the BUSY wait for the PA ramp
    const uint32_t tDone = benchClock();
    LOG_TRACE("Tx launch steps: %s, len %u, verdict to launch %u, stage %u, settx+busy %u us",
              prestaged ? "prestaged" : "radiolib", (unsigned)numbytes, (unsigned)benchClockToUs(t0 - cadVerdictClock),
              (unsigned)benchClockToUs(tStage - t0), (unsigned)benchClockToUs(tDone - tStage));
    return res;
}
#endif

#ifdef LR11X0_TX_STAGE_EARLY
template <typename T> void LR11x0Interface<T>::noteTxBuffer(size_t numbytes)
{
    earlyStagedLen = numbytes;
    memcpy(earlyStagedBytes, &radioBuffer, numbytes); // still the payload just written
}

template <typename T> void LR11x0Interface<T>::stageTxEarly(meshtastic_MeshPacket *p)
{
    // Only while RX runs: a TX in flight is using the buffer, and SPI would wake a sleeping chip
    if (!p || sendingPacket || !isReceiving)
        return;
    const size_t numbytes = encodeRadioBuffer(p);
    if (numbytes == 0 || numbytes > sizeof(earlyStagedBytes))
        return;
    if (earlyStagedLen == numbytes && memcmp(earlyStagedBytes, &radioBuffer, numbytes) == 0)
        return; // a redraw of the same packet: still in the buffer
    earlyStagedLen = 0;
    if (lora.writeBuffer8((uint8_t *)&radioBuffer, numbytes) == RADIOLIB_ERR_NONE) {
        noteTxBuffer(numbytes);
        LOG_TRACE("TX staged early, %u bytes, id 0x%08x", (unsigned)numbytes, p->id);
    }
}

template <typename T> bool LR11x0Interface<T>::takeEarlyTxStage()
{
    if (!earlyStagedLen || !scanForTx)
        return false;
    const size_t numbytes = encodeRadioBuffer(scanForTx); // CPU only, no bus traffic
    if (numbytes != earlyStagedLen || memcmp(earlyStagedBytes, &radioBuffer, numbytes) != 0) {
        LOG_DEBUG("TX staged early: not the packet being scanned for, restage at the scan");
        earlyStagedLen = 0; // whatever the scan stages goes over it
        return false;
    }
    prestagedLen = numbytes;
    prestagedId = scanForTx->id;
    return true;
}
#endif

#ifdef LR11X0_CAD_SLIM
template <typename T> int16_t LR11x0Interface<T>::scanChannelForTx(const ChannelScanConfig_t &cfg)
{
    // lora.scanChannel(cfg) less its packet-type reads and its standby, which trySetStandby() has just done (a second
    // one would also drop STBY_XOSC to STBY_RC), and with the CAD parameters sent only when they change
    module.setRfSwitchState(Module::MODE_RX);
    const uint32_t irqs = lora.getIrqMapped(cfg.cad.irqFlags);
    int16_t res = lora.setDioIrqParams(irqs, irqs);
    if (res == RADIOLIB_ERR_NONE)
        res = lora.clearIrqState(RADIOLIB_LR11X0_IRQ_ALL);
    // As RadioLib's startCad(): its defaults, and the timeout in 30.52 us steps
    static constexpr uint8_t DEFAULT_DET_PEAK[8] = {48, 48, 50, 55, 55, 59, 61, 65};
    const uint32_t timeoutRaw = (uint32_t)((float)cfg.cad.timeout / 30.52f);
    const uint8_t cadParams[8] = {
        cfg.cad.symNum != RADIOLIB_LR11X0_CAD_PARAM_DEFAULT ? cfg.cad.symNum : (uint8_t)2,
        cfg.cad.detPeak != RADIOLIB_LR11X0_CAD_PARAM_DEFAULT ? cfg.cad.detPeak
                                                             : DEFAULT_DET_PEAK[(sf >= 5 && sf <= 12) ? sf - 5 : 0],
        cfg.cad.detMin != RADIOLIB_LR11X0_CAD_PARAM_DEFAULT ? cfg.cad.detMin : (uint8_t)10,
        cfg.cad.exitMode != RADIOLIB_LR11X0_CAD_PARAM_DEFAULT ? cfg.cad.exitMode : (uint8_t)RADIOLIB_LR11X0_CAD_EXIT_MODE_STBY_RC,
        (uint8_t)((timeoutRaw >> 24) & 0xFF),
        (uint8_t)((timeoutRaw >> 16) & 0xFF),
        (uint8_t)((timeoutRaw >> 8) & 0xFF),
        (uint8_t)(timeoutRaw & 0xFF)};
    if (res == RADIOLIB_ERR_NONE && (!cadParamsValid || memcmp(cadParams, cadParamsSent, sizeof(cadParams)) != 0)) {
        res = lora.setCadParams(cadParams[0], cadParams[1], cadParams[2], cadParams[3], timeoutRaw);
        cadParamsValid = res == RADIOLIB_ERR_NONE;
        if (cadParamsValid)
            memcpy(cadParamsSent, cadParams, sizeof(cadParams));
    }
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCad();
    if (res != RADIOLIB_ERR_NONE)
        return res;
    // As scanChannel(): wait for the IRQ pin to report the CAD finished, then read the verdict from the IRQ status alone
    while (!module.hal->digitalRead(module.getIrq()))
        module.hal->yield();
    const uint32_t irq = lora.getIrqStatus();
    if (irq & RADIOLIB_LR11X0_IRQ_CAD_DETECTED)
        return RADIOLIB_LORA_DETECTED;
    if (irq & RADIOLIB_LR11X0_IRQ_CAD_DONE)
        return RADIOLIB_CHANNEL_FREE;
    return RADIOLIB_ERR_UNKNOWN;
}
#endif

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
    forgetChipState(); // the calibration below may not keep it
#ifdef LR11X0_RESUME_CONTINUOUS_RX
    rxArmedContinuous = false; // the warm sleep below stops RX
#endif

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
    forgetChipState();     // sleep without retention loses it
    (void)trySetStandby(); // Stop any pending operations - the chip is being put to sleep, a failure must not crash

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

#endif
