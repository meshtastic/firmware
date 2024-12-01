#pragma once
#if RADIOLIB_EXCLUDE_SX127X != 1
#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in
#include "RadioLibInterface.h"
#include "RadioLibRF95.h"

/**
 * Our new not radiohead adapter for RF95 style radios
 */
class RF95Interface : public RadioLibInterface
{
    RadioLibRF95 *lora = NULL; // Either a RFM95 or RFM96 depending on what was stuffed on this board

  public:
    RF95Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                  RADIOLIB_PIN_TYPE busy);

    // TODO: Verify that this irq flag works with RFM95 / SX1276 radios the way it used to
    bool isIRQPending() override { return lora->getIRQFlags() & RADIOLIB_SX127X_MASK_IRQ_FLAG_VALID_HEADER; }

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

  protected:
    /**
     * Glue functions called from ISR land
     */
    virtual void disableInterrupt() override;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*callback)()) { lora->setDio0Action(callback, RISING); }

    /** can we detect a LoRa preamble on the current channel? */
    virtual bool isChannelActive() override;

    /** are we actively receiving a packet (only called during receiving state) */
    virtual bool isActivelyReceiving() override;

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive() override;

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(meshtastic_MeshPacket *mp) override;

    virtual void setStandby() override;

    /**
     *  We override to turn on transmitter power as needed.
     */
    virtual void configHardwareForSend() override;

  private:
    /** Some boards require GPIO control of tx vs rx paths */
    void setTransmitEnable(bool txon);
};
#endif