#pragma once

#include "Observer.h"
#include "mesh-pb-constants.h"
#include "mesh.pb.h"
#include <string>

// Make sure that we never let our packets grow too large for one BLE packet
#define MAX_TO_FROM_RADIO_SIZE 512

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
        STATE_LEGACY,       // Temporary default state - until Android apps are all updated, uses the old BLE API
        STATE_SEND_NOTHING, // (Eventual) Initial state, don't send anything until the client starts asking for config
        STATE_SEND_MY_INFO, // send our my info record
        STATE_SEND_RADIO,
        // STATE_SEND_OWNER, no need to send Owner specially, it is just part of the nodedb
        STATE_SEND_NODEINFO, // states progress in this order as the device sends to to the client
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // send packets or debug strings
    };

    State state = STATE_LEGACY;

    /**
     * Each packet sent to the phone has an incrementing count
     */
    uint32_t fromRadioNum = 0;

    /// We temporarily keep the packet here between the call to available and getFromRadio.  We will free it after the phone
    /// downloads it
    MeshPacket *packetForPhone = NULL;

    /// We temporarily keep the nodeInfo here between the call to available and getFromRadio
    const NodeInfo *nodeInfoForPhone = NULL;

    ToRadio toRadioScratch; // this is a static scratch object, any data must be copied elsewhere before returning

    /// Use to ensure that clients don't get confused about old messages from the radio
    uint32_t config_nonce = 0;

    /** the last msec we heard from the client on the other side of this link */
    uint32_t lastContactMsec = 0;

    bool isConnected = false;

  public:
    PhoneAPI();

    /// Do late init that can't happen at constructor time
    virtual void init();

    /**
     * Handle a ToRadio protobuf
     */
    virtual void handleToRadio(const uint8_t *buf, size_t len);

    /**
     * Get the next packet we want to send to the phone
     *
     * We assume buf is at least FromRadio_size bytes long.
     * Returns number of bytes in the FromRadio packet (or 0 if no packet available)
     */
    size_t getFromRadio(uint8_t *buf);

    /**
     * Return true if we have data available to send to the phone
     */
    bool available();

    //
    // The following routines are only public for now - until the rev1 bluetooth API is removed
    //

    void handleSetOwner(const User &o);
    void handleSetRadio(const RadioConfig &r);

  protected:
    /// Our fromradio packet while it is being assembled
    FromRadio fromRadioScratch;

    /// Hookable to find out when connection changes
    virtual void onConnectionChanged(bool connected) {}

  /// If we haven't heard from the other side in a while then say not connected
    void checkConnectionTimeout();

    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) {}

  private:
    /**
     * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
     */
    void handleToRadioPacket(MeshPacket *p);

    /// If the mesh service tells us fromNum has changed, tell the phone
    virtual int onNotify(uint32_t newValue);
};
