#pragma once

#include "RadioLibInterface.h"

/**
 * \brief Adapter for SX128x radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for SX128x: SX1280.
 */
template <class T> class SX128xInterface : public RadioLibInterface
{
  public:
    SX128xInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init() override;

    virtual bool wideLora() override;

    /// SX128x is a 2.4 GHz-only chip; it cannot tune sub-GHz regions
    virtual bool supportsSubGhz() override { return false; }

    bool supportsModulation(RadioModulation m) override { return true; }

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() override;

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() override;

    bool isIRQPending() override { return lora.getIrqFlags() != 0; }

    /// DS rev 3.3 16.2: a header error leaves RX wedged with no RX_DONE or timeout, so only a re-arm clears it
    bool headerErrWedgesRx() const override { return true; }

  protected:
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
    virtual void setRadioIsr(void (*callback)()) override { lora.setDio1Action(callback); }

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

    uint32_t getPacketTime(uint32_t pl, bool received) override
    {
        return isFastModem() ? fastPacketTimeMsec(pl, received) : computePacketTime(lora, pl, received);
    }

    float getRxSNR() override;

    // FLRC frames carry at most 127 bytes, so FLRC packets get a 1-byte fragment header and are split when needed.
    int16_t launchTransmit(size_t numbytes) override;
    uint32_t nextFrameDelayMsec() override;
    bool sendNextFrame() override;
    uint32_t lastTxIdleMsec() override { return usesFragments() && txFragCount > 1 ? (txFragCount - 1) * FRAG_GAP_MSEC : 0; }
    int acceptFrame(size_t &length) override;

    bool readRxHeaderInfo(uint8_t &cr, bool &hasCRC) override
    {
        return lora.getLoRaRxHeaderInfo(&cr, &hasCRC) == RADIOLIB_ERR_NONE;
    }

    // 2.4 GHz only. isChannelActive() passes CAD_ON_8_SYMB; keep the two in step.
    uint8_t getCadSymbolCountWideLora() const override { return 8; }

    /** Set the CAD peak-to-noise threshold for the current sf/bw. Call after either changes. */
    void applyCadDetPeak();

  private:
    static const uint8_t FLRC_MAX_FRAME = 127;
    // Bench: 16- and 24-bit FLRC preambles lost every packet at 650 kb/s CR 1/2; 32 bits worked at every rate and CR
    static const uint8_t FLRC_PREAMBLE_BITS = 32;
    static const uint8_t GFSK_PREAMBLE_BITS = 24;
    // A receiver must read a fragment out before the next one overwrites the SX128x buffer
    static const uint8_t FRAG_GAP_MSEC = 10;

    RadioModulation activeModulation = MODULATION_LORA; // modem currently programmed in the chip
    uint8_t frameBuf[FLRC_MAX_FRAME];
    size_t txFragLen = 0;
    uint8_t txFragCount = 0, txFragNext = 0, txFragTag = 0, txFragId = 0;
    uint8_t rxFrag[MAX_LORA_PAYLOAD_LEN];
    size_t rxFragLen = 0;
    uint8_t rxFragTag = 0, rxFragCount = 0, rxFragNext = 0xFF, rxFragId = 0;

    /** begin() for the configured modem: LoRa, or the FLRC/GFSK preset in fastModem */
    int16_t beginModem();
    bool usesFragments() const { return fastModem.modulation == MODULATION_FLRC; }
    size_t maxFrameLen() const { return usesFragments() ? FLRC_MAX_FRAME : MAX_LORA_PAYLOAD_LEN; }
    // A whole packet takes a 1-byte header; each fragment of a split packet takes 2 (see launchTransmit())
    static uint8_t fragmentCount(size_t len)
    {
        return len < FLRC_MAX_FRAME ? 1 : (len + FLRC_MAX_FRAME - 3) / (FLRC_MAX_FRAME - 2);
    }
    int16_t transmitFragment();
    uint32_t fastPacketTimeMsec(uint32_t pl, bool received);

    /** Program all modem parameters into the chip; returns the first RadioLib error, or RADIOLIB_ERR_NONE */
    int16_t programModemParams();

    /** begin() and chip-side setup, shared by init() and by reconfigure()'s recovery of a chip that lost its state */
    /** @param fromInit true only for the boot-time call, which may adjust region and reboot on a
     *  2.4GHz-only part; a runtime recovery must never reboot the node. */
    bool reinitChip(bool fromInit = false);

    /** setStandby()'s body, returning the standby error instead of asserting - for callers that can recover */
    int16_t trySetStandby();

    /** Recover a chip that lost its runtime state: hardware-reset via begin() and reprogram */
    bool recoverChipStateLoss() override
    {
        if (!reinitChip() || programModemParams() != RADIOLIB_ERR_NONE)
            return false;
        applyCadDetPeak(); // the reset took the CAD threshold with the rest of the chip state
        return true;
    }
};
