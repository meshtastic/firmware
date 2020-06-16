#pragma once

#include "RadioLibInterface.h"

/**
 * Our adapter for SX1262 radios
 */
class SX1262Interface : public RadioLibInterface
{
    SX1262 lora;

  public:
    SX1262Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init();

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure();

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep();

  protected:
    /**
     * Glue functions called from ISR land
     */
    virtual void disableInterrupt();

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*callback)()) { lora.setDio1Action(callback); }

    /** are we actively receiving a packet (only called during receiving state) */
    virtual bool isActivelyReceiving();

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive();

    /**
     *  We override to turn on transmitter power as needed.
     */
    virtual void configHardwareForSend();

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(MeshPacket *mp);

    virtual void setStandby();

  private:
};