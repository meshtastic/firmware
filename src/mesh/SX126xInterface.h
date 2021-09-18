#pragma once

#include "RadioLibInterface.h"

/**
 * \brief Adapter for SX126x radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for SX126x: SX1262, SX1268.
 */
template<class T>
class SX126xInterface : public RadioLibInterface
{
  public:
    SX126xInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);

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

    bool isIRQPending() { return lora.getIrqStatus() != 0; }

  protected:

    /**
     * Specific module instance 
     */
    T lora;

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