#pragma once

#include "Observer.h"
#include "mesh-pb-constants.h"
#include <iterator>
#include <string>
#include <vector>

// Make sure that we never let our packets grow too large for one BLE packet
#define MAX_TO_FROM_RADIO_SIZE 512

#if meshtastic_FromRadio_size > MAX_TO_FROM_RADIO_SIZE
#error "meshtastic_FromRadio_size is too large for our BLE packets"
#endif
#if meshtastic_ToRadio_size > MAX_TO_FROM_RADIO_SIZE
#error "meshtastic_ToRadio_size is too large for our BLE packets"
#endif

#define SPECIAL_NONCE 69420

/**
 * Provides our protobuf based API which phone/PC clients can use to talk to our device
 * over UDP, bluetooth or serial.
 *
 * Subclass to customize behavior for particular type of transport (BLE, UDP, TCP, serial)
 *
 * Eventually there should be once instance of this class for each live connection (because it has a bit of state
 * for that connection)
 */
class PhoneAPI
    : public Observer<uint32_t> // FIXME, we shouldn't be inheriting from Observer, instead use CallbackObserver as a member
{
    enum State {
        STATE_SEND_NOTHING, // Initial state, don't send anything until the client starts asking for config
        STATE_SEND_MY_INFO, // send our my info record
        STATE_SEND_OWN_NODEINFO,
        STATE_SEND_METADATA,
        STATE_SEND_CHANNELS,        // Send all channels
        STATE_SEND_CONFIG,          // Replacement for the old Radioconfig
        STATE_SEND_MODULECONFIG,    // Send Module specific config
        STATE_SEND_OTHER_NODEINFOS, // states progress in this order as the device sends to to the client
        STATE_SEND_FILEMANIFEST,    // Send file manifest
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // send packets or debug strings
    };

    State state = STATE_SEND_NOTHING;

    uint8_t config_state = 0;

    /**
     * Each packet sent to the phone has an incrementing count
     */
    uint32_t fromRadioNum = 0;

    /// We temporarily keep the packet here between the call to available and getFromRadio.  We will free it after the phone
    /// downloads it
    meshtastic_MeshPacket *packetForPhone = NULL;

    // file transfer packets destined for phone. Push it to the queue then free it.
    meshtastic_XModem xmodemPacketForPhone = meshtastic_XModem_init_zero;

    // Keep QueueStatus packet just as packetForPhone
    meshtastic_QueueStatus *queueStatusPacketForPhone = NULL;

    // Keep MqttClientProxyMessage packet just as packetForPhone
    meshtastic_MqttClientProxyMessage *mqttClientProxyMessageForPhone = NULL;

    /// We temporarily keep the nodeInfo here between the call to available and getFromRadio
    meshtastic_NodeInfo nodeInfoForPhone = meshtastic_NodeInfo_init_default;

    meshtastic_ToRadio toRadioScratch = {
        0}; // this is a static scratch object, any data must be copied elsewhere before returning

    /// Use to ensure that clients don't get confused about old messages from the radio
    uint32_t config_nonce = 0;
    uint32_t readIndex = 0;

    std::vector<meshtastic_FileInfo> filesManifest = {};

    void resetReadIndex() { readIndex = 0; }

  public:
    PhoneAPI();

    /// Destructor - calls close()
    virtual ~PhoneAPI();

    // Call this when the client drops the connection, resets the state to STATE_SEND_NOTHING
    // Unregisters our observer.  A closed connection **can** be reopened by calling init again.
    virtual void close();

    /**
     * Handle a ToRadio protobuf
     * @return true true if a packet was queued for sending (so that caller can yield)
     */
    virtual bool handleToRadio(const uint8_t *buf, size_t len);

    /**
     * Get the next packet we want to send to the phone
     *
     * We assume buf is at least FromRadio_size bytes long.
     * Returns number of bytes in the FromRadio packet (or 0 if no packet available)
     */
    size_t getFromRadio(uint8_t *buf);

    void sendConfigComplete();

    /**
     * Return true if we have data available to send to the phone
     */
    bool available();

    bool isConnected() { return state != STATE_SEND_NOTHING; }

  protected:
    /// Our fromradio packet while it is being assembled
    meshtastic_FromRadio fromRadioScratch = {};

    /** the last msec we heard from the client on the other side of this link */
    uint32_t lastContactMsec = 0;

    /// Hookable to find out when connection changes
    virtual void onConnectionChanged(bool connected) {}

    /// If we haven't heard from the other side in a while then say not connected. Returns true if timeout occurred
    bool checkConnectionTimeout();

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() = 0;

    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) {}

    /**
     * Subclasses can use this to find out when a client drops the link
     */
    virtual void handleDisconnect();

  private:
    void releasePhonePacket();

    void releaseQueueStatusPhonePacket();

    void releaseMqttClientProxyPhonePacket();

    /// begin a new connection
    void handleStartConfig();

    /**
     * Handle a packet that the phone wants us to send.  We can write to it but can not keep a reference to it
     * @return true true if a packet was queued for sending
     */
    bool handleToRadioPacket(meshtastic_MeshPacket &p);

    /// If the mesh service tells us fromNum has changed, tell the phone
    virtual int onNotify(uint32_t newValue) override;
};