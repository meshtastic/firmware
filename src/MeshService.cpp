
#include <Arduino.h>
#include <assert.h>
#include <string>

#include "GPS.h"
//#include "MeshBluetoothService.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Periodic.h"
#include "PowerFSM.h"
#include "main.h"
#include "mesh-pb-constants.h"

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

#define NUM_PACKET_ID 255 // 0 is consider invalid

/// Generate a unique packet id
// FIXME, move this someplace better
PacketId generatePacketId()
{
    static uint32_t i;

    i++;
    return (i % NUM_PACKET_ID) + 1; // return number between 1 and 255
}

MeshService::MeshService() : toPhoneQueue(MAX_RX_TOPHONE)
{
    // assert(MAX_RX_TOPHONE == 32); // FIXME, delete this, just checking my clever macro
}

void MeshService::init()
{
    nodeDB.init();

    gpsObserver.observe(&gps);
    packetReceivedObserver.observe(&router.notifyPacketReceived);
}

void MeshService::sendOurOwner(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocForSending();
    p->to = dest;
    p->payload.want_response = wantReplies;
    p->payload.has_user = true;
    User &u = p->payload.user;
    u = owner;
    DEBUG_MSG("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);

    sendToMesh(p);
}

/// handle a user packet that just arrived on the radio, return NULL if we should not process this packet at all
const MeshPacket *MeshService::handleFromRadioUser(const MeshPacket *mp)
{
    bool wasBroadcast = mp->to == NODENUM_BROADCAST;
    bool isCollision = mp->from == myNodeInfo.my_node_num;

    // we win if we have a lower macaddr
    bool weWin = memcmp(&owner.macaddr, &mp->payload.user.macaddr, sizeof(owner.macaddr)) < 0;

    if (isCollision) {
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

        String lcd = String("Joined: ") + mp->payload.user.long_name + "\n";
        screen.print(lcd.c_str());
    }

    return mp;
}

void MeshService::handleIncomingPosition(const MeshPacket *mp)
{
    if (mp->has_payload && mp->payload.has_position) {
        DEBUG_MSG("handled incoming position time=%u\n", mp->payload.position.time);

        if (mp->payload.position.time) {
            struct timeval tv;
            uint32_t secs = mp->payload.position.time;

            tv.tv_sec = secs;
            tv.tv_usec = 0;

            gps.perhapsSetRTC(&tv);
        }
    } else {
        DEBUG_MSG("Ignoring incoming packet - not a position\n");
    }
}

int MeshService::handleFromRadio(const MeshPacket *mp)
{
    powerFSM.trigger(EVENT_RECEIVED_PACKET); // Possibly keep the node from sleeping

    // If it is a position packet, perhaps set our clock (if we don't have a GPS of our own, otherwise wait for that to work)
    if (!gps.isConnected)
        handleIncomingPosition(mp);
    else {
        DEBUG_MSG("Ignoring incoming time, because we have a GPS\n");
    }

    if (mp->has_payload && mp->payload.has_user) {
        mp = handleFromRadioUser(mp);
    }

    // If we veto a received User packet, we don't put it into the DB or forward it to the phone (to prevent confusing it)
    if (mp) {
        DEBUG_MSG("Forwarding to phone, from=0x%x, rx_time=%u\n", mp->from, mp->rx_time);
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

        if (mp->payload.want_response)
            sendNetworkPing(mp->from);
    } else {
        DEBUG_MSG("Not delivering vetoed User message\n");
    }

    return 0;
}

uint32_t sendOwnerCb()
{
    service.sendOurOwner();

    return radioConfig.preferences.send_owner_interval * radioConfig.preferences.position_broadcast_secs * 1000;
}

Periodic sendOwnerPeriod(sendOwnerCb);

/// Do idle processing (mostly processing messages which have been queued from the radio)
void MeshService::loop()
{
    if (oldFromNum != fromNum) { // We don't want to generate extra notifies for multiple new packets
        fromNumChanged.notifyObservers(fromNum);
        oldFromNum = fromNum;
    }

    // occasionally send our owner info
    sendOwnerPeriod.loop();
}

/// The radioConfig object just changed, call this to force the hw to change to the new settings
void MeshService::reloadConfig()
{
    // If we can successfully set this radio to these settings, save them to disk
    nodeDB.resetRadioConfig(); // Don't let the phone send us fatally bad settings
    configChanged.notifyObservers(NULL);
    nodeDB.saveToDisk();
}

/// Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
void MeshService::handleToRadio(std::string s)
{
    static ToRadio r; // this is a static scratch object, any data must be copied elsewhere before returning

    if (pb_decode_from_bytes((const uint8_t *)s.c_str(), s.length(), ToRadio_fields, &r)) {
        switch (r.which_variant) {
        case ToRadio_packet_tag: {
            // If our phone is sending a position, see if we can use it to set our RTC
            MeshPacket &p = r.variant.packet;
            handleIncomingPosition(&p); // If it is a position packet, perhaps set our clock

            if (p.from == 0) // If the phone didn't set a sending node ID, use ours
                p.from = nodeDB.getNodeNum();

            if (p.id == 0)
                p.id = generatePacketId(); // If the phone didn't supply one, then pick one

            p.rx_time = gps.getValidTime(); // Record the time the packet arrived from the phone
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
            break;
        }
        default:
            DEBUG_MSG("Error: unexpected ToRadio variant\n");
            break;
        }
    } else {
        DEBUG_MSG("Error: ignoring malformed toradio\n");
    }
}

void MeshService::sendToMesh(MeshPacket *p)
{
    nodeDB.updateFrom(*p); // update our local DB for this packet (because phone might have sent position packets etc...)

    // Strip out any time information before sending packets to other  nodes - to keep the wire size small (and because other
    // nodes shouldn't trust it anyways) Note: for now, we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (p->has_payload && p->payload.has_position) {
        if (!gps.isConnected) {
            DEBUG_MSG("Stripping time %u from position send\n", p->payload.position.time);
            p->payload.position.time = 0;
        } else
            DEBUG_MSG("Providing time to mesh %u\n", p->payload.position.time);
    }

    // If the phone sent a packet just to us, don't send it out into the network
    if (p->to == nodeDB.getNodeNum())
        DEBUG_MSG("Dropping locally processed message\n");
    else {
        // Note: We might return !OK if our fifo was full, at that point the only option we have is to drop it
        if (router.send(p) != ERRNO_OK) {
            DEBUG_MSG("No radio was able to send packet, discarding...\n");
            releaseToPool(p);
        }
    }
}

MeshPacket *MeshService::allocForSending()
{
    MeshPacket *p = packetPool.allocZeroed();

    p->has_payload = true;
    p->from = nodeDB.getNodeNum();
    p->to = NODENUM_BROADCAST;
    p->id = generatePacketId();
    p->rx_time = gps.getValidTime(); // Just in case we process the packet locally - make sure it has a valid timestamp

    return p;
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
    MeshPacket *p = allocForSending();
    p->to = dest;
    p->payload.has_position = true;
    p->payload.position = node->position;
    p->payload.want_response = wantReplies;
    p->payload.position.time = gps.getValidTime(); // This nodedb timestamp might be stale, so update it if our clock is valid.
    sendToMesh(p);
}

int MeshService::onGPSChanged(void *unused)
{
    DEBUG_MSG("got gps notify\n");

    // Update our local node info with our position (even if we don't decide to update anyone else)
    MeshPacket *p = allocForSending();
    p->payload.has_position = true;

    Position &pos = p->payload.position;
    // !zero or !zero lat/long means valid
    if (gps.latitude != 0 || gps.longitude != 0) {
        if (gps.altitude != 0)
            pos.altitude = gps.altitude;
        pos.latitude = gps.latitude;
        pos.longitude = gps.longitude;
        pos.time = gps.getValidTime();
    }

    // We limit our GPS broadcasts to a max rate
    static uint32_t lastGpsSend;
    uint32_t now = millis();
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
