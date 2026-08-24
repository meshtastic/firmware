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

    /** Re-arm RX, skipping the standby only after a CAD GOTO_RX handoff has left the chip listening. */
    void rearmReceive() override;

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

  private:
    // Set when CAD exited straight into RX, so the next rearmReceive() knows the chip is already
    // listening. Cleared there; the re-arm after that packet's RX_DONE is a normal full one.
    bool cadHandedToRx = false;

#ifdef LORA_DIO1_SOFTWARE_POLL
    bool irqPollingActive = false;
    bool pollTxMode = false;
#endif
    /** Some boards require GPIO control of tx vs rx paths */
    void setTransmitEnable(bool txon);
};
#endif