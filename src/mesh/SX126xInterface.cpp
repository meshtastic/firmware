#include "SX126xInterface.h"
#include "configuration.h"
#include "error.h"
#include "mesh/NodeDB.h"

// Particular boards might define a different max power based on what their hardware can do, default to max power output if not specified (may be dangerous if using external PA and SX126x power config forgotten)
#ifndef SX126X_MAX_POWER
#define SX126X_MAX_POWER 22
#endif

template <typename T>
SX126xInterface<T>::SX126xInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                    RADIOLIB_PIN_TYPE busy)
    : RadioLibInterface(hal, cs, irq, rst, busy, &lora), lora(&module)
{
    LOG_WARN("SX126xInterface(cs=%d, irq=%d, rst=%d, busy=%d)\n", cs, irq, rst, busy);
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
template <typename T> bool SX126xInterface<T>::init()
{
#ifdef SX126X_POWER_EN
    digitalWrite(SX126X_POWER_EN, HIGH);
    pinMode(SX126X_POWER_EN, OUTPUT);
#endif

// If we need more TCXO voltage ranges in the future we can split this into DIO3_AS_TCXO and DIO3_TCXO_VOLTAGE
#ifdef DIO3_AS_TCXO_AT_1V8
    // Used in E22 modules as TCXO voltage is 1.8V per https://www.ebyte.com/en/pdf-down.aspx?id=781 (and https://github.com/jgromes/RadioLib/issues/12#issuecomment-520695575)
    // (DIO3 is not free to be used as an IRQ)
    LOG_DEBUG("DIO3_AS_TCXO_AT_1V8 mode true. Using DIO3 to power TCXO with 1.8V\n");
    float tcxoVoltage = 1.8;
#else
    // (DIO3 is free to be used as an IRQ)
    LOG_DEBUG("DIO3_AS_TCXO_AT_1V8 mode false. Not using DIO3 to power TCXO\n");
    float tcxoVoltage = 0; // None - we use an XTAL
#endif
    bool useRegulatorLDO = false; // Seems to depend on the connection to pin 9/DCC_SW - if an inductor DCDC?

    RadioLibInterface::init();

    // Do not correct 0dBm to 22dBm as may be using external amplifier! (removed code that did this)

    // Clamp power output to valid range (incorrect value may be entered as this chip has lower power limits than some)
    if (power > SX126X_MAX_POWER)
        power = SX126X_MAX_POWER;

    limitPower();

    int res = lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO);
    // \todo Display actual typename of the adapter, not just `SX126x`
    LOG_INFO("SX126x init result %d\n", res);
    if (res == RADIOLIB_ERR_CHIP_NOT_FOUND)
        return false;

    LOG_INFO("Frequency set to %f\n", getFreq());
    LOG_INFO("Bandwidth set to %f\n", bw);
    LOG_INFO("Power output set to %d\n", power);

    // current limit was removed from module' ctor
    // override default value (60 mA) using value in SX126xInterface.h - might not be true that default value FOR ALL SX126x is 60mA:
    // From Table 12-1: List of Registers in SX1262 datasheet
    // Set the Over Current Protection level. The value is changed internally depending on the device selected.
    // Default values are: SX1262: 0x38 (140 mA), SX1261: 0x18 (60 mA)
    res = lora.setCurrentLimit(currentLimit);
    LOG_DEBUG("Current limit set to %f\n", currentLimit);
    LOG_DEBUG("Current limit set result %d\n", res);
    // Requried current for SX126x shouldn't be affected by PA
    // Note: setting current limit for all SX126x as 140mA may be dangerous if support for SX1261 is added and this file is used!

// Reference 13.3.5 SetDIO2AsRfSwitchCtrl in SX1262 datasheet
#if defined(DIO2_AS_RF_SWITCH)
    // DIO2 is selected to be used to control an RF switch. In this case:
    // DIO2 = 0 in SLEEP, STDBY_RX, STDBY_XOSC, FS and RX modes, DIO2 = 1 in TX mode
    // (DIO2 is not free to be used as an IRQ)
    if (res == RADIOLIB_ERR_NONE) {
        LOG_DEBUG("DIO2_AS_RF_SWITCH mode true. Setting DIO2 as RF switch\n");
        res = lora.setDio2AsRfSwitch(true);
    }
#else
   // (DIO2 is free to be used as an IRQ)
   if (res == RADIOLIB_ERR_NONE) {
        LOG_DEBUG("DIO2_AS_RF_SWITCH mode false. Setting DIO2 as not RF switch\n");
        res = lora.setDio2AsRfSwitch(false);
    }
#endif

#ifndef SX126X_RXEN
    #define SX126X_RXEN RADIOLIB_NC;
#endif
#ifndef SX126X_TXEN
    #define SX126X_TXEN RADIOLIB_NC;
#endif
    // If the pins for RX or for TX isn't defined, then it won't control that function as it is set to NC
    if (res == RADIOLIB_ERR_NONE) {
        LOG_DEBUG("Setting RF Switch: RXEN=%i, TXEN=%i\n", SX126X_RXEN, SX126X_TXEN);
        lora.setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
    }

    if (config.lora.sx126x_rx_boosted_gain) {
        uint16_t result = lora.setRxBoostedGainMode(true);
        LOG_INFO("Set Rx Boosted Gain mode; result: %d\n", result);
    } else {
        uint16_t result = lora.setRxBoostedGainMode(false);
        LOG_INFO("Set Rx Power Saving Gain mode; result: %d\n", result);
    }

#if 0
    // Read/write a register we are not using (only used for FSK mode) to test SPI comms
    uint8_t crcLSB = 0;
    int err = lora.readRegister(SX126X_REG_CRC_POLYNOMIAL_LSB, &crcLSB, 1);
    if(err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    //if(crcLSB != 0x0f)
    //    RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    crcLSB = 0x5a;
    err = lora.writeRegister(SX126X_REG_CRC_POLYNOMIAL_LSB, &crcLSB, 1);
    if(err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    err = lora.readRegister(SX126X_REG_CRC_POLYNOMIAL_LSB, &crcLSB, 1);
    if(err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);

    if(crcLSB != 0x5a)
        RECORD_CRITICALERROR(CriticalErrorCode_SX1262Failure);
    // If we got this far register accesses (and therefore SPI comms) are good
#endif

    if (res == RADIOLIB_ERR_NONE)
        res = lora.setCRC(RADIOLIB_SX126X_LORA_CRC_ON);

    if (res == RADIOLIB_ERR_NONE)
        startReceive(); // start receiving

    return res == RADIOLIB_ERR_NONE;
}

template <typename T> bool SX126xInterface<T>::reconfigure()
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

    err = lora.setCurrentLimit(currentLimit);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setPreambleLength(preambleLength);
    assert(err == RADIOLIB_ERR_NONE);

    err = lora.setFrequency(getFreq());
    if (err != RADIOLIB_ERR_NONE)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

    if (power > SX126X_MAX_POWER) // This chip has lower power limits than some
        power = SX126X_MAX_POWER;

    err = lora.setOutputPower(power);
    assert(err == RADIOLIB_ERR_NONE);

    startReceive(); // restart receiving

    return RADIOLIB_ERR_NONE;
}

template <typename T> void INTERRUPT_ATTR SX126xInterface<T>::disableInterrupt()
{
    lora.clearDio1Action();
}

template <typename T> void SX126xInterface<T>::setStandby()
{
    checkNotification(); // handle any pending interrupts before we force standby

    int err = lora.standby();

    if (err != RADIOLIB_ERR_NONE) {
        LOG_DEBUG("SX126x standby failed with error %d\n", err);
    }

    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    activeReceiveStart = 0;
    disableInterrupt();
    completeSending(); // If we were sending, not anymore
}

/**
 * Add SNR data to received messages
 */
template <typename T> void SX126xInterface<T>::addReceiveMetadata(meshtastic_MeshPacket *mp)
{
    // LOG_DEBUG("PacketStatus %x\n", lora.getPacketStatus());
    mp->rx_snr = lora.getSNR();
    mp->rx_rssi = lround(lora.getRSSI());
}

/** We override to turn on transmitter power as needed.
 */
template <typename T> void SX126xInterface<T>::configHardwareForSend()
{
    RadioLibInterface::configHardwareForSend();
}

// For power draw measurements, helpful to force radio to stay sleeping
// #define SLEEP_ONLY

template <typename T> void SX126xInterface<T>::startReceive()
{
#ifdef SLEEP_ONLY
    sleep();
#else

    setStandby();

    // We use a 16 bit preamble so this should save some power by letting radio sit in standby mostly.
    // Furthermore, we need the PREAMBLE_DETECTED and HEADER_VALID IRQ flag to detect whether we are actively receiving
    int err = lora.startReceiveDutyCycleAuto(preambleLength, 8,
                                             RADIOLIB_SX126X_IRQ_RX_DEFAULT | RADIOLIB_SX126X_IRQ_RADIOLIB_PREAMBLE_DETECTED |
                                                 RADIOLIB_SX126X_IRQ_HEADER_VALID);
    assert(err == RADIOLIB_ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
#endif
}

/** Is the channel currently active? */
template <typename T> bool SX126xInterface<T>::isChannelActive()
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
template <typename T> bool SX126xInterface<T>::isActivelyReceiving()
{
    // The IRQ status will be cleared when we start our read operation.  Check if we've started a header, but haven't yet
		// received and handled the interrupt for reading the packet/handling errors.

    uint16_t irq = lora.getIrqStatus();
    bool detected = (irq & (RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_RADIOLIB_PREAMBLE_DETECTED));
    // Handle false detections
    if (detected) {
        uint32_t now = millis();
        if (!activeReceiveStart) {
            activeReceiveStart = now;
        } else if ((now - activeReceiveStart > 2 * preambleTimeMsec) && !(irq & RADIOLIB_SX126X_IRQ_HEADER_VALID)) {
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

template <typename T> bool SX126xInterface<T>::sleep()
{
    // Not keeping config is busted - next time nrf52 board boots lora sending fails  tcxo related? - see datasheet
    // \todo Display actual typename of the adapter, not just `SX126x`
    LOG_DEBUG("SX126x entering sleep mode (FIXME, don't keep config)\n");
    setStandby(); // Stop any pending operations

    // turn off TCXO if it was powered
    // FIXME - this isn't correct
    // lora.setTCXO(0);

    // put chipset into sleep mode (we've already disabled interrupts by now)
    bool keepConfig = true;
    lora.sleep(keepConfig); // Note: we do not keep the config, full reinit will be needed

#ifdef SX126X_POWER_EN
    digitalWrite(SX126X_POWER_EN, LOW);
#endif

    return true;
}
