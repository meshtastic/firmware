#pragma once

#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in
#include "RadioLibInterface.h"
#include "RadioLibRF95.h"

/**
 * Our new not radiohead adapter for RF95 style radios
 */
class RF95Interface : public RadioLibInterface
{
    RadioLibRF95 *lora; // Either a RFM95 or RFM96 depending on what was stuffed on this board

  public:
    RF95Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, SPIClass &spi);

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
    virtual void enableInterrupt(void (*callback)()) { lora->setDio0Action(callback); }

    /** are we actively receiving a packet (only called during receiving state) */
    virtual bool isActivelyReceiving();

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive();

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(MeshPacket *mp);

    virtual void setStandby();

    /**
     *  We override to turn on transmitter power as needed.
     */
    virtual void configHardwareForSend();

  private:
    /** Some boards require GPIO control of tx vs rx paths */
    void setTransmitEnable(bool txon);
};