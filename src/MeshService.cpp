
#include <Arduino.h>
#include <assert.h>

#include "mesh-pb-constants.h"
#include "MeshService.h"
#include "MeshBluetoothService.h"
#include "NodeDB.h"
#include "GPS.h"
#include "screen.h"

/*
receivedPacketQueue - this is a queue of messages we've received from the mesh, which we are keeping to deliver to the phone.
It is implemented with a FreeRTos queue (wrapped with a little RTQueue class) of pointers to MeshPacket protobufs (which were alloced with new).
After a packet ptr is removed from the queue and processed it should be deleted.  (eventually we should move sent packets into a 'sentToPhone' queue
of packets we can delete just as soon as we are sure the phone has acked those packets - when the phone writes to FromNum)

mesh - an instance of Mesh class.  Which manages the interface to the mesh radio library, reception of packets from other nodes, arbitrating to select
a node number and keeping the current nodedb.

*/

/* Broadcast when a newly powered mesh node wants to find a node num it can use

The algoritm is as follows:
* when a node starts up, it broadcasts their user and the normal flow is for all other nodes to reply with their User as well (so the new node can build its node db)
* If a node ever receives a User (not just the first broadcast) message where the sender node number equals our node number, that indicates a collision has occurred and the following steps should happen:

If the receiving node (that was already in the mesh)'s macaddr is LOWER than the new User who just tried to sign in: it gets to keep its nodenum.  We send a broadcast message
of OUR User (we use a broadcast so that the other node can receive our message, considering we have the same id - it also serves to let observers correct their nodedb) - this case is rare so it should be okay.

If any node receives a User where the macaddr is GTE than their local macaddr, they have been vetoed and should pick a new random nodenum (filtering against whatever it knows about the nodedb) and
rebroadcast their User.

FIXME in the initial proof of concept we just skip the entire want/deny flow and just hand pick node numbers at first.
*/

MeshService service;

// I think this is right, one packet for each of the three fifos + one packet being currently assembled for TX or RX
#define MAX_PACKETS (MAX_RX_TOPHONE + MAX_RX_FROMRADIO + MAX_TX_QUEUE + 2) // max number of packets which can be in flight (either queued from reception or queued for sending)

#define MAX_RX_FROMRADIO 4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big

MeshService::MeshService()
    : packetPool(MAX_PACKETS),
      toPhoneQueue(MAX_RX_TOPHONE),
      fromRadioQueue(MAX_RX_FROMRADIO),
      fromNum(0),
      radio(packetPool, fromRadioQueue)
{
    // assert(MAX_RX_TOPHONE == 32); // FIXME, delete this, just checking my clever macro
}

void MeshService::init()
{
    nodeDB.init();

    if (!radio.init())
        DEBUG_MSG("radio init failed\n");

    gps.addObserver(this);
    sendOurOwner();
}

void MeshService::sendOurOwner(NodeNum dest)
{
    MeshPacket *p = allocForSending();
    p->to = dest;
    p->payload.which_variant = SubPacket_user_tag;
    User &u = p->payload.variant.user;
    u = owner;
    DEBUG_MSG("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);

    sendToMesh(p);
}

void MeshService::handleFromRadio()
{
    MeshPacket *mp;
    uint32_t oldFromNum = fromNum;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL)
    {
        if (mp->has_payload && mp->payload.which_variant == SubPacket_user_tag)
        {
            bool wasBroadcast = mp->to == NODENUM_BROADCAST;
            bool isCollision = mp->from == myNodeInfo.my_node_num;

            // we win if we have a lower macaddr
            bool weWin = memcmp(&owner.macaddr, &mp->payload.variant.user.macaddr, sizeof(owner.macaddr)) < 0;

            if (isCollision)
            {
                if (weWin)
                {
                    DEBUG_MSG("NOTE! Received a nodenum collision and we are vetoing\n");

                    packetPool.release(mp); // discard it
                    mp = NULL;

                    sendOurOwner(); // send our owner as a _broadcast_ because that other guy is mistakenly using our nodenum
                }
                else
                {
                    // we lost, we need to try for a new nodenum!
                    DEBUG_MSG("NOTE! Received a nodenum collision we lost, so picking a new nodenum\n");
                    nodeDB.updateFrom(*mp); // update the DB early - before trying to repick (so we don't select the same node number again)
                    nodeDB.pickNewNodeNum();
                    sendOurOwner(); // broadcast our new attempt at a node number
                }
            }
            else if (wasBroadcast)
            {
                // If we haven't yet abandoned the packet and it was a broadcast, reply (just to them) with our User record so they can build their DB

                // Someone just sent us a User, reply with our Owner
                DEBUG_MSG("Received broadcast Owner from 0x%x, replying with our owner\n", mp->from);

                sendOurOwner(mp->from);

                String lcd = String("Joined: ") + mp->payload.variant.user.long_name + "\n";
                screen_print(lcd.c_str());
            }
        }

        // If we veto a received User packet, we don't put it into the DB or forward it to the phone (to prevent confusing it)
        if (mp)
        {
            nodeDB.updateFrom(*mp); // update our DB state based off sniffing every RX packet from the radio

            fromNum++;

            if (toPhoneQueue.numFree() == 0)
            {
                DEBUG_MSG("NOTE: tophone queue is full, discarding oldest\n");
                MeshPacket *d = toPhoneQueue.dequeuePtr(0);
                if (d)
                    releaseToPool(d);
            }
            assert(toPhoneQueue.enqueue(mp, 0) == pdTRUE); // FIXME, instead of failing for full queue, delete the oldest mssages
        }
        else
            DEBUG_MSG("Dropping vetoed User message\n");
    }
    if (oldFromNum != fromNum) // We don't want to generate extra notifies for multiple new packets
        bluetoothNotifyFromNum(fromNum);
}

/// Do idle processing (mostly processing messages which have been queued from the radio)
void MeshService::loop()
{
    radio.loop(); // FIXME, possibly move radio interaction to own thread

    handleFromRadio();

    // FIXME, don't send user this often, but for now it is useful for testing
    static uint32_t lastsend;
    uint32_t now = millis();
    if (now - lastsend > 5 * 60 * 1000)
    {
        lastsend = now;
        sendOurOwner();
    }
}

/// The radioConfig object just changed, call this to force the hw to change to the new settings
void MeshService::reloadConfig()
{
    // If we can successfully set this radio to these settings, save them to disk
    radio.reloadConfig();
    nodeDB.saveToDisk();
}

/// Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
void MeshService::handleToRadio(std::string s)
{
    static ToRadio r; // this is a static scratch object, any data must be copied elsewhere before returning

    if (pb_decode_from_bytes((const uint8_t *)s.c_str(), s.length(), ToRadio_fields, &r))
    {
        switch (r.which_variant)
        {
        case ToRadio_packet_tag:
            sendToMesh(packetPool.allocCopy(r.variant.packet));
            break;

        default:
            DEBUG_MSG("Error: unexpected ToRadio variant\n");
            break;
        }
    }
}

void MeshService::sendToMesh(MeshPacket *p)
{
    nodeDB.updateFrom(*p);
    assert(radio.send(p) == pdTRUE);
}

MeshPacket *MeshService::allocForSending()
{
    MeshPacket *p = packetPool.allocZeroed();

    p->has_payload = true;
    p->from = nodeDB.getNodeNum();
    p->to = NODENUM_BROADCAST;

    return p;
}

void MeshService::onGPSChanged()
{
    MeshPacket *p = allocForSending();
    p->payload.which_variant = SubPacket_position_tag;
    Position &pos = p->payload.variant.position;
    if (gps.altitude.isValid())
        pos.altitude = gps.altitude.meters();
    pos.latitude = gps.location.lat();
    pos.longitude = gps.location.lng();

    sendToMesh(p);
}

void MeshService::onNotify(Observable *o)
{
    DEBUG_MSG("got gps notify\n");
    onGPSChanged();
}