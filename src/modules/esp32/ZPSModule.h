#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "gps/RTC.h"

#define ZPS_PORTNUM meshtastic_PortNum_ZPS_APP

#define ZPS_DATAPKT_MAXITEMS 20 // max number of records to pack in an outbound packet (~10)
#define ZPS_STARTUP_DELAY 10000 // Module startup delay in millis

// Duration of a BLE scan in millis.
// We want this number to be SLIGHTLY UNDER an integer number of seconds,
//  to be able to catch the result as fresh as possible on a 1-second polling loop
#define ZPS_BLE_SCANTIME 2900 // millis

enum SCANSTATE { SCAN_NONE, SCAN_BSS_RUN, SCAN_BSS_DONE, SCAN_BLE_RUN, SCAN_BLE_DONE };

/*
 * Data packing "compression" functions
 * Ingest a WiFi BSSID, channel and RSSI (or BLE address and RSSI)
 *   and encode them into a packed uint64
 */
uint64_t encodeBSS(uint8_t *bssid, uint8_t chan, uint8_t absRSSI);
uint64_t encodeBLE(uint8_t *addr, uint8_t absRSSI);

class ZPSModule : public SinglePortModule, private concurrency::OSThread
{
    /// The id of the last packet we sent, to allow us to cancel it if we make something fresher
    PacketId prevPacketId = 0;

    /// We limit our broadcasts to a max rate
    uint32_t lastSend = 0;

    bool wantBSS = true;
    bool haveBSS = false;

    bool wantBLE = true;
    bool haveBLE = false;

  public:
    /** Constructor
     * name is for debugging output
     */
    ZPSModule();

    /**
     * Send our radio environment data into the mesh
     */
    void sendDataPacket(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp);

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual meshtastic_MeshPacket *allocReply();

    /** Does our periodic broadcast */
    virtual int32_t runOnce();

  private:
    // outbound data packet staging buffer and record counter
    uint64_t netData[ZPS_DATAPKT_MAXITEMS + 2];
    uint8_t netRecs = 0;

    // mini state machine to alternate between BSS(Wifi) and BLE scanning
    SCANSTATE scanState = SCAN_NONE;

    inline void outBufAdd(uint64_t netBytes)
    {
        // is this first record? then initialize header
        // this is SO BAD that it's incorrect even by our inexistent standards
        if (!netRecs) {
            netData[0] = getTime();
            netData[1] = 0;
        }

        // push to buffer and update counter
        if (netRecs < ZPS_DATAPKT_MAXITEMS)
            netData[2 + (netRecs++)] = netBytes;
    }
};

extern ZPSModule *zpsModule;