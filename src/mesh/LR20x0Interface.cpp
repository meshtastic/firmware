#include "configuration.h"

#if (defined(USE_LR2021) || defined(ARCH_PORTDUINO)) && RADIOLIB_EXCLUDE_LR2021 != 1
#include "LR20x0Band.h"
#include "LR20x0Interface.h"
#include "error.h"
#include "mesh/NodeDB.h"

#if defined(LR2021_DCDC_WORKAROUND) && RADIOLIB_GODMODE
// The DCDC sensitivity workaround pokes RadioLib-internal DCDC registers that are NOT exposed via the
// public LR2021.h, so pull in the internal register map explicitly. Opt-in only (see LR2021_DCDC_WORKAROUND).
#include <modules/LR2021/LR2021_registers.h>
#endif

// Keep LR20x0 naming while RadioLib exposes LR2021 symbols.
#ifndef LR20x0
#define LR20x0 LR2021
#endif

#ifdef LR2021_DIO_AS_RF_SWITCH
#include "rfswitch.h"
#elif ARCH_PORTDUINO
#include "PortduinoGlue.h"

// Switch-capable DIOs in slot order with this part's constants.
static const int8_t lr20x0_switch_dio_nums[] = {5, 6, 7, 8, 9, 10, 11};
static const uint32_t lr20x0_switch_dio_consts[] = {RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_LR2021_DIO7,
                                                    RADIOLIB_LR2021_DIO8, RADIOLIB_LR2021_DIO9, RADIOLIB_LR2021_DIO10,
                                                    RADIOLIB_LR2021_DIO11};
static_assert(sizeof(lr20x0_switch_dio_nums) / sizeof(lr20x0_switch_dio_nums[0]) ==
                  sizeof(lr20x0_switch_dio_consts) / sizeof(lr20x0_switch_dio_consts[0]),
              "LR20x0 switch DIO numbers and constants must describe the same slots");

// This part has MODE_RX_HF and no MODE_TX_HP/MODE_GNSS/MODE_WIFI.
static const int32_t lr20x0_rfswitch_mode_map[RFSW_MODE_COUNT] = {
    LR20x0::MODE_STBY,  LR20x0::MODE_RX,    LR20x0::MODE_TX,       RFSW_MODE_UNSUPPORTED,
    LR20x0::MODE_TX_HF, LR20x0::MODE_RX_HF, RFSW_MODE_UNSUPPORTED, RFSW_MODE_UNSUPPORTED,
};

static uint32_t lr20x0_rfswitch_dio_pins[Module::RFSWITCH_MAX_PINS];
static Module::RfSwitchMode_t lr20x0_rfswitch_table[RFSW_MODE_COUNT + 1];
#else
static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    {LR20x0::MODE_STBY, {}},  {LR20x0::MODE_RX, {}},    {LR20x0::MODE_TX, {}},
    {LR20x0::MODE_RX_HF, {}}, {LR20x0::MODE_TX_HF, {}}, END_OF_MODE_TABLE,
};
#endif

#ifdef LR2021_CUSTOM_PA_TABLE
#include "pa_table.h"
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

// Last programmed carrier; LF/HF hops use full begin() (live setOutputPower returns -706).
static float lr20x0LastFreqMHz = 0;

// Unlike SX126x/LR11x0 (on/off bool), the LR2021 RX gain boost is a 0-7 level (0 = disabled, 7 = max boost).
// Map the historical on/off sx126x_rx_boosted_gain flag to max boost when enabled.
#ifndef LR2021_RX_GAIN_BOOST_LEVEL
#define LR2021_RX_GAIN_BOOST_LEVEL 7
#endif

template <typename T>
LR20x0Interface<T>::LR20x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_DEBUG_RADIO("LR20x0Interface(cs=%d, irq=%d, rst=%d, busy=%d)", cs, irq, rst, busy);
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
    // An explicit Vref wins; probing with none given tries the radio default first.
    float tcxoVoltage;
    if (portduino_config.dio3_tcxo_voltage > 0)
        tcxoVoltage = (float)portduino_config.dio3_tcxo_voltage / 1000;
    else if (TCXO_OPTIONAL_ENABLED)
        tcxoVoltage = TCXO_OPTIONAL_DEFAULT_VOLTAGE;
    else
        tcxoVoltage = 0;
    if (portduino_config.dio3_tcxo_voltage <= 0 && TCXO_OPTIONAL_ENABLED)
        LOG_DEBUG_RADIO("TCXO_OPTIONAL: no Lora.DIO3_TCXO_VOLTAGE set, trying default TCXO Vref %f V first", tcxoVoltage);
#elif defined(LR2021_DIO3_TCXO_VOLTAGE)
    float tcxoVoltage = LR2021_DIO3_TCXO_VOLTAGE;
    LOG_DEBUG_RADIO("LR2021_DIO3_TCXO_VOLTAGE defined, DIO3 as TCXO Vref %f V", LR2021_DIO3_TCXO_VOLTAGE);
    // (DIO3 is not free to be used as an IRQ)
#elif defined(TCXO_OPTIONAL)
    float tcxoVoltage = 1.6f; // TCXO_OPTIONAL: try default 1.6 V first, fall back to XTAL on failure
    LOG_DEBUG_RADIO("TCXO_OPTIONAL: no LR2021_DIO3_TCXO_VOLTAGE, try default TCXO Vref 1.6 V first");
#else
    float tcxoVoltage =
        0; // "TCXO reference voltage to be set on DIO3. Defaults to 1.6 V, set to 0 to skip." per
           // https://github.com/jgromes/RadioLib/blob/690a050ebb46e6097c5d00c371e961c1caa3b52e/src/modules/LR11x0/LR11x0.h#L471C26-L471C104
    // (DIO3 is free to be used as an IRQ)
    LOG_DEBUG_RADIO("LR2021_DIO3_TCXO_VOLTAGE not defined, DIO3 not used as TCXO Vref");
#endif

    RadioLibInterface::init();
    LOG_INFO("r148i: lr2021_cad_lbt=%d", LR2021_CAD_LBT); // bench: which arm is flashed

#ifdef LR2021_IRQ_DIO_NUM
    lora.irqDioNum = LR2021_IRQ_DIO_NUM;
    LOG_DEBUG_RADIO("Set irqDioNum %d", lora.irqDioNum);
#elif defined(IRQ_DIO_NUM)
    lora.irqDioNum = IRQ_DIO_NUM;
    LOG_DEBUG_RADIO("Set irqDioNum %d", lora.irqDioNum);
#elif defined(ARCH_PORTDUINO)
    // Unset keeps RadioLib's default of DIO5, which many carriers also drive as a switch line. The
    // range is checked again here because a DIO the radio cannot drive is a silently dead receiver.
    if (portduino_config.irq_dio_num < 0) {
        LOG_DEBUG_RADIO("Use default irqDioNum %d", lora.irqDioNum);
    } else if (portduino_config.irq_dio_num >= kLr20x0IrqDioMin && portduino_config.irq_dio_num <= kLr20x0IrqDioMax) {
        lora.irqDioNum = portduino_config.irq_dio_num;
        LOG_DEBUG_RADIO("Set irqDioNum %d from config", lora.irqDioNum);
    } else {
        LOG_WARN("Config irqDioNum %d outside DIO%d-DIO%d, using default irqDioNum %d", portduino_config.irq_dio_num,
                 kLr20x0IrqDioMin, kLr20x0IrqDioMax, lora.irqDioNum);
    }
#else
    LOG_DEBUG_RADIO("Use default irqDioNum %d", lora.irqDioNum);
#endif

    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) { // clamp if wide freq range
        limitPower(LR2021_MAX_POWER_HF);
    } else {
        limitPower(LR2021_MAX_POWER); // default clamp for non-wide freq range
    }

#ifdef LR2021_RF_SWITCH_SUBGHZ
    pinMode(LR2021_RF_SWITCH_SUBGHZ, OUTPUT);
    digitalWrite(LR2021_RF_SWITCH_SUBGHZ, isLr20x0HighBand(getFreq()) ? LOW : HIGH);
    LOG_DEBUG_RADIO("Set RF0 switch to %s", isLr20x0HighBand(getFreq()) ? "2.4GHz" : "SubGHz");
#endif

#ifdef LR2021_RF_SWITCH_2_4GHZ
    pinMode(LR2021_RF_SWITCH_2_4GHZ, OUTPUT);
    digitalWrite(LR2021_RF_SWITCH_2_4GHZ, isLr20x0HighBand(getFreq()) ? HIGH : LOW);
    LOG_DEBUG_RADIO("Set RF1 switch to %s", isLr20x0HighBand(getFreq()) ? "2.4GHz" : "SubGHz");
#endif

    // Allow extra time for TCXO to stabilize after power-on
    delay(10);

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);

    // Retry if we get SPI command failed - some units need extra TCXO stabilization time
    if (res == RADIOLIB_ERR_SPI_CMD_FAILED) {
        LOG_WARN("LR20x0 init failed with %d (SPI_CMD_FAILED), retry after delay", res);
        delay(100);
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
    }

    // If init failed for any reason other than chip not found, retry without TCXO (XTAL mode)
    if (TCXO_OPTIONAL_ENABLED && res != RADIOLIB_ERR_NONE && res != RADIOLIB_ERR_CHIP_NOT_FOUND && tcxoVoltage > 0) {
        LOG_WARN("LR20x0 init failed with TCXO Vref %f V (err %d), retry without TCXO", tcxoVoltage, res);
        tcxoVoltage = 0;
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        if (res == RADIOLIB_ERR_NONE)
            LOG_INFO("LR20x0 init success without TCXO (XTAL mode)");
    }

    // \todo Display actual typename of the adapter, not just `LR20x0`
    LOG_INFO("LR20x0 init result %d", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || res == RADIOLIB_ERR_SPI_CMD_FAILED)
        return false;

    if (res == RADIOLIB_ERR_NONE && tcxoVoltage > 0) {
        // Standby and TX/RX fallback on STDBY_XOSC keep the TCXO powered
        // to avoid the TCXO startup time on each TX/RX transition.
        const int16_t xoscRes = lora.setStandbyXOSC(true);
        LOG_DEBUG("Keep TCXO on in standby, result: %d", xoscRes);
    }

    // Some basic info about the module's explicit firmware version - no other info available
    // Currently requires radiolib godmode

#if RADIOLIB_GODMODE
    if (res == RADIOLIB_ERR_NONE) {
        uint8_t fwMajor = 0;
        uint8_t fwMinor = 0;
        int versionRes = lora.getVersion(&fwMajor, &fwMinor);
        if (versionRes == RADIOLIB_ERR_NONE)
            LOG_DEBUG_RADIO("LR20x0 FW %d.%d", fwMajor, fwMinor);
    }
#endif

    // Semtech DCDC sensitivity workaround for sub-GHz operation - applied here after lora.begin() has set the
    // packet type and modulation params. reconfigure() reapplies it after its own modulation changes.
    if (res == RADIOLIB_ERR_NONE)
        applyDcdcWorkaround();

    applyCustomLfPaTable(getFreq());

    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    // Standard DCDC ramp timing from RadioLib workarounds (register 0x00F20024)
    // Currently requires radiolib godmode
#if RADIOLIB_GODMODE
    if (res == RADIOLIB_ERR_NONE) {
        uint8_t rampTimes[4] = {15, 15, 15, 15}; // Standard case for all conditions
        // godmode-only DCDC ramp tuning: log failures but don't fail init (radio is already up)
        int16_t rmRes = lora.setRegMode(RADIOLIB_LR2021_REG_MODE_SIMO_NORMAL, rampTimes);
        if (rmRes != RADIOLIB_ERR_NONE)
            LOG_WARN("LR2021 setRegMode failed: %d", rmRes);
    }
#endif

#ifdef LR2021_DIO_AS_RF_SWITCH
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = portduino_config.has_rfswitch_table;
    if (dioAsRfSwitch)
        buildRfSwitchTable(lr20x0_rfswitch_dio_pins, lr20x0_rfswitch_table, RFSW_MODE_COUNT + 1, lr20x0_switch_dio_nums,
                           lr20x0_switch_dio_consts, sizeof(lr20x0_switch_dio_nums) / sizeof(lr20x0_switch_dio_nums[0]),
                           lr20x0_rfswitch_mode_map);
#else
    bool dioAsRfSwitch = false;
#endif

    if (dioAsRfSwitch) {
        lora.setRfSwitchTable(lr20x0_rfswitch_dio_pins, lr20x0_rfswitch_table);
        LOG_DEBUG_RADIO("Set DIO RF switch");
    }

    if (res == RADIOLIB_ERR_NONE) {
        if (config.lora.sx126x_rx_boosted_gain) { // the name is unfortunate but historically accurate
            res = lora.setRxBoostedGainMode(LR2021_RX_GAIN_BOOST_LEVEL);
            LOG_INFO("Set RX gain to boosted mode (level %d); result: %d", LR2021_RX_GAIN_BOOST_LEVEL, res);
        } else {
            res = lora.setRxBoostedGainMode(0);
            LOG_INFO("Set RX gain to power saving mode (boosted mode off); result: %d", res);
        }
    }

    if (res == RADIOLIB_ERR_NONE) {
        boundBusyWait();
        startReceive(); // start receiving
    }

    lr20x0LastFreqMHz = getFreq();
    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool LR20x0Interface<T>::reconfigure()
{
    // A readout between these calls would clear the flags they set up, or move the chip out from under them
    RadioSequence seq(this);
    // Propagated to the return value below, separately from the chip-programming outcome, so a
    // base-class failure isn't masked as success.
    const bool reconfigureSuccess = RadioLibInterface::reconfigure();

#ifdef MESHTASTIC_RX_READOUT_TASK
    // A reconfigure standbys the chip and owns the re-arm that follows, so an RX the readout task armed at TX_DONE is
    // void from here. Left set, adoptReceiveArmedFromIsr() would report armed for an RX that no longer exists and, via
    // the base startReceive() it calls, clear rxOffline and chipRecoveryFailures for a chip that never took one -
    // zeroing the ladder and the rxOffline that periodicRadioMaintenance() gates its retry on.
    rearmState = REARM_NONE;
#endif

    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) {
        limitPower(LR2021_MAX_POWER_HF);
    } else {
        limitPower(LR2021_MAX_POWER);
    }

    const float freq = getFreq();
    const bool bandHop = lr20x0ReconfigurePath(lr20x0LastFreqMHz, freq) == Lr20x0ReconfigurePath::FullBegin;

    if (bandHop) {
        LOG_INFO("LR20x0 LF/HF band hop %.1f -> %.1f MHz, full begin()", lr20x0LastFreqMHz, freq);
        // fullBegin() hardware-resets the chip, so a standby failure is survivable here
        (void)trySetStandby();

        if (!fullBegin(freq))
            return false;

        startReceive();
        return reconfigureSuccess;
    }

    // Same-band reconfigure (previous incremental path)
    bool standbySuccess = true;
    int16_t standbyErr = trySetStandby();
    if (standbyErr != RADIOLIB_ERR_NONE)
        standbySuccess = false;

    if (standbyErr == RADIOLIB_ERR_NONE) {
        int err = lora.setFrequency(freq);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setFrequency %.3f MHz %s%d", freq, radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        err = lora.setSpreadingFactor(sf);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setSpreadingFactor(%u) %s%d", sf, radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        err = lora.setBandwidth(bw);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setBandwidth(%.1f) %s%d", bw, radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        err = lora.setCodingRate(cr, cr != 7);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setCodingRate(%u) %s%d", cr, radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        err = lora.setSyncWord(syncWord);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setSyncWord %s%d", radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        err = lora.setPreambleLength(preambleLength);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setPreambleLength(%u) %s%d", preambleLength, radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        selectLfPaTable(freq);
        err = lora.setOutputPower(power);
        if (err != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 setOutputPower %d dBm @ %.3f MHz %s%d", power, freq, radioLibErr, err);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            standbySuccess = false;
        }

        // Warn-only, as in LR11x0: a rejected gain mode is cosmetic and not a lost-state signature, so
        // it must not drag reconfigure() into a full chip reset.
        err = lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain ? LR2021_RX_GAIN_BOOST_LEVEL : 0);
        if (err != RADIOLIB_ERR_NONE)
            LOG_WARN("LR20x0 setRxBoostedGainMode %s%d", radioLibErr, err);
    }

    if (!standbySuccess) {
        // A chip that fails standby or rejects parameter programming (typically WRONG_MODEM, -20) has
        // lost its runtime configuration to a chip-internal reset or brownout. Recover in place with the
        // same full begin() the band-hop path uses - it hardware-resets the chip. Crashing here instead
        // would reboot before MeshService persists the config change that triggered us.
        LOG_ERROR("LR20x0 rejected modem params, chip state lost? Full re-init");
        if (!fullBegin(freq)) {
            LOG_ERROR("LR20x0 unrecoverable, radio down until reboot");
            return false;
        }
        LOG_INFO("LR20x0 recovered after re-init");
    }

    // setSpreadingFactor/setBandwidth/setCodingRate each re-run setLoRaModulationParams(), which resets
    // the DCDC configure state, so reapply the workaround before we resume receiving.
    if (standbySuccess)
        applyDcdcWorkaround();

    startReceive();
    lr20x0LastFreqMHz = freq;
    return reconfigureSuccess;
}

// The chip-side re-init the band-hop and recovery paths share: front-end switch GPIOs for the target
// band, a fresh begin() (which hardware-resets the chip), CRC, DIO RF-switch table, and RX gain.
template <typename T> bool LR20x0Interface<T>::fullBegin(float freq)
{
    {
        // Match init(): external LF/HF front-end GPIOs (if board defines them).
#ifdef LR2021_RF_SWITCH_SUBGHZ
        pinMode(LR2021_RF_SWITCH_SUBGHZ, OUTPUT);
        digitalWrite(LR2021_RF_SWITCH_SUBGHZ, isLr20x0HighBand(freq) ? LOW : HIGH);
        LOG_DEBUG_RADIO("Set RF0 switch to %s", isLr20x0HighBand(freq) ? "2.4GHz" : "SubGHz");
#endif
#ifdef LR2021_RF_SWITCH_2_4GHZ
        pinMode(LR2021_RF_SWITCH_2_4GHZ, OUTPUT);
        digitalWrite(LR2021_RF_SWITCH_2_4GHZ, isLr20x0HighBand(freq) ? HIGH : LOW);
        LOG_DEBUG_RADIO("Set RF1 switch to %s", isLr20x0HighBand(freq) ? "2.4GHz" : "SubGHz");
#endif

#if ARCH_PORTDUINO
        float tcxoVoltage;
        if (portduino_config.dio3_tcxo_voltage > 0)
            tcxoVoltage = (float)portduino_config.dio3_tcxo_voltage / 1000;
        else if (TCXO_OPTIONAL_ENABLED)
            tcxoVoltage = TCXO_OPTIONAL_DEFAULT_VOLTAGE;
        else
            tcxoVoltage = 0;
#elif defined(LR2021_DIO3_TCXO_VOLTAGE)
        float tcxoVoltage = LR2021_DIO3_TCXO_VOLTAGE;
#elif defined(TCXO_OPTIONAL)
        float tcxoVoltage = TCXO_OPTIONAL_DEFAULT_VOLTAGE;
#else
        float tcxoVoltage = 0;
#endif

        delay(10); // same TCXO settle window as init()

        rxArmedContinuous = false; // begin() resets the chip
        int res = lora.begin(freq, bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        if (res == RADIOLIB_ERR_SPI_CMD_FAILED) {
            LOG_WARN("LR20x0 band-hop begin SPI_CMD_FAILED, retrying");
            delay(100);
            res = lora.begin(freq, bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        }
        if (TCXO_OPTIONAL_ENABLED && res != RADIOLIB_ERR_NONE && res != RADIOLIB_ERR_CHIP_NOT_FOUND && tcxoVoltage > 0) {
            LOG_WARN("LR20x0 band-hop begin TCXO failed (%s%d), retry without TCXO", radioLibErr, res);
            tcxoVoltage = 0;
            res = lora.begin(freq, bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
        }
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 band-hop begin %s%d", radioLibErr, res);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            return false;
        }

        applyCustomLfPaTable(freq);

        lr20x0LastFreqMHz = freq;

        res = lora.setCRC(2);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 band-hop setCRC %s%d", radioLibErr, res);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            return false;
        }

#ifdef LR2021_DIO_AS_RF_SWITCH
        lora.setRfSwitchTable(lr20x0_rfswitch_dio_pins, lr20x0_rfswitch_table);
#elif ARCH_PORTDUINO
        if (portduino_config.has_rfswitch_table)
            lora.setRfSwitchTable(lr20x0_rfswitch_dio_pins, lr20x0_rfswitch_table);
#endif

        res = lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain ? LR2021_RX_GAIN_BOOST_LEVEL : 0);
        if (res != RADIOLIB_ERR_NONE) {
            LOG_ERROR("LR20x0 band-hop setRxBoostedGainMode %s%d", radioLibErr, res);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);
            return false;
        }

        // begin() above reprogrammed the modulation params, so the DCDC configure state is reset here too.
        applyDcdcWorkaround();

        return true;
    }
}

// Board LF PA table after begin(); pointer is retained. HF keeps the RadioLib default.
// Warn-only: a calibration miss must not fail init/fullBegin, keep the begin() PA config.
template <typename T> void LR20x0Interface<T>::applyCustomLfPaTable(float freq)
{
#ifdef LR2021_CUSTOM_PA_TABLE
    if (isLr20x0HighBand(freq))
        return;
    selectLfPaTable(freq);
    int16_t paRes = lora.setOutputPower(power);
    if (paRes != RADIOLIB_ERR_NONE)
        LOG_WARN("LR2021 LF PA table setOutputPower failed (%s%d)", radioLibErr, paRes);
    else
        LOG_DEBUG_RADIO("LR2021 %s LF PA table at %.3f MHz", isLr20x0CustomLfPaBand(freq) ? "custom" : "default", freq);
#else
    (void)freq;
#endif
}

// RadioLib retains the pointer, so a same-band retune out of the calibrated range must clear it.
template <typename T> void LR20x0Interface<T>::selectLfPaTable(float freq)
{
#ifdef LR2021_CUSTOM_PA_TABLE
    if (!isLr20x0HighBand(freq))
        lora.setPaTable(isLr20x0CustomLfPaBand(freq) ? lr2021_pa_table_lf : nullptr, false);
#else
    (void)freq;
#endif
}

// Semtech DCDC sensitivity workaround for sub-GHz operation on engineering sample date code 2513.
// Worthy of note is that we tested this on non-engineering samples and it didn't make any difference, but
// we went to the trouble of writing this, so it can stay in, albeit gated behind a compile-time option.
// lr20xx_workarounds_dcdc_reset must follow setPacketType; lr20xx_workarounds_dcdc_configure must follow
// setModulationParams. In init() both hold once lora.begin() returns; in reconfigure() the caller invokes this
// after setSpreadingFactor/setBandwidth/setCodingRate, whose RadioLib implementations re-run
// setLoRaModulationParams() and reset the DCDC configure state. Only applies to sub-GHz; 2.4 GHz (LORA_24) is
// excluded. Opt-in only: requires -DLR2021_DCDC_WORKAROUND (and RADIOLIB_GODMODE for the internal register access).
template <typename T> void LR20x0Interface<T>::applyDcdcWorkaround()
{
#if defined(LR2021_DCDC_WORKAROUND) && RADIOLIB_GODMODE
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24)
        return;

    // Helper: set DCDC LF frequency register and re-apply the current RF frequency.
    auto dcdcSetFreq = [&](uint32_t freqHz) -> int16_t {
        const uint32_t freqLf = (uint32_t)((float)freqHz * 1.048576f);
        int16_t s = lora.writeRegMem32(RADIOLIB_LR2021_REG_DCDC_FREQ_LF, &freqLf, 1);
        if (s != RADIOLIB_ERR_NONE)
            return s;
        uint32_t rawRfFreq = 0;
        s = lora.readRegMem32(RADIOLIB_LR2021_REG_RTTOF_RF_FREQ, &rawRfFreq, 1);
        if (s != RADIOLIB_ERR_NONE)
            return s;
        // Convert PLL steps to Hz: (steps * 15625 + 16383) / 16384
        uint32_t rfHz = (uint32_t)(((uint64_t)rawRfFreq * 15625ULL + 16383ULL) / 16384ULL);
        return lora.setRfFrequency(rfHz);
    };

    // dcdc_reset: reset RISE/FALL ramp fields to conservative 15/15 at 2.8 MHz
    int16_t dcdcRes = lora.writeRegMemMask32(RADIOLIB_LR2021_REG_DCDC_SWITCHER, 0xFu << 20, 15u << 20);
    if (dcdcRes == RADIOLIB_ERR_NONE)
        dcdcRes = lora.writeRegMemMask32(RADIOLIB_LR2021_REG_DCDC_SWITCHER, 0xFu << 16, 15u << 16);
    if (dcdcRes == RADIOLIB_ERR_NONE)
        dcdcRes = dcdcSetFreq(2800000);

    // dcdc_configure: tune RISE/FALL and DC freq based on ADC decimation and RX path.
    // Matches lr20xx_workarounds_dcdc_configure() exactly.
    if (dcdcRes == RADIOLIB_ERR_NONE) {
        uint32_t adcCtrl = 0, rxPath = 0;
        dcdcRes = lora.readRegMem32(RADIOLIB_LR2021_REG_DCDC_ADC_CTRL, &adcCtrl, 1);
        if (dcdcRes == RADIOLIB_ERR_NONE)
            dcdcRes = lora.readRegMem32(RADIOLIB_LR2021_REG_DCDC_RX_PATH, &rxPath, 1);
        if (dcdcRes == RADIOLIB_ERR_NONE) {
            const uint32_t anaDec = (adcCtrl >> 8) & 0x7;
            const bool isRxHf = (rxPath & 0x3) == 1;
            // Narrowband sub-GHz path (ana_dec 1 or 2): use tighter RISE=11/FALL=13 timing
            const uint32_t rise = (!isRxHf && (anaDec == 1 || anaDec == 2)) ? 11u : 15u;
            const uint32_t fall = (!isRxHf && (anaDec == 1 || anaDec == 2)) ? 13u : 15u;
            dcdcRes = lora.writeRegMemMask32(RADIOLIB_LR2021_REG_DCDC_SWITCHER, 0xFu << 20, rise << 20);
            if (dcdcRes == RADIOLIB_ERR_NONE)
                dcdcRes = lora.writeRegMemMask32(RADIOLIB_LR2021_REG_DCDC_SWITCHER, 0xFu << 16, fall << 16);
            if (dcdcRes == RADIOLIB_ERR_NONE)
                dcdcRes = dcdcSetFreq(anaDec == 1 ? 4300000 : 2800000);
        }
    }
    if (dcdcRes != RADIOLIB_ERR_NONE)
        LOG_WARN("LR20x0 DCDC workaround failed: %d", dcdcRes);
    else
        LOG_DEBUG_RADIO("LR20x0 DCDC workaround applied");
#endif
}

template <typename T> void LR20x0Interface<T>::clearRadioIsr()
{
    lora.clearIrqAction();
}

template <typename T> int16_t LR20x0Interface<T>::trySetStandby()
{
    // Handle any pending interrupts before we force standby, but not a TX whose backoff timer is pending: it would start
    // here, before its delay is up, and the standby below would cut it off with its payload still in the radio
    checkNotificationExcept(TRANSMIT_DELAY_COMPLETED);

    int16_t err = lora.standby();

    if (err != RADIOLIB_ERR_NONE) {
        LOG_DEBUG_RADIO("LR20x0 standby failed, err %d", err);
    }

    isReceiving = false; // If we were receiving, not any more
    activeReceiveStart = 0;
    rxArmedContinuous = false;
    disableInterruptTimed();
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
    return err;
}

template <typename T> void LR20x0Interface<T>::setStandby()
{
    int16_t err = trySetStandby();
    assert(err == RADIOLIB_ERR_NONE);
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
    rxArmedContinuous = false; // the transmission takes the chip out of RX
#ifdef MESHTASTIC_RX_READOUT_TASK
    rearmState = REARM_NONE; // only this TX's TX_DONE may re-arm, never a stale one from a TX the poll completed
    rxArmedBeforeTxDone = false;
#endif
    RadioLibInterface::configHardwareForSend();
}

template <typename T> int16_t LR20x0Interface<T>::launchTransmit(size_t numbytes)
{
#if LR2021_CAD_LBT
    const bool onAir = lbtSentId && sendingPacket && sendingPacket->id == lbtSentId && lbtSentLen == numbytes;
    lbtSentId = 0;
    lbtSentLen = 0;
    if (onAir) {
        // The scan started this TX, so TX_DONE can come before startSend() arms the ISR. Arm it here and catch a
        // TX_DONE that already raised the pin: the ISR is edge-triggered and would never see it.
        enableInterrupt(isrTxLevel0);
        if (module.hal->digitalRead(module.getIrq()))
            deliverPendingIrqFromPoll(ISR_TX);
        return RADIOLIB_ERR_NONE;
    }
#endif
    // RadioLib's TX staging appends to the TX FIFO and never empties it, so bytes a cut-short TX left there would go out
    // ahead of this packet, at this packet's length, and push every later packet out of line by the same amount.
    uint16_t level = 0;
    const bool levelRead = readTxFifoLevel(level);
    if (level)
        LOG_WARN("LR20x0 TX FIFO held %u bytes from an earlier TX, cleared", (unsigned)level);
    if (!levelRead || level)
        module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_CLEAR_TX_FIFO, NULL, 0, true, true); // private in RadioLib
    return RadioLibInterface::launchTransmit(numbytes);
}

template <typename T> void LR20x0Interface<T>::discardUnreadRxFrame()
{
    // The FIFO appends, so a frame left in it is read out as the start of the next one, which then fails to decode
    module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_CLEAR_RX_FIFO, NULL, 0, true, true); // private in RadioLib
}

template <typename T> bool LR20x0Interface<T>::readTxFifoLevel(uint16_t &level)
{
    // RadioLib's getTxFifoLevel() is private, so read it as RadioLib does: the command, then the reply in a second transfer
    // with no command in front. The width swap is safe for the reason readChipMode() gives.
    uint8_t buff[2] = {0};
    if (module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_GET_TX_FIFO_LEVEL, NULL, 0, true, false) != RADIOLIB_ERR_NONE)
        return false;
    const Module::BitWidth_t width = module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD];
    module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_0;
    const int16_t res = module.SPIreadStream((uint16_t)RADIOLIB_LR2021_CMD_NOP, buff, sizeof(buff), true, false);
    module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = width;
    if (res != RADIOLIB_ERR_NONE)
        return false;
    level = ((uint16_t)buff[0] << 8) | buff[1];
    return true;
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void LR20x0Interface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else
    postTxChecks();

    int16_t err = trySetStandby();

    if (err == RADIOLIB_ERR_NONE) {
        lora.setPreambleLength(preambleLength); // Solve RX ack fail after direct message sent.  Not sure why this is needed.

        // We use a 16 bit preamble so this should save some power by letting radio sit in standby mostly.
        err =
            lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    if (err != RADIOLIB_ERR_NONE) {
        LOG_ERROR("StartReceive error: %d", err);
        if (maybeRecoverChipStateLoss()) {
            lora.setPreambleLength(preambleLength);
            err = lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS,
                                    RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
        }
    }

    if (err != RADIOLIB_ERR_NONE) {
        // No assert: leave RX off rather than reboot; periodicRadioMaintenance() re-arms it, throttled
        LOG_ERROR("LR20x0 RX offline %s%d", radioLibErr, err);
        rxOffline = true;
        return;
    }

    RadioLibInterface::startReceive();
    rxArmedContinuous = true; // RADIOLIB_LR2021_RX_TIMEOUT_INF: continuous

    // Must be done AFTER starting receive, because startReceive clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
#endif
}

template <typename T> void LR20x0Interface<T>::postTxChecks()
{
#if RADIO_TXDONE_MODE_CHECK
    if (!takeTxModeCheck())
        return;
    txDoneMode = readChipMode();
    // GetErrors and ClearErrors are not public in RadioLib: send them as LRxxxx::SPIcommand() does, the read in two
    // transactions. Errors latch, so a sampled read still counts each one since the last.
    uint16_t errs = 0;
    uint8_t eb[2] = {0};
    if (module.SPIwriteStream(RADIOLIB_LR2021_CMD_GET_ERRORS, NULL, 0, true, false) == RADIOLIB_ERR_NONE) {
        module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_0;
        const int16_t st = module.SPIreadStream(RADIOLIB_LRXXXX_CMD_NOP, eb, sizeof(eb), true, false);
        module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_16;
        if (st == RADIOLIB_ERR_NONE)
            errs = ((uint16_t)eb[0] << 8) | eb[1];
    }
    if (errs)
        (void)module.SPIwriteStream(RADIOLIB_LR2021_CMD_CLEAR_ERRORS, NULL, 0, true, true);
    txDoneErrs = errs;
    txDoneModeAfter = 0xFF;
    txModeLogDue = true;
#endif
}

template <typename T> uint8_t LR20x0Interface<T>::readChipMode()
{
    // Any NOP transfer returns stat1 and stat2 first, but SPItransferStream() drops the configured 16-bit status from the
    // front, so read them as data with the width cleared for the transfer, as RadioLib's own status reads do. stat2 bits
    // 2..0 are the mode. FS is the chip on its way to TX or RX, so look again for up to 1 ms.
    //
    // spiConfig belongs to the Module, so the swap below would be visible to anything else talking to this radio: every
    // caller of this is inside a RadioSequence, which is also what the readout task takes before it touches the chip, so
    // nothing else can be mid-transfer here. RadioLib's own LRxxxx::getStatus() would make the swap unnecessary, but it
    // is protected, and reads the reply at the wrong offset until jgromes/RadioLib fixes it.
    uint8_t buff[2] = {0};
    uint8_t mode = 0xFF;
    for (int tries = 0; tries < 10; tries++) {
        const Module::BitWidth_t width = module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS];
        module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS] = Module::BITS_0;
        const int16_t res = module.SPItransferStream(NULL, 0, false, NULL, buff, sizeof(buff), true);
        module.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS] = width;
        if (res != RADIOLIB_ERR_NONE)
            return 0xFF;
        mode = buff[1] & 0x07;
        if (mode != LR20X0_CHIP_MODE_FS)
            break;
        delayMicroseconds(100);
    }
    return mode;
}

template <typename T> bool LR20x0Interface<T>::resumeRunningReceive()
{
    // A continuous RX keeps listening after RX_DONE and after CRC or header errors, so pick it back up instead of a standby
    // and restart. Checked on the chip, as the LR2021 datasheet does not say it for every error.
    if (!rxArmedContinuous)
        return false;
    const uint8_t mode = readChipMode();
    if (mode != LR20X0_CHIP_MODE_RX) {
        LOG_WARN("LR20x0 RX not running after a frame (chip mode %u), restarting it", (unsigned)mode);
        rxArmedContinuous = false;
        return false;
    }
    // No flag clearing, as in SX126xInterface::resumeRunningReceive(): a latched RX_DONE here is a next frame
    activeReceiveStart = 0; // the frame it timed is done; a preamble now is the next one
    RadioLibInterface::startReceive();
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag(); // an RX_DONE that beat the resume
    return true;
}

#ifdef MESHTASTIC_RX_READOUT_TASK
template <typename T> bool INTERRUPT_ATTR LR20x0Interface<T>::rearmReceiveFromIsr()
{
    // The interrupt cannot call RadioLib, so the readout task, above the main loop, re-arms as soon as it returns.
    rearmState = REARM_PENDING;
    if (requestRearmFromIsr())
        return true;
    rearmState = REARM_NONE;
    return false;
}

template <typename T> void LR20x0Interface<T>::rearmReceiveFromTask()
{
    // Outside any radio-thread sequence. If the thread got there first, it took the re-arm over and left nothing to do.
    RadioSequence seq(this);
    if (rearmState != REARM_PENDING) {
        edgeRearmMark(EDGE_REARM_LATE);
        return;
    }
    edgeRearmMark(EDGE_REARM_LOCK);
    postTxChecks();
#if RADIO_SLIM_REARM
    const int16_t err = slimRearmRx();
#else
    // What startReceive() sends, less the standby: after TX_DONE the chip has already fallen back to standby.
    int16_t err = lora.setPreambleLength(preambleLength);
    if (err == RADIOLIB_ERR_NONE)
        err =
            lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
#endif
    rearmErr = err;
    if (err != RADIOLIB_ERR_NONE) {
        rearmState = REARM_FAILED;
        edgeRearmMark(EDGE_REARM_ARMED);
        return;
    }
    rearmState = REARM_ARMED;
    rxArmedContinuous = true; // so a frame the task reads before the thread adopts finds the chip still listening
    // The TX_DONE interrupt detached the radio's interrupt. Attach the RX one now, so a frame that ends before the radio
    // thread runs wakes this task to read it, rather than waiting in the chip for the next frame to overwrite it.
    rxArmedBeforeTxDone = true;
    enableInterrupt(isrRxLevel0);
    edgeRearmMark(EDGE_REARM_ARMED);
}

#if RADIO_SLIM_REARM
template <typename T> int16_t LR20x0Interface<T>::slimRearmRx()
{
    // After TX_DONE the chip is in its standby fallback, and TX changed only the IRQ routing and the payload length. The
    // RadioLib calls this replaces also send two GetPacketType reads (two transactions each), the packet params twice,
    // and SetRxPath, which re-runs the DC-DC workaround (a register read in two transactions, three writes and
    // SetRfFrequency): 16 transactions against 5. RadioLib changes neither the RX path nor the DC-DC registers on TX.
    //     int16_t err = lora.setPreambleLength(preambleLength);
    //     if (err == RADIOLIB_ERR_NONE)
    //         err = lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS,
    //                                 RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    const uint32_t irq = lora.getIrqMapped(MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS & RADIOLIB_IRQ_RX_DEFAULT_MASK);
    const uint8_t dioIrq[5] = {(uint8_t)lora.irqDioNum, (uint8_t)(irq >> 24), (uint8_t)(irq >> 16), (uint8_t)(irq >> 8),
                               (uint8_t)irq};
    int16_t res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_DIO_IRQ_CONFIG, dioIrq, sizeof(dioIrq));
    const uint8_t clearAll[4] = {(uint8_t)(RADIOLIB_LR2021_IRQ_ALL >> 24), (uint8_t)(RADIOLIB_LR2021_IRQ_ALL >> 16),
                                 (uint8_t)(RADIOLIB_LR2021_IRQ_ALL >> 8), (uint8_t)RADIOLIB_LR2021_IRQ_ALL};
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_CLEAR_IRQ, clearAll, sizeof(clearAll));
    // Fixed for Meshtastic: explicit header, CRC on, IQ standard; the RX payload length is the maximum, as RadioLib's
    const uint8_t packetParams[4] = {
        (uint8_t)(preambleLength >> 8), (uint8_t)(preambleLength & 0xFF), (uint8_t)RADIOLIB_LR2021_MAX_PACKET_LENGTH,
        (uint8_t)(((RADIOLIB_LRXXXX_LORA_HEADER_EXPLICIT & 0x01) << 2) | ((RADIOLIB_LRXXXX_LORA_CRC_ENABLED & 0x01) << 1) |
                  (RADIOLIB_LR2021_LORA_IQ_STANDARD & 0x01))};
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_LORA_PACKET_PARAMS, packetParams, sizeof(packetParams));
    module.setRfSwitchState(Module::MODE_RX);
    const uint8_t rxTimeout[3] = {0xFF, 0xFF, 0xFF}; // continuous, as RADIOLIB_LR2021_RX_TIMEOUT_INF
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_RX, rxTimeout, sizeof(rxTimeout));
    // A refused SetRx would show only in the next transfer, and the chip would sit deaf in standby until then
    if (res == RADIOLIB_ERR_NONE && module.spiConfig.checkStatusCb)
        res = module.spiConfig.checkStatusCb(&module);
    return res;
}
#endif

template <typename T> bool LR20x0Interface<T>::adoptReceiveArmedFromIsr()
{
    // Called inside the post-TX sequence, so the task cannot be mid re-arm: it has either finished or not started, and
    // clearing the state here stops it starting.
    const uint8_t state = rearmState;
    rearmState = REARM_NONE;
    rxArmedBeforeTxDone = false; // this is the TX_DONE; frames the task read meanwhile were delivered ahead of it
    if (state == REARM_FAILED)
        LOG_WARN("LR20x0 RX re-arm at TX_DONE failed %s%d, restarting RX", radioLibErr, rearmErr);
    if (state != REARM_ARMED)
        return false;
    RadioLibInterface::startReceive();
    rxArmedContinuous = true; // the task armed a continuous RX
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag(); // an RX_DONE that completed before the interrupt was attached
    return true;
}
#endif

/** Is the channel currently active? */
template <typename T> bool LR20x0Interface<T>::isChannelActive()
{
    // check if we can detect a LoRa preamble on the current channel.
    // symNum is SetLoraCadParams nb_symbols - a plain count - so take it straight from
    // getCadSymbolCount(), which follows the band (8 on 2.4 GHz, as SX1280 scans) and sizes the CW slot.
    // det_peak is the peak-to-average ratio threshold: a higher value demands a stronger correlation
    // peak and so detects less readily, which is why the recommended value falls as the scan lengthens.
    // Indexed by that same count, so the threshold tracks the scan instead of drifting out of step.
    // Table 6-19 stops at 4 symbols, so a longer scan reuses that row - the last measured point, and
    // erring high, i.e. slightly less sensitive than a true 8-symbol value would be.
    static constexpr uint8_t CAD_DET_PEAK[4][8] = {
        // LR20xx datasheet rev 2.2, Table 6-19 - recommended det_peak, SF5..SF12
        {60, 60, 60, 64, 64, 66, 70, 74}, // 1 symbol
        {56, 56, 56, 58, 58, 60, 64, 68}, // 2 symbols (RadioLib's default row)
        {51, 51, 52, 54, 56, 60, 60, 65}, // 3 symbols
        {51, 51, 51, 54, 56, 60, 60, 64}, // 4 symbols
    };
    const uint8_t symNum = getCadSymbolCount();
    const uint8_t detPeak = CAD_DET_PEAK[(symNum >= 1 && symNum <= 4) ? symNum - 1 : 3]
                                        [(sf >= 5 && sf <= 12) ? sf - 5 : 6]; // out of range: 4 symbols, SF11
    // Times the RX that follows a detection. lr20xx_driver calls this field PLL steps of 31.25 us (the
    // datasheet's "32 MHz periods" disagrees); RadioLib scales it as 30.52, so we land ~2% long.
    const RadioLibTime_t cadRxTimeoutUsec =
        (RadioLibTime_t)getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN + sizeof(PacketHeader), false) * 1000;
    // SetLoraCadParams exit mode: 0x00 CAD only, 0x01 RX on detection, 0x10 TX when clear, as on LR11x0. RadioLib's
    // RADIOLIB_LR2021_CAD_EXIT_MODE_RX is 0x02, which the chip refuses with a processing error (-706), so no scan runs.
    static constexpr uint8_t CAD_EXIT_MODE_RX = 0x01;
    ChannelScanConfig_t cfg = {.cad = {.symNum = symNum,
                                       .detPeak = detPeak,
                                       // ignored: SetLoraCadParams has no det_min - that byte carries
                                       // pnr_delta, which scanChannel() takes from lora.fastCad below
                                       .detMin = RADIOLIB_LR2021_CAD_PARAM_DEFAULT,
                                       .exitMode = CAD_EXIT_MODE_RX,
                                       .timeout = cadRxTimeoutUsec,
                                       // DS rev 2.2 6.8.3: only routes IRQs to a pin, so keep
                                       // preamble/header off it - they would fire the ISR mid-frame
                                       .irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_RX_DONE) |
                                                   (1UL << RADIOLIB_IRQ_TIMEOUT) | (1UL << RADIOLIB_IRQ_CRC_ERR) |
                                                   (1UL << RADIOLIB_IRQ_HEADER_ERR),
                                       .irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK}}; // ignored: startChannelScan() drops it
    // fastCad is not in ChannelScanConfig_t - scanChannel() reads it off the radio object - so pin it.
    // false is pnr_delta 0: the scan runs the full nb_symbols, which is what the slot time assumes.
    lora.fastCad = false;
#if LR2021_CAD_LBT
    lbtSentId = 0;
    lbtSentLen = 0;
    // Only for a packet startSend() will go on to send: nothing after a clear LBT scan can stop it
    if (scanForTx && !disabled && config.lora.tx_enabled)
        return scanThenTransmit(cfg);
#endif

    int16_t result = trySetStandby();
    if (result == RADIOLIB_ERR_NONE) {
        result = lora.scanChannel(cfg);
        if (result == RADIOLIB_LORA_DETECTED) {
            // The chip auto-entered RX. Drop the latched CAD verdict so the pin releases and the coming
            // RX_DONE is a clean edge.
            lora.clearIrqFlags(RADIOLIB_LR2021_IRQ_CAD_DONE | RADIOLIB_LR2021_IRQ_CAD_DETECTED);
            noteCadHandoffToRx(); // nothing below arms the radio; the caller's rearmReceive() adopts it
            return true;
        }
        if (result != RADIOLIB_CHANNEL_FREE && result != RADIOLIB_ERR_WRONG_MODEM)
            LOG_WARN("LR20x0 channel scan failed %s%d, reported clear", radioLibErr, result);
        if (result != RADIOLIB_ERR_WRONG_MODEM)
            return false;
    }

    // standby failed or the LoRa modem type is gone - the chip lost its runtime state
    maybeRecoverChipStateLoss();
    return false; // report the channel free: a recovered chip can TX, a dead one fails startSend safely
}

#if LR2021_CAD_LBT
template <typename T> bool LR20x0Interface<T>::scanThenTransmit(ChannelScanConfig_t &cfg)
{
    cfg.cad.exitMode = RADIOLIB_LR2021_LORA_CAD_EXIT_MODE_LBT;
    cfg.cad.timeout = 0; // with LBT this is the TX timeout: none, as SetTx
    cfg.cad.irqFlags = cfg.cad.irqMask =
        RADIOLIB_IRQ_CAD_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_TX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT);
    if (trySetStandby() != RADIOLIB_ERR_NONE) {
        maybeRecoverChipStateLoss(); // as isChannelActive()
        return false;
    }
    // After the standby: it can deliver a pending RX, which reads that frame into radioBuffer
    const size_t numbytes = encodeRadioBuffer(scanForTx); // CPU only
    // The RadioLib calls startLbtScan() replaces, which send the same commands plus the reads it skips:
    //     int16_t result = lora.writeTxBuffer((const uint8_t *)&radioBuffer, numbytes);
    //     RadioModeConfig_t txCfg = {};
    //     txCfg.transmit.len = numbytes; // data NULL: written above
    //     if (result == RADIOLIB_ERR_NONE)
    //         result = lora.stageMode(RADIOLIB_RADIO_MODE_TX, &txCfg);
    //     if (result == RADIOLIB_ERR_NONE)
    //         result = lora.startChannelScan(cfg);
    int16_t result = startLbtScan(cfg, numbytes);
    if (result == RADIOLIB_ERR_NONE) {
        // As scanChannel(): wait for the CAD to finish, then read the verdict
        while (!module.hal->digitalRead(module.getIrq()))
            module.hal->yield();
        const uint32_t irq = lora.getIrqStatus();
        if (irq & RADIOLIB_LR2021_IRQ_CAD_DETECTED) {
            // The chip went back to standby; the caller's rearmReceive() restarts RX
            lora.clearIrqFlags(RADIOLIB_LR2021_IRQ_ALL);
            return true;
        }
        if (irq & (RADIOLIB_LR2021_IRQ_CAD_DONE | RADIOLIB_LR2021_IRQ_TX_DONE)) {
            // On air already. Drop the CAD flags so the pin falls and TX_DONE is a fresh edge for the ISR
            lora.clearIrqFlags(RADIOLIB_LR2021_IRQ_CAD_DONE | RADIOLIB_LR2021_IRQ_CAD_DETECTED);
            lbtSentId = scanForTx->id;
            lbtSentLen = numbytes;
            return false;
        }
        LOG_WARN("LR20x0 LBT scan ended with IRQ 0x%08lx, sending with SetTx", (unsigned long)irq);
        return false;
    }
    // Free either way, as isChannelActive(): startSend() sends with SetTx, or fails safely on a dead chip. A refused
    // command here is what RadioLib's packet-type check would have caught as WRONG_MODEM: a chip that lost its state.
    LOG_ERROR("LR20x0 LBT scan %s%d", radioLibErr, result);
    maybeRecoverChipStateLoss();
    return false;
}

template <typename T> int16_t LR20x0Interface<T>::startLbtScan(const ChannelScanConfig_t &cfg, size_t numbytes)
{
    // RadioLib's writeTxBuffer(), stageMode(TX) and startChannelScan() send these seven commands, plus three
    // GetPacketType reads (two SPI transactions each), a second SetDioIrqConfig and ClearIrq, and a standby (NOP and
    // SetStandby) the caller has just done: 17 transactions against 7 here. Each one costs host time whatever its length
    // (lock, transfer, BUSY wait); on the LR1110 the same cut took the ThinkNode M9's scan setup from 3.9 to 2.1 ms. The
    // packet-type check those reads made is kept by the status check below: a chip that lost its LoRa state refuses
    // these commands.
    // The TX FIFO appends, so empty it first, as writeTxBuffer() does.
    int16_t res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_CLEAR_TX_FIFO, NULL, 0);
    // Fixed for Meshtastic: LoRa, explicit header, CRC on, IQ standard, as the radio setup sets them.
    const uint8_t packetParams[4] = {(uint8_t)(preambleLength >> 8), (uint8_t)(preambleLength & 0xFF), (uint8_t)numbytes,
                                     (uint8_t)(((RADIOLIB_LRXXXX_LORA_HEADER_EXPLICIT & 0x01) << 2) |
                                               ((RADIOLIB_LRXXXX_LORA_CRC_ENABLED & 0x01) << 1) |
                                               (RADIOLIB_LR2021_LORA_IQ_STANDARD & 0x01))};
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_LORA_PACKET_PARAMS, packetParams, sizeof(packetParams));
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_WRITE_TX_FIFO, (const uint8_t *)&radioBuffer, numbytes);
    module.setRfSwitchState(Module::MODE_RX);
    const uint32_t irq = lora.getIrqMapped(cfg.cad.irqFlags);
    const uint8_t dioIrq[5] = {(uint8_t)lora.irqDioNum, (uint8_t)(irq >> 24), (uint8_t)(irq >> 16), (uint8_t)(irq >> 8),
                               (uint8_t)irq};
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_DIO_IRQ_CONFIG, dioIrq, sizeof(dioIrq));
    const uint8_t clearAll[4] = {(uint8_t)(RADIOLIB_LR2021_IRQ_ALL >> 24), (uint8_t)(RADIOLIB_LR2021_IRQ_ALL >> 16),
                                 (uint8_t)(RADIOLIB_LR2021_IRQ_ALL >> 8), (uint8_t)RADIOLIB_LR2021_IRQ_ALL};
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_CLEAR_IRQ, clearAll, sizeof(clearAll));
    // startCad()'s defaults for any parameter left at RADIOLIB_LR2021_CAD_PARAM_DEFAULT; preamble-only off, as there
    static const uint8_t detPeakBySf[8] = {56, 56, 56, 58, 58, 60, 64, 68};
    const uint32_t timeoutRaw = (uint32_t)((float)cfg.cad.timeout / 30.52f);
    const uint8_t pnrDelta = lora.fastCad ? RADIOLIB_LR2021_LORA_CAD_PNR_DELTA_FAST : RADIOLIB_LR2021_LORA_CAD_PNR_DELTA_STANDARD;
    const uint8_t detPeak =
        cfg.cad.detPeak != RADIOLIB_LR2021_CAD_PARAM_DEFAULT ? cfg.cad.detPeak : detPeakBySf[(sf >= 5 && sf <= 12) ? sf - 5 : 4];
    const uint8_t cadParams[7] = {cfg.cad.symNum != RADIOLIB_LR2021_CAD_PARAM_DEFAULT ? cfg.cad.symNum : (uint8_t)2,
                                  (uint8_t)(pnrDelta & 0x0F),
                                  cfg.cad.exitMode,
                                  (uint8_t)(timeoutRaw >> 16),
                                  (uint8_t)(timeoutRaw >> 8),
                                  (uint8_t)timeoutRaw,
                                  (uint8_t)(detPeak & 0x7F)};
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_LORA_CAD_PARAMS, cadParams, sizeof(cadParams));
    if (res == RADIOLIB_ERR_NONE)
        res = module.SPIwriteStream((uint16_t)RADIOLIB_LR2021_CMD_SET_LORA_CAD, NULL, 0);
    // A refused SetLoraCad would show only in the next transfer, and the caller would wait on an IRQ that never comes
    if (res == RADIOLIB_ERR_NONE && module.spiConfig.checkStatusCb)
        res = module.spiConfig.checkStatusCb(&module);
    return res;
}
#endif

/** Could we send right now (i.e. either not actively receiving or transmitting)? */
template <typename T> bool LR20x0Interface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation. Check if we've started a header, but haven't yet
    // received and handled the interrupt for reading the packet/handling errors.
    return receiveDetected(lora.getIrqStatus(), RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID, RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED);
}

#ifdef LR20X0_AGC_RESET
template <typename T> bool LR20x0Interface<T>::resetAGC()
{
    // Safety: don't reset mid-packet
    if (sendingPacket != NULL || (isReceiving && isActivelyReceiving()))
        return false;

    LOG_DEBUG_RADIO("LR20x0 AGC reset: warm sleep + Calibrate(0x3F)");

    // 1. Warm sleep - powers down the analog frontend, resetting AGC state
    rxArmedContinuous = false; // the warm sleep stops RX
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
    lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain ? LR2021_RX_GAIN_BOOST_LEVEL : 0);

    // 6. Resume receiving
    startReceive();
    return true;
}
#endif

template <typename T> bool LR20x0Interface<T>::sleep()
{
    // \todo Display actual typename of the adapter, not just `LR20x0`
    LOG_DEBUG_RADIO("LR20x0 entering sleep mode");
    // trySetStandby() and the sleep command below are one sequence: a readout landing between them talks to a chip
    // that is on its way down, and the standby would be undone by the arm a readout's caller can follow it with.
    RadioSequence seq(this);
    (void)trySetStandby(); // Stop any pending operations - the chip is being put to sleep, a failure must not crash

    // turn off TCXO if it was powered
    lora.setTCXO(0);

    // put chipset into sleep mode. Interrupts are not necessarily detached here - disable() and NodeDB's
    // config-reload park both reach this with the ISR still attached - so the lock above is what keeps
    // the readout task out of it rather than the absence of interrupts.
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

// Don't leak the alias into the files InterfacesTemplates.cpp includes after this one.
#undef LR20x0
#endif
