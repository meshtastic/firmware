#pragma once

#include "../concurrency/OSThread.h"
#include "RadioInterface.h"
#include "MeshPacketQueue.h"

#define RADIOLIB_EXCLUDE_HTTP
#include <RadioLib.h>

// ESP32 has special rules about ISR code
#ifdef ARDUINO_ARCH_ESP32
#define INTERRUPT_ATTR IRAM_ATTR
#else
#define INTERRUPT_ATTR
#endif

/**
 * A wrapper for the RadioLib Module class, that adds mutex for SPI bus access
 */
class LockingModule : public Module
{
  public:
    /*!
      \brief Extended SPI-based module constructor.

      \param cs Arduino pin to be used as chip select.

      \param irq Arduino pin to be used as interrupt/GPIO.

      \param rst Arduino pin to be used as hardware reset for the module.

      \param gpio Arduino pin to be used as additional interrupt/GPIO.

      \param spi SPI interface to be used, can also use software SPI implementations.

      \param spiSettings SPI interface settings.
    */
    LockingModule(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE gpio, SPIClass &spi,
                  SPISettings spiSettings)
        : Module(cs, irq, rst, gpio, spi, spiSettings)
    {
    }
    
#ifdef ARCH_PORTDUINO
    void SPItransfer(uint8_t cmd, uint8_t reg, uint8_t *dataOut, uint8_t *dataIn, uint8_t numBytes) override;
#else
    void SPIbeginTransaction() override;
    void SPIendTransaction() override;
#endif
};

class RadioLibInterface : public RadioInterface, protected concurrency::NotifiedWorkerThread
{
    /// Used as our notification from the ISR
    enum PendingISR { ISR_NONE = 0, ISR_RX, ISR_TX, TRANSMIT_DELAY_COMPLETED };

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrTxLevel0(), isrLevel0Common(PendingISR code);

    /**
     * Debugging counts
     */
    uint32_t rxBad = 0, rxGood = 0, txGood = 0;

    MeshPacketQueue txQueue = MeshPacketQueue(MAX_TX_QUEUE);

  protected:

    /**
     * We use a meshtastic sync word, but hashed with the Channel name.  For releases before 1.2 we used 0x12 (or for very old loads 0x14)
     * Note: do not use 0x34 - that is reserved for lorawan
     * 
     * We now use 0x2b (so that someday we can possibly use NOT 2b - because that would be funny pun).  We will be staying with this code
     * for a long time.
     */
    const uint8_t syncWord = 0x2b;
    
    float currentLimit = 100;   // 100mA OCP - Should be acceptable for RFM95/SX127x chipset.  

    LockingModule module; // The HW interface to the radio

    /**
     * provides lowest common denominator RadioLib API
     */
    PhysicalLayer *iface;

    /// are _trying_ to receive a packet currently (note - we might just be waiting for one)
    bool isReceiving = false;

  public:
    /** Our ISR code currently needs this to find our active instance
     */
    static RadioLibInterface *instance;

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
                      PhysicalLayer *iface = NULL);

    virtual ErrorCode send(MeshPacket *p) override;

    /**
     * Return true if we think the board can go to sleep (i.e. our tx queue is empty, we are not sending or receiving)
     *
     * This method must be used before putting the CPU into deep or light sleep.
     */
    virtual bool canSleep() override;

    /**
     * Start waiting to receive a message
     *
     * External functions can call this method to wake the device from sleep.
     */
    virtual void startReceive() = 0;

    /** can we detect a LoRa preamble on the current channel? */
    virtual bool isChannelActive() = 0;

    /** are we actively receiving a packet (only called during receiving state)
     *  This method is only public to facilitate debugging.  Do not call.
     */
    virtual bool isActivelyReceiving() = 0;

    /** Attempt to cancel a previously sent packet.  Returns true if a packet was found we could cancel */
    virtual bool cancelSending(NodeNum from, PacketId id) override;

  private:
    /** if we have something waiting to send, start a short (random) timer so we can come check for collision before actually doing
     * the transmit */
    void setTransmitDelay();

    /** random timer with certain min. and max. settings */
    void startTransmitTimer(bool withDelay = true);

    /** timer scaled to SNR of to be flooded packet */
    void startTransmitTimerSNR(float snr);

    void handleTransmitInterrupt();
    void handleReceiveInterrupt();

    static void timerCallback(void *p1, uint32_t p2);

    virtual void onNotify(uint32_t notification) override;

    /** start an immediate transmit
     *  This method is virtual so subclasses can hook as needed, subclasses should not call directly
     */
    virtual void startSend(MeshPacket *txp);

  protected:

    /** Do any hardware setup needed on entry into send configuration for the radio.  Subclasses can customize */
    virtual void configHardwareForSend() {}

    /** Could we send right now (i.e. either not actively receiving or transmitting)? */
    virtual bool canSendImmediately();

    /**
     * Raw ISR handler that just calls our polymorphic method
     */
    static void isrRxLevel0();

    /**
     * If a send was in progress finish it and return the buffer to the pool */
    void completeSending();

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(MeshPacket *mp) = 0;

    virtual void setStandby() = 0;
};
