#pragma once

#include "RadioInterface.h"

#include <RadioLib.h>

// ESP32 has special rules about ISR code
#ifdef ARDUINO_ARCH_ESP32
#define INTERRUPT_ATTR IRAM_ATTR
#else
#define INTERRUPT_ATTR
#endif

class RadioLibInterface : public RadioInterface
{
    enum PendingISR { ISR_NONE = 0, ISR_RX, ISR_TX };

    /**
     * What sort of interrupt do we expect our helper thread to now handle */
    volatile PendingISR pending = ISR_NONE;

    /** Our ISR code currently needs this to find our active instance
     */
    static RadioLibInterface *instance;

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrTxLevel0();

    /**
     * Debugging counts
     */
    uint32_t rxBad = 0, rxGood = 0, txGood = 0;

  protected:
    float bw = 125;
    uint8_t sf = 9;
    uint8_t cr = 7;

    /**
     * FIXME, use a meshtastic sync word, but hashed with the Channel name.  Currently picking the same default
     * the RF95 used (0x14). Note: do not use 0x34 - that is reserved for lorawan
     */
    uint8_t syncWord = SX126X_SYNC_WORD_PRIVATE;

    float currentLimit = 100;    // FIXME
    uint16_t preambleLength = 8; // 8 is default, but FIXME use longer to increase the amount of sleep time when receiving

    Module module; // The HW interface to the radio

    /**
     * provides lowest common denominator RadioLib API
     */
    PhysicalLayer &iface;

    /// are _trying_ to receive a packet currently (note - we might just be waiting for one)
    bool isReceiving;

    /**
     * Glue functions called from ISR land
     */
    virtual void disableInterrupt() = 0;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*)()) = 0;

  public:
    RadioLibInterface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi,
                      PhysicalLayer *iface);

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

    virtual void loop(); // Idle processing

    /**
     * Return true if we think the board can go to sleep (i.e. our tx queue is empty, we are not sending or receiving)
     *
     * This method must be used before putting the CPU into deep or light sleep.
     */
    virtual bool canSleep();

  private:
    /** start an immediate transmit */
    void startSend(MeshPacket *txp);

    /** start a queued transmit (if we have one), else start receiving */
    void startNextWork();

    void handleTransmitInterrupt();
    void handleReceiveInterrupt();

  protected:
    /**
     * Convert our modemConfig enum into wf, sf, etc...
     */
    void applyModemConfig();

    /** Could we send right now (i.e. either not actively receiving or transmitting)? */
    virtual bool canSendImmediately() = 0;

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive() = 0;

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrRxLevel0();

    /**
     * If a send was in progress finish it and return the buffer to the pool */
    void completeSending();
};