#pragma once

#include "mesh-pb-constants.h"
#include "mesh.pb.h"
#include <string>

/**
 * Provides our protobuf based API which phone/PC clients can use to talk to our device
 * over UDP, bluetooth or serial.
 *
 * Eventually there should be once instance of this class for each live connection (because it has a bit of state
 * for that connection)
 */
class PhoneAPI
{
    enum State {
        STATE_SEND_NOTHING, // Initial state, don't send anything until the client starts asking for config
        STATE_SEND_MY_NODEINFO,
        STATE_SEND_OWNER,
        STATE_SEND_RADIO,
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // send packets or debug strings
    };

    State state = STATE_SEND_NOTHING;

    /**
     * Each packet sent to the phone has an incrementing count
     */
    uint32_t fromRadioNum = 0; 

  public:
    PhoneAPI();

    /**
     * Handle a ToRadio protobuf
     */
    void handleToRadio(const char *buf, size_t len);

    /**
     * Get the next packet we want to send to the phone, or NULL if no such packet is available.
     *
     * We assume buf is at least FromRadio_size bytes long.
     */
    bool getFromRadio(char *buf);

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

    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    void onNowHasData(uint32_t fromRadioNum) {}

  private:
    /**
     * The client wants to start a new set of config reads
     */
    void handleWantConfig(uint32_t nonce);

    /**
     * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
     */
    void handleToRadioPacket(MeshPacket *p);
};
