#if RADIOLIB_EXCLUDE_LR2021 != 1
#include "LR20x0Interface.h"
#include "Throttle.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"
#ifdef LR2021_DIO_AS_RF_SWITCH
#include "rfswitch.h"
#elif ARCH_PORTDUINO
#include "PortduinoGlue.h"
#define lr2021_rfswitch_dio_pins portduino_config.rfswitch_dio_pins
#define lr2021_rfswitch_table portduino_config.rfswitch_table
#else
static const uint32_t lr2021_rfswitch_dio_pins[] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t lr2021_rfswitch_table[] = {
    {LR2021::MODE_STBY, {}}, {LR2021::MODE_RX, {}}, {LR2021::MODE_TX, {}}, {LR2021::MODE_RX_HF, {}}, {LR2021::MODE_TX_HF, {}},
    END_OF_MODE_TABLE,
};
#endif

// Particular boards might define a different max power based on what their hardware can do.
#if ARCH_PORTDUINO
#define LR2021_MAX_POWER portduino_config.lr1110_max_power
#endif
#ifndef LR2021_MAX_POWER
#define LR2021_MAX_POWER 22
#endif

#if ARCH_PORTDUINO
#define LR2021_MAX_POWER_HF portduino_config.lr1120_max_power
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
#elif !defined(LR2021_DIO3_TCXO_VOLTAGE)
    float tcxoVoltage = 0;
    LOG_DEBUG("LR2021_DIO3_TCXO_VOLTAGE not defined, not using DIO3 as TCXO reference voltage");
#else
    float tcxoVoltage = LR2021_DIO3_TCXO_VOLTAGE;
    LOG_DEBUG("LR2021_DIO3_TCXO_VOLTAGE defined, using DIO3 as TCXO reference voltage at %f V", LR2021_DIO3_TCXO_VOLTAGE);
#endif

    RadioLibInterface::init();

    limitPower(LR2021_MAX_POWER);
    if ((power > LR2021_MAX_POWER_HF) && (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        power = LR2021_MAX_POWER_HF;
        preambleLength = 12;
    }

#ifdef LR2021_RF_SWITCH_SUBGHZ
    pinMode(LR2021_RF_SWITCH_SUBGHZ, OUTPUT);
    digitalWrite(LR2021_RF_SWITCH_SUBGHZ, getFreq() < 1e9 ? HIGH : LOW);
    LOG_DEBUG("Set RF0 switch to %s", getFreq() < 1e9 ? "SubGHz" : "2.4GHz");
#endif

#ifdef LR2021_RF_SWITCH_2_4GHZ
    pinMode(LR2021_RF_SWITCH_2_4GHZ, OUTPUT);
    digitalWrite(LR2021_RF_SWITCH_2_4GHZ, getFreq() < 1e9 ? LOW : HIGH);
    LOG_DEBUG("Set RF1 switch to %s", getFreq() < 1e9 ? "SubGHz" : "2.4GHz");
#endif

// ToDo :All subsequent template classes must include the `irqDioNum` member; otherwise, an error will occur. 
//Alternatively, attempt to identify the template class (e.g., `LR2021`) before configuring `irqDioNum`.
#ifdef IQR_DIO_NUM
    lora.irqDioNum = IQR_DIO_NUM;
    LOG_DEBUG("Set irqDioNum  %d", lora.irqDioNum);
#else
    LOG_DEBUG("Use default irqDioNum  %d", lora.irqDioNum);
#endif

    delay(10);

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);

    if (res == RADIOLIB_ERR_SPI_CMD_FAILED) {
        LOG_WARN("LR20x0 init failed with %d (SPI_CMD_FAILED), retrying after delay...", res);
        delay(100);
        res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
    }

    LOG_INFO("LR20x0 init result %d", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND || res == RADIOLIB_ERR_SPI_CMD_FAILED)
        return false;


    LOG_INFO("Frequency set to %f", getFreq());
    LOG_INFO("Bandwidth set to %f", bw);
    LOG_INFO("Power output set to %d", power);



#ifdef LR2021_DIO_AS_RF_SWITCH
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = portduino_config.has_rfswitch_table;
#else
    bool dioAsRfSwitch = false;
#endif

    if (dioAsRfSwitch) {
        lora.setRfSwitchTable(lr2021_rfswitch_dio_pins, lr2021_rfswitch_table);
        LOG_DEBUG("Set DIO RF switch");
    }

    if (res == RADIOLIB_ERR_NONE) {
        uint8_t gain = config.lora.sx126x_rx_boosted_gain ? 7 : 0;
        res = lora.setRxBoostedGainMode(gain);
        LOG_INFO("Set RX boosted gain level to %u; result: %d", gain, res);
    }

    if (res == RADIOLIB_ERR_NONE)
        startReceive();

    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool LR20x0Interface<T>::reconfigure()
{
    RadioLibInterface::reconfigure();

    setStandby();

    int err = lora.setSpreadingFactor(sf);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setBandwidth(bw);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setCodingRate(cr, cr != 7);
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    err = lora.setSyncWord(syncWord);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > LR2021_MAX_POWER)
        power = LR2021_MAX_POWER;

    if ((power > LR2021_MAX_POWER_HF) && (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24))
        power = LR2021_MAX_POWER_HF;

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    //
    err = lora.setRxBoostedGainMode(config.lora.sx126x_rx_boosted_gain);
    if (err != RADIOLIB_ERR_NONE)
        LOG_WARN("LR20x0 setRxBoostedGainMode %s%d", radioLibErr, err);
    
    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template <typename T> void LR20x0Interface<T>::disableInterrupt()
{
    lora.clearIrqAction();
}

template <typename T> void LR20x0Interface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();
    if (err != RADIOLIB_ERR_NONE)
        LOG_DEBUG("LR20x0 standby failed with error %d", err);
    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    activeReceiveStart = 0;
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
    RadioLibInterface::setStandby();
}

template <typename T> void LR20x0Interface<T>::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
    // LOG_DEBUG("Corrected frequency offset: %f", lora.getFrequencyError());
}

template <typename T> void LR20x0Interface<T>::configHardwareForSend()
{
    RadioLibInterface::configHardwareForSend();
}

template <typename T> void LR20x0Interface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else
    setStandby();

    lora.setPreambleLength(preambleLength);

    int err = 
        lora.startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS, RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    if (err)
        LOG_ERROR("StartReceive error: %d", err);
    assert(err == RADIOLIB_ERR_NONE);

    RadioLibInterface::startReceive();

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
    checkRxDoneIrqFlag();
#endif
}

template <typename T> bool LR20x0Interface<T>::isChannelActive()
{
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

template <typename T> bool LR20x0Interface<T>::isActivelyReceiving()
{
    return receiveDetected(lora.getIrqStatus(), RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID, 
                                                RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED);
}

template <typename T> bool LR20x0Interface<T>::sleep()
{

    LOG_DEBUG("LR20x0 entering sleep mode");
    setStandby();

    lora.setTCXO(0);

    bool keepConfig = false;
    lora.sleep(keepConfig, 0);// Note: we do not keep the config, full reinit will be needed

#ifdef LR2021_POWER_EN
    digitalWrite(LR2021_POWER_EN, LOW);
#endif

    return true;
}

#endif
