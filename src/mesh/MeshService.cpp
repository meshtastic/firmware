
#include <Arduino.h>
#include <assert.h>
#include <string>

#include "GPS.h"
//#include "MeshBluetoothService.h"
#include "../concurrency/Periodic.h"
#include "BluetoothCommon.h" // needed for updateBatteryLevel, FIXME, eventually when we pull mesh out into a lib we shouldn't be whacking bluetooth from here
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "plugins/PositionPlugin.h"
#include "plugins/NodeInfoPlugin.h"
#include "power.h"

/*
receivedPacketQueue - this is a queue of messages we've received from the mesh, which we are keeping to deliver to the phone.
It is implemented with a FreeRTos queue (wrapped with a little RTQueue class) of pointers to MeshPacket protobufs (which were
alloced with new). After a packet ptr is removed from the queue and processed it should be deleted.  (eventually we should move
sent packets into a 'sentToPhone' queue of packets we can delete just as soon as we are sure the phone has acked those packets -
when the phone writes to FromNum)

mesh - an instance of Mesh class.  Which manages the interface to the mesh radio library, reception of packets from other nodes,
arbitrating to select a node number and keeping the current nodedb.

*/

/* Broadcast when a newly powered mesh node wants to find a node num it can use

The algoritm is as follows:
* when a node starts up, it broadcasts their user and the normal flow is for all other nodes to reply with their User as well (so
the new node can build its node db)
* If a node ever receives a User (not just the first broadcast) message where the sender node number equals our node number, that
indicates a collision has occurred and the following steps should happen:

If the receiving node (that was already in the mesh)'s macaddr is LOWER than the new User who just tried to sign in: it gets to
keep its nodenum.  We send a broadcast message of OUR User (we use a broadcast so that the other node can receive our message,
considering we have the same id - it also serves to let observers correct their nodedb) - this case is rare so it should be okay.

If any node receives a User where the macaddr is GTE than their local macaddr, they have been vetoed and should pick a new random
nodenum (filtering against whatever it knows about the nodedb) and rebroadcast their User.

FIXME in the initial proof of concept we just skip the entire want/deny flow and just hand pick node numbers at first.
*/

MeshService service;

#include "Router.h"

static int32_t sendOwnerCb()
{
    nodeInfoPlugin.sendOurNodeInfo();

    return getPref_send_owner_interval() * getPref_position_broadcast_secs() * 1000;
}

static concurrency::Periodic *sendOwnerPeriod;

MeshService::MeshService() : toPhoneQueue(MAX_RX_TOPHONE)
{
    // assert(MAX_RX_TOPHONE == 32); // FIXME, delete this, just checking my clever macro
}

void MeshService::init()
{
    sendOwnerPeriod = new concurrency::Periodic("SendOwner", sendOwnerCb);

    nodeDB.init();

    if (gps)
        gpsObserver.observe(&gps->newStatus);
    packetReceivedObserver.observe(&router->notifyPacketReceived);
}


int MeshService::handleFromRadio(const MeshPacket *mp)
{
    powerFSM.trigger(EVENT_RECEIVED_PACKET); // Possibly keep the node from sleeping

    printPacket("Forwarding to phone", mp);
    nodeDB.updateFrom(*mp); // update our DB state based off sniffing every RX packet from the radio

    fromNum++;

    if (toPhoneQueue.numFree() == 0) {
        DEBUG_MSG("NOTE: tophone queue is full, discarding oldest\n");
        MeshPacket *d = toPhoneQueue.dequeuePtr(0);
        if (d)
            releaseToPool(d);
    }

    MeshPacket *copied = packetPool.allocCopy(*mp);
    assert(toPhoneQueue.enqueue(copied, 0)); // FIXME, instead of failing for full queue, delete the oldest mssages

    if (mp->decoded.want_response)
        sendNetworkPing(mp->from);

    return 0;
}

/// Do idle processing (mostly processing messages which have been queued from the radio)
void MeshService::loop()
{
    if (oldFromNum != fromNum) { // We don't want to generate extra notifies for multiple new packets
        fromNumChanged.notifyObservers(fromNum);
        oldFromNum = fromNum;
    }
}

/// The radioConfig object just changed, call this to force the hw to change to the new settings
bool MeshService::reloadConfig()
{
    // If we can successfully set this radio to these settings, save them to disk

    // This will also update the region as needed
    bool didReset = nodeDB.resetRadioConfig(); // Don't let the phone send us fatally bad settings

    configChanged.notifyObservers(NULL);
    nodeDB.saveToDisk();

    return didReset;
}

/// The owner User record just got updated, update our node DB and broadcast the info into the mesh
void MeshService::reloadOwner()
{
    nodeInfoPlugin.sendOurNodeInfo();
    nodeDB.saveToDisk();
}

/**
 *  Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
 * Called by PhoneAPI.handleToRadio.  Note: p is a scratch buffer, this function is allowed to write to it but it can not keep a
 * reference
 */
void MeshService::handleToRadio(MeshPacket &p)
{
    if (p.from == 0) // If the phone didn't set a sending node ID, use ours
        p.from = nodeDB.getNodeNum();

    if (p.id == 0)
        p.id = generatePacketId(); // If the phone didn't supply one, then pick one

    p.rx_time = getValidTime(RTCQualityFromNet); // Record the time the packet arrived from the phone
                                                 // (so we update our nodedb for the local node)

    // Send the packet into the mesh

    sendToMesh(packetPool.allocCopy(p));

    bool loopback = false; // if true send any packet the phone sends back itself (for testing)
    if (loopback) {
        // no need to copy anymore because handle from radio assumes it should _not_ delete
        // packetPool.allocCopy(r.variant.packet);
        handleFromRadio(&p);
        // handleFromRadio will tell the phone a new packet arrived
    }
}

void MeshService::sendToMesh(MeshPacket *p)
{
    nodeDB.updateFrom(*p); // update our local DB for this packet (because phone might have sent position packets etc...)

    // Strip out any time information before sending packets to other  nodes - to keep the wire size small (and because other
    // nodes shouldn't trust it anyways) Note: we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (p->which_payload == MeshPacket_decoded_tag && p->decoded.which_payload == SubPacket_position_tag &&
        p->decoded.position.time) {
        if (getRTCQuality() < RTCQualityGPS) {
            DEBUG_MSG("Stripping time %u from position send\n", p->decoded.position.time);
            p->decoded.position.time = 0;
        } else
            DEBUG_MSG("Providing time to mesh %u\n", p->decoded.position.time);
    }

    // Note: We might return !OK if our fifo was full, at that point the only option we have is to drop it
    router->sendLocal(p);
}

void MeshService::sendNetworkPing(NodeNum dest, bool wantReplies)
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
    assert(node);

    DEBUG_MSG("Sending network ping to 0x%x, with position=%d, wantReplies=%d\n", dest, node->has_position, wantReplies);
    if (node->has_position)
        positionPlugin.sendOurPosition(dest, wantReplies);
    else
        nodeInfoPlugin.sendOurNodeInfo(dest, wantReplies);
}

int MeshService::onGPSChanged(const meshtastic::GPSStatus *unused)
{
    // Update our local node info with our position (even if we don't decide to update anyone else)

    Position pos;

    memset(&pos, 0, sizeof(pos));

    if (gps->hasLock()) {
        if (gps->altitude != 0)
            pos.altitude = gps->altitude;
        pos.latitude_i = gps->latitude;
        pos.longitude_i = gps->longitude;
    }

    pos.time = getValidTime(RTCQualityGPS);

    // Include our current battery voltage in our position announcement
    pos.battery_level = powerStatus->getBatteryChargePercent();
    updateBatteryLevel(pos.battery_level);

    // DEBUG_MSG("got gps notify time=%u, lat=%d, bat=%d\n", pos.latitude_i, pos.time, pos.battery_level);

    // Update our current position in the local DB
    nodeDB.updatePosition(nodeDB.getNodeNum(), pos);

    // We limit our GPS broadcasts to a max rate
    static uint32_t lastGpsSend;
    uint32_t now = millis();
    if (lastGpsSend == 0 || now - lastGpsSend > getPref_position_broadcast_secs() * 1000) {
        lastGpsSend = now;
        DEBUG_MSG("Sending position to mesh (not requesting replies)\n");
        positionPlugin.sendOurPosition();
    }

    return 0;
}
