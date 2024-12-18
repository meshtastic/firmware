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

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() override;

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() override;

    bool isIRQPending() override { return lora.getIrqFlags() != 0; }

  protected:
    /**
     * Specific module instance
     */
    T lora;

    /**
     * Glue functions called from ISR land
     */
    virtual void disableInterrupt() override;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*callback)()) { lora.setDio1Action(callback); }

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
};
