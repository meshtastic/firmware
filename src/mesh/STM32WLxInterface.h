#pragma once

#include "RadioLibInterface.h"

template<class T>
class STM32WLxInterface : public RadioLibInterface
{
  public:
    STM32WLxInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi, const RADIOLIB_PIN_TYPE rfswitch_pins[3], const Module::RfSwitchMode_t rfswitch_table[4]);

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

    bool isIRQPending() override { return lora.getIrqStatus() != 0; }

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
    virtual void addReceiveMetadata(MeshPacket *mp) override;

    virtual void setStandby() override;

    float tcxoVoltage; 

  private:

    const RADIOLIB_PIN_TYPE rfswitch_pins[3] = {rfswitch_pins[0], rfswitch_pins[1], rfswitch_pins[2]};

    const Module::RfSwitchMode_t rfswitch_table[4] = {rfswitch_table[0], rfswitch_table[1], rfswitch_table[2], rfswitch_table[3]}; 
};