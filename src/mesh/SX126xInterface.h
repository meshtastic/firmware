#pragma once
#if RADIOLIB_EXCLUDE_SX126X != 1

#include "RadioLibInterface.h"
#include "configuration.h"

// Bench: -DSX126X_TX_LAUNCH_TRACE times each step from the CAD verdict to SET_TX, as a CH341 host always does, and
// -DSX126X_TX_PRESTAGE also writes the payload before the scan.
#if defined(ARCH_PORTDUINO) || defined(SX126X_TX_LAUNCH_TRACE) || defined(SX126X_TX_PRESTAGE)
#define SX126X_TX_LAUNCH_OVERRIDE 1
#endif

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

#ifdef SX126X_STATE_SAMPLER_MS
    /// Bench: read the chip's mode and IRQ flags, changing neither, and log them when they differ from the last look
    void sampleChipState();
#ifdef SX126X_STATE_SAMPLER_TASK
    /// Bench: the same look from a FreeRTOS task above the main loop, queueing changes for sampleChipState() to log
    void sampleChipStateFromTask();
    static void chipStateTaskMain(void *arg);
#endif
#endif

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

#ifdef SX126X_TX_LAUNCH_OVERRIDE
    /** On a CH341 host or a bench build: time each step from the CAD verdict to SET_TX, and launch a payload staged
     *  before the scan */
    int16_t launchTransmit(size_t numbytes) override;
    /** Whether launchTransmit() takes the timed path: a CH341 host, or a build with the launch trace */
    bool txLaunchTimed() const;
#endif

  private:
#ifdef LORA_DIO1_SOFTWARE_POLL
    bool irqPollingActive = false;
    bool pollTxMode = false;
#endif
    /** Some boards require GPIO control of tx vs rx paths */
    void setTransmitEnable(bool txon);

#ifdef SX126X_TX_LAUNCH_OVERRIDE
    /** MESHTASTIC_TX_PRESTAGE=1 on a CH341 host, or -DSX126X_TX_PRESTAGE: write the payload before the CAD, leaving four
     *  commands after it */
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

#if defined(SX126X_STATE_SAMPLER_MS) || defined(SX126X_RX_REARM_AT_TX_DONE)
    /** The chip select, kept for raw commands outside RadioLib, which does not expose it */
    RADIOLIB_PIN_TYPE rawCs = RADIOLIB_NC;
#endif
#ifdef SX126X_STATE_SAMPLER_MS
    /** What the last sample saw, so only changes are logged; 0xFF/0xFFFF until the first look */
    uint8_t sampledMode = 0xFF;
    uint16_t sampledIrq = 0xFFFF;
    uint32_t lastSampleMs = 0;
    /** One GetIrqStatus; mode 0xB if the chip was BUSY. False only from the task, when the SPI lock was held */
    bool readChipState(bool fromTask, uint8_t &mode, uint16_t &irq, uint8_t &status);
#endif
#ifdef SX126X_STATE_SAMPLER_TASK
    /** The HAL without its lock, for the task: it takes the SPI lock itself, and never waits for it */
    ArduinoHal *samplerHal = nullptr;
    /** Changes seen by the task, stamped when seen; single producer (the task), single consumer (the main loop) */
    struct ChipStateEvent {
        uint32_t ms;
        uint16_t irq;
        uint8_t mode;
        uint8_t status;
    };
    static constexpr uint8_t chipStateRingSize = 255; // holds 254: a 175 ms loop hold at a few changes a frame
    ChipStateEvent chipStateRing[chipStateRingSize];
    volatile uint8_t chipStateHead = 0, chipStateTail = 0;
    volatile uint32_t chipStateDropped = 0, chipStateLockBusy = 0, chipStateTaskLate = 0;
#endif

    /** RX was armed continuous and nothing has put the chip into standby since, so it is still listening */
    bool rxArmedContinuous = false;

    bool resumeRunningReceive() override;

#ifdef SX126X_RX_REARM_AT_TX_DONE
    bool rearmReceiveFromIsr() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmOutcome : uint8_t { REARM_NONE, REARM_ARMED, REARM_SPI_BUSY, REARM_CHIP_BUSY, REARM_BAD_COMMAND };
    /** Longest raw command the ISR sends: SET_DIO_IRQ_PARAMS, opcode plus 8 bytes */
    static constexpr size_t rawCommandMax = 9;
    /** One raw command from the ISR: wait briefly for BUSY, then write it without RadioLib or the SPI lock */
    RearmOutcome rawCommandFromIsr(const uint8_t *cmd, size_t len);
    /** The HAL without its lock: the ISR has already taken the SPI lock without blocking */
    ArduinoHal *isrHal = nullptr;
    volatile uint8_t rearmOutcome = REARM_NONE;
    /** FreeRTOS tick count when the ISR re-armed RX */
    volatile uint32_t rearmTicks = 0;
#endif

    /** How long the last trySetStandby() spent in each part, in ms, for the channel scan's step trace */
    struct StandbySteps {
        uint32_t notifyMs, cmdMs, detachMs;
    } lastStandbySteps = {0, 0, 0};

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }
};
#endif