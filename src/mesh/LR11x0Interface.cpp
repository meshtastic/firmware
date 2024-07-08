#include "LR11x0Interface.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"
#ifdef ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

// Particular boards might define a different max power based on what their hardware can do, default to max power output if not
// specified (may be dangerous if using external PA and LR11x0 power config forgotten)
#ifndef LR11X0_MAX_POWER
#define LR11X0_MAX_POWER 22
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

    if (power > LR11X0_MAX_POWER) // Clamp power to maximum defined level
        power = LR11X0_MAX_POWER;

    limitPower();

    // set RF switch configuration for Wio WM1110
    // Wio WM1110 uses DIO5 and DIO6 for RF switching
    // NOTE: other boards may be different. If you are
    // using a different board, you may need to wrap
    // this in a conditional.

    static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC,
                                                 RADIOLIB_NC};

    static const Module::RfSwitchMode_t rfswitch_table[] = {
        // mode                  DIO5  DIO6
        {LR11x0::MODE_STBY, {LOW, LOW}},  {LR11x0::MODE_RX, {HIGH, LOW}},
        {LR11x0::MODE_TX, {HIGH, HIGH}},  {LR11x0::MODE_TX_HP, {LOW, HIGH}},
        {LR11x0::MODE_TX_HF, {LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW}},
        {LR11x0::MODE_WIFI, {LOW, LOW}},  END_OF_MODE_TABLE,
    };

// We need to do this before begin() call
#ifdef LR11X0_DIO_AS_RF_SWITCH
    LOG_DEBUG("Setting DIO RF switch\n");
    bool dioAsRfSwitch = true;
#elif defined(ARCH_PORTDUINO)
    bool dioAsRfSwitch = false;
    if (settingsMap[dio2_as_rf_switch]) {
        LOG_DEBUG("Setting DIO RF switch\n");
        dioAsRfSwitch = true;
    }
#else
    bool dioAsRfSwitch = false;
#endif

    if (dioAsRfSwitch)
        lora.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage);
    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_INFO("LR11x0 init result %d\n", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND)
        return false;

    LOG_INFO("Frequency set to %f\n", getFreq());
    LOG_INFO("Bandwidth set to %f\n", bw);
    LOG_INFO("Power output set to %d\n", power);

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(2);

    // FIXME: May want to set depending on a definition, currently all LR1110 variant files use the DC-DC regulator option
    if (res == RADIOLIB_ERR_NONE)
        res = lora.setRegulatorDCDC();

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

    if (power > LR11X0_MAX_POWER) // This chip has lower power limits than some
        power = LR11X0_MAX_POWER;

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
    int err = lora.startReceive(
        RADIOLIB_LR11X0_RX_TIMEOUT_INF, RADIOLIB_LR11X0_IRQ_RX_DONE,
        0); // only RX_DONE IRQ is needed, we'll check for PREAMBLE_DETECTED and HEADER_VALID in isActivelyReceiving
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

    uint16_t irq = lora.getIrqStatus();
    bool detected = (irq & (RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED));
    // Handle false detections
    if (detected) {
        uint32_t now = millis();
        if (!activeReceiveStart) {
            activeReceiveStart = now;
        } else if ((now - activeReceiveStart > 2 * preambleTimeMsec) && !(irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID)) {
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

    // if (detected) LOG_DEBUG("rx detected\n");
    return detected;
}

template <typename T> bool LR11x0Interface<T>::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    // \todo Display actual typename of the adapter, not just `LR11x0`
    LOG_DEBUG("LR11x0 entering sleep mode (FIXME, don't keep config)\n");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    // FIXME - this isn't correct
    // lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig, 0); // Note: we do not keep the config, full reinit will be needed

#ifdef LR11X0_POWER_EN
    digitalWrite(LR11X0_POWER_EN, LOW);
#endif

    return true;
}