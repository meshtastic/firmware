#pragma once
#if RADIOLIB_EXCLUDE_SX126X != 1

#include "RadioLibInterface.h"
#include "configuration.h"

/**
 * \brief Adapter for SX126x radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for SX126x: SX1262, SX1268.
 */
template <class T> class SX126xInterface : public RadioLibInterface
{
  public:
    SX126xInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init() override;

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() override;

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() override;

    bool isIRQPending() override { return lora.getIrqFlags() != 0; }

    void resetAGC() override;

    void setTCXOVoltage(float voltage) { tcxoVoltage = voltage; }

  protected:
    float currentLimit = 140; // Higher OCP limit for SX126x PA
    float tcxoVoltage = 0.0;

    /**
     * Specific module instance
     */
    T lora;

    int16_t getCurrentRSSI() override;

    /**
     * Glue functions called from ISR land
     */
    virtual void clearRadioIsr() override;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void setRadioIsr(void (*callback)()) override;

#ifdef LORA_DIO1_SOFTWARE_POLL
    void handleSoftwareLoraIrqPoll() override;
#endif

    /** can we detect a LoRa preamble on the current channel? */
    virtual bool isChannelActive() override;

    /** are we actively receiving a packet (only called during receiving state) */
    virtual bool isActivelyReceiving() override;

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive() override;

    /**
     *  We override to turn on transmitter power as needed.
     */
    virtual void configHardwareForSend() override;

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(meshtastic_MeshPacket *mp) override;

    virtual void setStandby() override;

    uint32_t getPacketTime(uint32_t pl, bool received) override { return computePacketTime(lora, pl, received); }

    // Sub-GHz only. isChannelActive() passes CAD_ON_4_SYMB; keep the two in step.
    uint8_t getCadSymbolCountSubGhz() const override { return 4; }

#ifdef ARCH_PORTDUINO
    /** On a CH341 host: time each step from the CAD verdict to SET_TX, and launch a payload staged before the scan */
    int16_t launchTransmit(size_t numbytes) override;
#endif

  private:
#ifdef LORA_DIO1_SOFTWARE_POLL
    bool irqPollingActive = false;
    bool pollTxMode = false;
#endif
    /** Some boards require GPIO control of tx vs rx paths */
    void setTransmitEnable(bool txon);

#ifdef ARCH_PORTDUINO
    /** MESHTASTIC_TX_PRESTAGE=1 on a CH341 host: write the payload before the CAD, leaving four commands after it */
    bool txPrestageEnabled = false;
    /** A full RadioLib TX staging (which applies the sensitivity fix) has run since the chip last lost its registers */
    bool txStagedByRadioLib = false;
    /** The payload isChannelActive() wrote into the chip's buffer before the CAD, or 0 bytes if none */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
    /** When the last CAD verdict was read, for the launch step trace */
    uint32_t cadVerdictMs = 0;
#endif

    /** Program all modem parameters into the chip; returns the first RadioLib error, or RADIOLIB_ERR_NONE */
    int16_t programModemParams();

    /** begin() and chip-side setup, shared by init() and by reconfigure()'s recovery of a chip that lost its state */
    bool reinitChip();

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

    /** RX was armed continuous and nothing has put the chip into standby since, so it is still listening */
    bool rxArmedContinuous = false;

    bool resumeRunningReceive() override;

    /** How long the last trySetStandby() spent in each part, in ms, for the channel scan's step trace */
    struct StandbySteps {
        uint32_t notifyMs, cmdMs, detachMs;
    } lastStandbySteps = {0, 0, 0};

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }
};
#endif