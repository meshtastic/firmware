#pragma once
#if RADIOLIB_EXCLUDE_SX126X != 1

#include "RadioLibInterface.h"
#include "configuration.h"

// Re-arm RX from the TX_DONE interrupt instead of waiting for the RadioIf thread, which a main-loop hold can delay by
// hundreds of ms. On by default on nRF52, whose SPI can be driven from an interrupt, where DIO1 is a real interrupt and no
// LoRa FEM needs setting for RX. -DSX126X_RX_REARM_AT_TX_DONE=0 turns it off.
#ifndef SX126X_RX_REARM_AT_TX_DONE
#if defined(ARCH_NRF52) && !defined(LORA_DIO1_SOFTWARE_POLL) && !HAS_LORA_FEM
#define SX126X_RX_REARM_AT_TX_DONE 1
#else
#define SX126X_RX_REARM_AT_TX_DONE 0
#endif
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

  private:
#ifdef LORA_DIO1_SOFTWARE_POLL
    bool irqPollingActive = false;
    bool pollTxMode = false;
#endif
    /** Some boards require GPIO control of tx vs rx paths */
    void setTransmitEnable(bool txon);

    /** Program all modem parameters into the chip; returns the first RadioLib error, or RADIOLIB_ERR_NONE */
    int16_t programModemParams();

    /** begin() and chip-side setup, shared by init() and by reconfigure()'s recovery of a chip that lost its state */
    bool reinitChip();

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override { return reinitChip() && programModemParams() == RADIOLIB_ERR_NONE; }

#if SX126X_RX_REARM_AT_TX_DONE
    bool rearmReceiveFromIsr() override;
    bool adoptReceiveArmedFromIsr() override;
    enum RearmOutcome : uint8_t { REARM_NONE, REARM_ARMED, REARM_SPI_BUSY, REARM_CHIP_BUSY, REARM_NOT_TX_DONE };
    /** Longest raw command the ISR sends: SET_DIO_IRQ_PARAMS, opcode plus 8 bytes */
    static constexpr size_t rawCommandMax = 9;
    /** One raw command from the ISR, with the SPI lock already held: wait briefly for BUSY, then write it */
    RearmOutcome rawCommandFromIsr(const uint8_t *cmd, size_t len, uint8_t *in);
    /** The chip select, kept for raw commands: RadioLib does not expose it */
    RADIOLIB_PIN_TYPE rawCs = RADIOLIB_NC;
    /** The HAL without its lock, for the ISR, which takes the SPI lock itself without blocking */
    ArduinoHal *isrHal = nullptr;
    volatile uint8_t rearmOutcome = REARM_NONE;
    /** FreeRTOS tick count when the ISR re-armed RX */
    volatile uint32_t rearmTicks = 0;
#endif
};
#endif