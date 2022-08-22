#include "configuration.h"
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
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "power.h"

#ifdef ARCH_ESP32
#include "nimble/NimbleBluetooth.h"
#endif

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

MeshService::MeshService() : toPhoneQueue(MAX_RX_TOPHONE)
{
    // assert(MAX_RX_TOPHONE == 32); // FIXME, delete this, just checking my clever macro
}

void MeshService::init()
{
    // moved much earlier in boot (called from setup())
    // nodeDB.init();

    if (gps)
        gpsObserver.observe(&gps->newStatus);
}

int MeshService::handleFromRadio(const MeshPacket *mp)
{
    powerFSM.trigger(EVENT_PACKET_FOR_PHONE); // Possibly keep the node from sleeping

    printPacket("Forwarding to phone", mp);
    nodeDB.updateFrom(*mp); // update our DB state based off sniffing every RX packet from the radio

    sendToPhone((MeshPacket *)mp);

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

    configChanged.notifyObservers(NULL); // This will cause radio hardware to change freqs etc
    nodeDB.saveToDisk();

    return didReset;
}

/// The owner User record just got updated, update our node DB and broadcast the info into the mesh
void MeshService::reloadOwner()
{
    // DEBUG_MSG("reloadOwner()\n");
    // update our local data directly
    nodeDB.updateUser(nodeDB.getNodeNum(), owner);
    assert(nodeInfoModule);
    // update everyone else
    if (nodeInfoModule)
        nodeInfoModule->sendOurNodeInfo();
    nodeDB.saveToDisk();
}

/**
 *  Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
 * Called by PhoneAPI.handleToRadio.  Note: p is a scratch buffer, this function is allowed to write to it but it can not keep a
 * reference
 */
void MeshService::handleToRadio(MeshPacket &p)
{
    if (p.from != 0) { // We don't let phones assign nodenums to their sent messages
        DEBUG_MSG("Warning: phone tried to pick a nodenum, we don't allow that.\n");
        p.from = 0;
    } else {
        // p.from = nodeDB.getNodeNum();
    }

    if (p.id == 0)
        p.id = generatePacketId(); // If the phone didn't supply one, then pick one

    p.rx_time = getValidTime(RTCQualityFromNet); // Record the time the packet arrived from the phone
                                                 // (so we update our nodedb for the local node)

    // Send the packet into the mesh

    sendToMesh(packetPool.allocCopy(p), RX_SRC_USER);

    bool loopback = false; // if true send any packet the phone sends back itself (for testing)
    if (loopback) {
        // no need to copy anymore because handle from radio assumes it should _not_ delete
        // packetPool.allocCopy(r.variant.packet);
        handleFromRadio(&p);
        // handleFromRadio will tell the phone a new packet arrived
    }
}

/** Attempt to cancel a previously sent packet from this _local_ node.  Returns true if a packet was found we could cancel */
bool MeshService::cancelSending(PacketId id)
{
    return router->cancelSending(nodeDB.getNodeNum(), id);
}

void MeshService::sendToMesh(MeshPacket *p, RxSource src, bool ccToPhone)
{
    nodeDB.updateFrom(*p); // update our local DB for this packet (because phone might have sent position packets etc...)

    // Note: We might return !OK if our fifo was full, at that point the only option we have is to drop it
    router->sendLocal(p, src);

    if (ccToPhone) {
        sendToPhone(p);
    }
}

void MeshService::sendNetworkPing(NodeNum dest, bool wantReplies)
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
    assert(node);

    if (node->has_position && (node->position.latitude_i != 0 || node->position.longitude_i != 0)) {
        if (positionModule) {
            DEBUG_MSG("Sending position ping to 0x%x, wantReplies=%d\n", dest, wantReplies);
            positionModule->sendOurPosition(dest, wantReplies);
        }
    } else {
        if (nodeInfoModule) {
            DEBUG_MSG("Sending nodeinfo ping to 0x%x, wantReplies=%d\n", dest, wantReplies);
            nodeInfoModule->sendOurNodeInfo(dest, wantReplies);
        }
    }
}

void MeshService::sendToPhone(MeshPacket *p)
{
    if (toPhoneQueue.numFree() == 0) {
        DEBUG_MSG("NOTE: tophone queue is full, discarding oldest\n");
        MeshPacket *d = toPhoneQueue.dequeuePtr(0);
        if (d)
            releaseToPool(d);
    }

    MeshPacket *copied = packetPool.allocCopy(*p);
    perhapsDecode(copied);
    assert(toPhoneQueue.enqueue(copied, 0)); // FIXME, instead of failing for full queue, delete the oldest mssages
    fromNum++;
}

NodeInfo *MeshService::refreshMyNodeInfo()
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
    assert(node);

    // We might not have a position yet for our local node, in that case, at least try to send the time
    if (!node->has_position) {
        memset(&node->position, 0, sizeof(node->position));
        node->has_position = true;
    }

    Position &position = node->position;

    // Update our local node info with our time (even if we don't decide to update anyone else)
    node->last_heard =
        getValidTime(RTCQualityFromNet); // This nodedb timestamp might be stale, so update it if our clock is kinda valid

    // For the time in the position field, only set that if we have a real GPS clock
    position.time = getValidTime(RTCQualityGPS);

    updateBatteryLevel(powerStatus->getBatteryChargePercent());

    return node;
}

int MeshService::onGPSChanged(const meshtastic::GPSStatus *newStatus)
{
    // Update our local node info with our position (even if we don't decide to update anyone else)
    NodeInfo *node = refreshMyNodeInfo();
    Position pos = Position_init_default;

    if (newStatus->getHasLock()) {
        // load data from GPS object, will add timestamp + battery further down
        pos = gps->p;
    } else {
        // The GPS has lost lock, if we are fixed position we should just keep using
        // the old position
#ifdef GPS_EXTRAVERBOSE
        DEBUG_MSG("onGPSchanged() - lost validLocation\n");
#endif
        if (config.position.fixed_position) {
            DEBUG_MSG("WARNING: Using fixed position\n");
            pos = node->position;
        }
    }

    // Finally add a fresh timestamp and battery level reading
    // I KNOW this is redundant with refreshMyNodeInfo() above, but these are
    //   inexpensive nonblocking calls and can be refactored in due course
    pos.time = getValidTime(RTCQualityGPS);

    // In debug logs, identify position by @timestamp:stage (stage 4 = nodeDB)
    DEBUG_MSG("onGPSChanged() pos@%x, time=%u, lat=%d, lon=%d, alt=%d\n", pos.pos_timestamp, pos.time, pos.latitude_i,
              pos.longitude_i, pos.altitude);

    // Update our current position in the local DB
    nodeDB.updatePosition(nodeDB.getNodeNum(), pos, RX_SRC_LOCAL);

    return 0;
}
