
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
#include "main.h"
#include "mesh-pb-constants.h"
#include "power.h"
#include "timing.h"

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

static uint32_t sendOwnerCb()
{
    service.sendOurOwner();

    return radioConfig.preferences.send_owner_interval * radioConfig.preferences.position_broadcast_secs * 1000;
}

static concurrency::Periodic sendOwnerPeriod(sendOwnerCb);

MeshService::MeshService() : toPhoneQueue(MAX_RX_TOPHONE)
{
    // assert(MAX_RX_TOPHONE == 32); // FIXME, delete this, just checking my clever macro
}

void MeshService::init()
{
    sendOwnerPeriod.setup();
    nodeDB.init();

    gpsObserver.observe(gps);
    packetReceivedObserver.observe(&router.notifyPacketReceived);
}

void MeshService::sendOurOwner(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = router.allocForSending();
    p->to = dest;
    p->decoded.want_response = wantReplies;
    p->decoded.which_payload = SubPacket_user_tag;
    User &u = p->decoded.user;
    u = owner;
    DEBUG_MSG("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);

    sendToMesh(p);
}

/// handle a user packet that just arrived on the radio, return NULL if we should not process this packet at all
const MeshPacket *MeshService::handleFromRadioUser(const MeshPacket *mp)
{
    bool wasBroadcast = mp->to == NODENUM_BROADCAST;

    // Disable this collision testing if we use 32 bit nodenums
    bool isCollision = (sizeof(NodeNum) == 1) && (mp->from == myNodeInfo.my_node_num);

    if (isCollision) {
        // we win if we have a lower macaddr
        bool weWin = memcmp(&owner.macaddr, &mp->decoded.user.macaddr, sizeof(owner.macaddr)) < 0;

        if (weWin) {
            DEBUG_MSG("NOTE! Received a nodenum collision and we are vetoing\n");

            mp = NULL;

            sendOurOwner(); // send our owner as a _broadcast_ because that other guy is mistakenly using our nodenum
        } else {
            // we lost, we need to try for a new nodenum!
            DEBUG_MSG("NOTE! Received a nodenum collision we lost, so picking a new nodenum\n");
            nodeDB.updateFrom(
                *mp); // update the DB early - before trying to repick (so we don't select the same node number again)
            nodeDB.pickNewNodeNum();
            sendOurOwner(); // broadcast our new attempt at a node number
        }
    } else if (wasBroadcast) {
        // If we haven't yet abandoned the packet and it was a broadcast, reply (just to them) with our User record so they can
        // build their DB

        // Someone just sent us a User, reply with our Owner
        DEBUG_MSG("Received broadcast Owner from 0x%x, replying with our owner\n", mp->from);

        sendOurOwner(mp->from);

        String lcd = String("Joined: ") + mp->decoded.user.long_name + "\n";
        screen.print(lcd.c_str());
    }

    return mp;
}

void MeshService::handleIncomingPosition(const MeshPacket *mp)
{
    if (mp->which_payload == MeshPacket_decoded_tag && mp->decoded.which_payload == SubPacket_position_tag) {
        DEBUG_MSG("handled incoming position time=%u\n", mp->decoded.position.time);

        if (mp->decoded.position.time) {
            struct timeval tv;
            uint32_t secs = mp->decoded.position.time;

            tv.tv_sec = secs;
            tv.tv_usec = 0;

            perhapsSetRTC(&tv);
        }
    } else {
        DEBUG_MSG("Ignoring incoming packet - not a position\n");
    }
}

int MeshService::handleFromRadio(const MeshPacket *mp)
{
    powerFSM.trigger(EVENT_RECEIVED_PACKET); // Possibly keep the node from sleeping

    // If it is a position packet, perhaps set our clock (if we don't have a GPS of our own, otherwise wait for that to work)
    if (!gps->isConnected)
        handleIncomingPosition(mp);
    else {
        DEBUG_MSG("Ignoring incoming time, because we have a GPS\n");
    }

    if (mp->which_payload == MeshPacket_decoded_tag && mp->decoded.which_payload == SubPacket_user_tag) {
        mp = handleFromRadioUser(mp);
    }

    // If we veto a received User packet, we don't put it into the DB or forward it to the phone (to prevent confusing it)
    if (mp) {
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
    } else {
        DEBUG_MSG("Not delivering vetoed User message\n");
    }

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
void MeshService::reloadConfig()
{
    // If we can successfully set this radio to these settings, save them to disk
    nodeDB.resetRadioConfig(); // Don't let the phone send us fatally bad settings
    configChanged.notifyObservers(NULL);
    nodeDB.saveToDisk();
}

/**
 *  Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
 * Called by PhoneAPI.handleToRadio.  Note: p is a scratch buffer, this function is allowed to write to it but it can not keep a
 * reference
 */
void MeshService::handleToRadio(MeshPacket &p)
{
    handleIncomingPosition(&p); // If it is a position packet, perhaps set our clock

    if (p.from == 0) // If the phone didn't set a sending node ID, use ours
        p.from = nodeDB.getNodeNum();

    if (p.id == 0)
        p.id = generatePacketId(); // If the phone didn't supply one, then pick one

    p.rx_time = getValidTime(); // Record the time the packet arrived from the phone
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
    // nodes shouldn't trust it anyways) Note: for now, we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (p->which_payload == MeshPacket_decoded_tag && p->decoded.which_payload == SubPacket_position_tag) {
        if (!gps->isConnected) {
            DEBUG_MSG("Stripping time %u from position send\n", p->decoded.position.time);
            p->decoded.position.time = 0;
        } else
            DEBUG_MSG("Providing time to mesh %u\n", p->decoded.position.time);
    }

    // Note: We might return !OK if our fifo was full, at that point the only option we have is to drop it
    router.sendLocal(p);
}

void MeshService::sendNetworkPing(NodeNum dest, bool wantReplies)
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
    assert(node);

    DEBUG_MSG("Sending network ping to 0x%x, with position=%d, wantReplies=%d\n", dest, node->has_position, wantReplies);
    if (node->has_position)
        sendOurPosition(dest, wantReplies);
    else
        sendOurOwner(dest, wantReplies);
}

void MeshService::sendOurPosition(NodeNum dest, bool wantReplies)
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
    assert(node);
    assert(node->has_position);

    // Update our local node info with our position (even if we don't decide to update anyone else)
    MeshPacket *p = router.allocForSending();
    p->to = dest;
    p->decoded.which_payload = SubPacket_position_tag;
    p->decoded.position = node->position;
    p->decoded.want_response = wantReplies;
    p->decoded.position.time = getValidTime(); // This nodedb timestamp might be stale, so update it if our clock is valid.
    sendToMesh(p);
}

int MeshService::onGPSChanged(void *unused)
{
    // DEBUG_MSG("got gps notify\n");

    // Update our local node info with our position (even if we don't decide to update anyone else)
    MeshPacket *p = router.allocForSending();
    p->decoded.which_payload = SubPacket_position_tag;

    Position &pos = p->decoded.position;
    // !zero or !zero lat/long means valid
    if (gps->latitude != 0 || gps->longitude != 0) {
        if (gps->altitude != 0)
            pos.altitude = gps->altitude;
        pos.latitude_i = gps->latitude;
        pos.longitude_i = gps->longitude;
        pos.time = getValidTime();
    }

    // Include our current battery voltage in our position announcement
    pos.battery_level = powerStatus->getBatteryChargePercent();
    updateBatteryLevel(pos.battery_level);

    // We limit our GPS broadcasts to a max rate
    static uint32_t lastGpsSend;
    uint32_t now = timing::millis();
    if (lastGpsSend == 0 || now - lastGpsSend > radioConfig.preferences.position_broadcast_secs * 1000) {
        lastGpsSend = now;
        DEBUG_MSG("Sending position to mesh\n");

        sendToMesh(p);
    } else {
        // We don't need to send this packet to anyone else, but it still serves as a nice uniform way to update our local state
        nodeDB.updateFrom(*p);

        releaseToPool(p);
    }

    return 0;
}
