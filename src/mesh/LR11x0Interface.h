#pragma once
#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "LR11x0ConfigApply.h"
#include "RadioLibInterface.h"

/**
 * \brief Adapter for LR11x0 radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for LR11x0: SX1262, SX1268.
 */
template <class T> class LR11x0Interface : public RadioLibInterface
{
  public:
    LR11x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
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

#ifdef LR11X0_AGC_RESET
    void resetAGC() override;
#endif

  protected:
    /**
     * Specific module instance
     */
    T lora;

    int16_t getCurrentRSSI() override;

    /**
     * Glue functions called from ISR land
     */
    virtual void disableInterrupt() override;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*callback)()) { lora.setIrqAction(callback); }

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

    LR11x0ConfigApplyParams makeReconfigureParams();

  private:
    bool configuredWideBand = false;

    class ConfigApplyOps
    {
      public:
        explicit ConfigApplyOps(LR11x0Interface<T> &radio) : radio(radio) {}

        int beginLoRa(float bandwidth, uint8_t spreadingFactor, uint8_t codingRate, uint8_t syncWord, uint16_t preambleLength,
                      bool wideBand)
        {
            return static_cast<LR11x0 &>(radio.lora)
                .begin(bandwidth, spreadingFactor, codingRate, syncWord, preambleLength, wideBand);
        }
        int standby() { return radio.setStandbyForReconfigure(); }
        int setSpreadingFactor(uint8_t spreadingFactor) { return radio.lora.setSpreadingFactor(spreadingFactor); }
        int setBandwidth(float bandwidth, bool wideBand)
        {
            targetWideBand = wideBand;
            return radio.lora.setBandwidth(bandwidth, wideBand);
        }
        int setCodingRate(uint8_t codingRate, bool interleaving) { return radio.lora.setCodingRate(codingRate, interleaving); }
        int setSyncWord(uint8_t syncWord) { return radio.lora.setSyncWord(syncWord); }
        int setPreambleLength(uint16_t preambleLength) { return radio.lora.setPreambleLength(preambleLength); }
        int setFrequency(float frequency)
        {
            return lr11x0SetFrequencyForBand(*this, frequency, targetWideBand, radio.configuredWideBand);
        }
        int setFrequency(float frequency, bool skipCalibration) { return radio.lora.setFrequency(frequency, skipCalibration); }
        int calibrateImage(float frequencyMin, float frequencyMax)
        {
            return radio.lora.calibrateImageRejection(frequencyMin, frequencyMax);
        }
        void waitForFrequencyRetry() { delay(100); }
        bool isRetryableFrequencyError(int error) { return error == RADIOLIB_ERR_SPI_CMD_FAILED; }
        int setOutputPower(int8_t outputPower) { return radio.lora.setOutputPower(outputPower); }
        int setRxBoostedGainMode(bool boostedGain) { return radio.lora.setRxBoostedGainMode(boostedGain); }
        int startReceive()
        {
            return radio.lora.startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, MESHTASTIC_RADIOLIB_IRQ_RX_FLAGS,
                                           RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
        }

      private:
        LR11x0Interface<T> &radio;
        bool targetWideBand = false;
    };

    int setStandbyForReconfigure();
    int setStandby(bool completePacket);
    int reinitializeForBand(const LR11x0ConfigApplyParams &params, LR11x0ApplyStep *failedStep);
    void selectExternalRfPath(float frequency);
    void finishStartReceive();

    bool receiveStartedDuringReconfigure = false;
};
#endif
