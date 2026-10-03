#pragma once

#include "MeshModule.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

// Is this serial config one we will accept? Outside the architecture guard below because it touches
// no serial hardware, and AdminModule must run it on every platform - including those where
// SerialModule itself does not exist. Logs and notifies the client on rejection.
bool serialConfigIsValid(const meshtastic_ModuleConfig_SerialConfig &config);

#if defined(USE_SERIAL_PACKET_IO)
#include "MeshPacketQueue.h"

#define MAX_TX_SERIAL_QUEUE 8

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&               \
    !defined(CONFIG_IDF_TARGET_ESP32C3)

typedef struct _SerialPacketHeader {
    uint8_t hbyte1;
    uint8_t hbyte2;
    uint16_t size; // this is size of header + payload length
    uint32_t crc;
    NodeNum to, from; // can be 1 byte or four bytes
    PacketId id;      // can be 1 byte or 4 bytes

    /**
     * Usage of flags:
     *
     * The new implemenentation hardcodes old hop_start=1, hop_limit=0
     **/
    uint8_t flags;

    /** The channel hash - used as a hint for the decoder to limit which channels we consider */
    uint8_t channel;

    uint8_t hop_limit; // new place for hop_limit

    uint8_t hop_start; // new place for hop_start

} SerialPacketHeader;

typedef struct _meshtastic_serialPacket {
    SerialPacketHeader header;
    uint8_t payload[256]; // 256 is max payload size
} meshtastic_serialPacket;

class SerialModule : public StreamAPI, private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long lastNmeaTime = millis();
    char outbuf[90] = "";
    int lastBufferCount = 0;

  public:
    SerialModule();
    static bool isValidConfig(const meshtastic_ModuleConfig_SerialConfig &config);

  protected:
    virtual int32_t runOnce() override;

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;

  private:
    uint32_t getBaudRate();
};

extern SerialModule *serialModule;

/*
 * Radio interface for SerialModule
 *
 */
class SerialModuleRadio : public MeshModule
{
    uint32_t lastRxID = 0;
    uint16_t txDrop = 0;
    MeshPacketQueue txQueue = MeshPacketQueue(MAX_TX_SERIAL_QUEUE);

  public:
    SerialModuleRadio();
    void onSend(meshtastic_MeshPacket *p);
    void checkTxQueue();

  protected:
    virtual meshtastic_MeshPacket *allocReply() override;

    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    meshtastic_PortNum ourPortNum;

    // virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

    meshtastic_MeshPacket *allocDataPacket()
    {
        // Update our local node info with our position (even if we don't decide to update anyone else)
        meshtastic_MeshPacket *p = router->allocForSending();
        p->decoded.portnum = ourPortNum;

        return p;
    }

  private:
    void sendPacketOverSerial(meshtastic_MeshPacket *p);
};

extern SerialModuleRadio *serialModuleRadio;

#endif

#else

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040) || defined(ARCH_STM32WL)) &&                             \
    !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32C3)

class SerialModule : public StreamAPI, private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long lastNmeaTime = millis();
    char outbuf[90] = "";

  public:
    SerialModule();

  protected:
    virtual int32_t runOnce() override;

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override;

  private:
    uint32_t getBaudRate();
    void sendTelemetry(meshtastic_Telemetry m);
    void processWXSerial();
};

extern SerialModule *serialModule;

/*
 * Radio interface for SerialModule
 *
 */
class SerialModuleRadio : public SinglePortModule
{
    uint32_t lastRxID = 0;
    char outbuf[90] = "";

  public:
    SerialModuleRadio();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern SerialModuleRadio *serialModuleRadio;

#endif
#endif
