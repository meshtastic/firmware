#pragma once

#include "RadioInterface.h"

#include <RadioLib.h>

class RadioLibInterface : public RadioInterface
{
    Module module; // The HW interface to the radio
    SX1262 lora;

  public:
    RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);

    virtual ErrorCode send(MeshPacket *p);

    // methods from radiohead

    /// Sets the address of this node. Defaults to 0xFF. Subclasses or the user may want to change this.
    /// This will be used to test the adddress in incoming messages. In non-promiscuous mode,
    /// only messages with a TO header the same as thisAddress or the broadcast addess (0xFF) will be accepted.
    /// In promiscuous mode, all messages will be accepted regardless of the TO header.
    /// In a conventional multinode system, all nodes will have a unique address
    /// (which you could store in EEPROM).
    /// You would normally set the header FROM address to be the same as thisAddress (though you dont have to,
    /// allowing the possibilty of address spoofing).
    /// \param[in] thisAddress The address of this node.
    virtual void setThisAddress(uint8_t thisAddress) {}

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init();

    /// Sets the transmitter and receiver
    /// centre frequency.
    /// \param[in] centre Frequency in MHz. 137.0 to 1020.0. Caution: RFM95/96/97/98 comes in several
    /// different frequency ranges, and setting a frequency outside that range of your radio will probably not work
    /// \return true if the selected frquency centre is within range
    bool setFrequency(float centre) { return true; }

    /// Select one of the predefined modem configurations. If you need a modem configuration not provided
    /// here, use setModemRegisters() with your own ModemConfig.
    /// Caution: the slowest protocols may require a radio module with TCXO temperature controlled oscillator
    /// for reliable operation.
    /// \param[in] index The configuration choice.
    /// \return true if index is a valid choice.
    bool setModemConfig(RH_RF95::ModemConfigChoice index) { return true; }

    /// If current mode is Rx or Tx changes it to Idle. If the transmitter or receiver is running,
    /// disables them.
    void setModeIdle() {}

    /// If current mode is Tx or Idle, changes it to Rx.
    /// Starts the receiver in the RF95/96/97/98.
    void setModeRx() {}

    /// Returns the operating mode of the library.
    /// \return the current mode, one of RF69_MODE_*
    virtual RHGenericDriver::RHMode mode() { return RHGenericDriver::RHModeIdle; }

    /// Sets the transmitter power output level, and configures the transmitter pin.
    /// Be a good neighbour and set the lowest power level you need.
    /// Some SX1276/77/78/79 and compatible modules (such as RFM95/96/97/98)
    /// use the PA_BOOST transmitter pin for high power output (and optionally the PA_DAC)
    /// while some (such as the Modtronix inAir4 and inAir9)
    /// use the RFO transmitter pin for lower power but higher efficiency.
    /// You must set the appropriate power level and useRFO argument for your module.
    /// Check with your module manufacturer which transmtter pin is used on your module
    /// to ensure you are setting useRFO correctly.
    /// Failure to do so will result in very low
    /// transmitter power output.
    /// Caution: legal power limits may apply in certain countries.
    /// After init(), the power will be set to 13dBm, with useRFO false (ie PA_BOOST enabled).
    /// \param[in] power Transmitter power level in dBm. For RFM95/96/97/98 LORA with useRFO false,
    /// valid values are from +5 to +23.
    /// For Modtronix inAir4 and inAir9 with useRFO true (ie RFO pins in use),
    /// valid values are from -1 to 14.
    /// \param[in] useRFO If true, enables the use of the RFO transmitter pins instead of
    /// the PA_BOOST pin (false). Choose the correct setting for your module.
    void setTxPower(int8_t power, bool useRFO = false) {}

  private:
    float freq = 915.0; // FIXME, init all these params from suer setings
    float bw = 125;
    uint8_t sf = 9;
    uint8_t cr = 7;
    uint8_t syncWord = 0; // FIXME, use a meshtastic sync word, but hashed with the Channel name
    int8_t power = 17;
    float currentLimit = 100; // FIXME
    uint16_t preambleLength = 8;
};