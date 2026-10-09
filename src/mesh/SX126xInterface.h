#pragma once
#if RADIOLIB_EXCLUDE_SX126X != 1

#include "RadioLibInterface.h"
#include "configuration.h"

// After TX_DONE the SX126x waits in standby for the radio thread to restart RX, which a main-loop hold can stretch by
// hundreds of ms. With the readout task, the TX_DONE interrupt has the task restart it instead, on every platform the
// task runs on.

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

    bool resetAGC() override;

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

    bool readRxHeaderInfo(uint8_t &cr, bool &hasCRC) override
    {
        return lora.getLoRaRxHeaderInfo(&cr, &hasCRC) == RADIOLIB_ERR_NONE;
    }

    // Sub-GHz only. isChannelActive() passes CAD_ON_4_SYMB; keep the two in step.
    uint8_t getCadSymbolCountSubGhz() const override { return 4; }

#ifdef ARCH_PORTDUINO
    /** On a CH341 host: launch a payload the scan staged, sending only what the scan overwrote */
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
    /** A full RadioLib TX staging (which applies the register fixes) has run since the chip last lost its registers */
    bool txStagedByRadioLib = false;
    /** The payload the scan wrote into the chip's buffer, or 0 bytes if none, and its packet id */
    size_t prestagedLen = 0;
    uint32_t prestagedId = 0;
    /** On a CH341 host: write scanForTx's payload in the scan's standby, so a clear verdict leaves four commands */
    void prestageTx();

    // Staging while RX runs, on a CH341 host. Continuous RX writes each frame right after the last one and wraps at
    // the buffer's end, so a payload staged in RX goes just behind the write point and each readout checks the two.
    /** The payload was staged while RX ran, at prestagedBase: the launch points the TX base there */
    bool prestagedInRx = false;
    uint8_t prestagedBase = 0;
    /** Where continuous RX writes its next frame */
    uint8_t rxWritePtr = 0;
    /** A frame arrived or finished around a stage write: its readout checks it against the staged bytes */
    bool rxClobberCheck = false;
    uint8_t rxClobberBase = 0;
    size_t rxClobberLen = 0;
    uint8_t rxClobberBytes[256];
    /** A payload written during its backoff: its length (0 if none), packet id, offset and bytes */
    size_t earlyStagedLen = 0;
    uint32_t earlyStagedId = 0;
    uint8_t earlyStagedBase = 0;
    uint8_t earlyStagedBytes[256];

    /** Where a payload of this length goes while RX runs: just behind RX's write point */
    uint8_t txStageBase(size_t numbytes) const;
    void noteStagedOverFrame(uint8_t base, size_t numbytes);
    /** Write scanForTx's payload while RX runs. True if a frame was arriving or unread: then the scan must not go
     *  ahead, since its standby would abort that frame. */
    bool stageTxInRx();
    /** At the scan: whether the early stage still holds exactly this packet; if so it becomes the scan's prestage */
    bool takeEarlyTxStage();
    bool rxFrameOverlapsTxStage(size_t length) override;
    bool wantsEarlyTxStage() const override;
    void stageTxEarly(meshtastic_MeshPacket *p) override;
#endif
    /** The SET_CAD_PARAMS bytes last sent, resent only when they change; invalid once the chip can have lost them */
    uint8_t cadParamsSent[7] = {};
    bool cadParamsValid = false;
    /** lora.scanChannel(cfg), in fewer commands on a CH341 host */
    int16_t scanChannelForTx(const ChannelScanConfig_t &cfg);

    /** Program all modem parameters into the chip; returns the first RadioLib error, or RADIOLIB_ERR_NONE */
    int16_t programModemParams();

    /** begin() and chip-side setup, shared by init() and by reconfigure()'s recovery of a chip that lost its state */
    bool reinitChip();

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

    /** RX was armed continuous and nothing has put the chip into standby since, so it is still listening */
    bool rxArmedContinuous = false;

    bool resumeRunningReceive() override;

    /** The RX command startReceive() sends once the chip is in standby. Always a continuous RX */
    int16_t startRxCommand();

#ifdef MESHTASTIC_RX_READOUT_TASK
    bool rearmReceiveFromIsr() override;
    void rearmReceiveFromTask() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmState : uint8_t { REARM_NONE, REARM_PENDING, REARM_ARMED, REARM_FAILED };
    volatile uint8_t rearmState = REARM_NONE;
    volatile int16_t rearmErr = 0;
#endif

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }
};
#endif