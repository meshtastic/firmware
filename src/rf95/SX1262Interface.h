#pragma once

#include "RadioLibInterface.h"

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

  protected:
    /**
     * Glue functions called from ISR land
     */
    virtual void INTERRUPT_ATTR disableInterrupt() { lora.clearDio1Action(); }

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*callback)()) { lora.setDio1Action(callback); }

    /** Could we send right now (i.e. either not actively receiving or transmitting)? */
    virtual bool canSendImmediately();

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive();
};