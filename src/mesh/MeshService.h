#pragma once

#include <Arduino.h>
#include <assert.h>
#include <string>

#include "GPSStatus.h"
#include "MemoryPool.h"
#include "MeshRadio.h"
#include "MeshTypes.h"
#include "Observer.h"
#include "PointerQueue.h"
#if defined(ARCH_PORTDUINO) && !HAS_RADIO
#include "../platform/portduino/SimRadio.h"
#endif

extern Allocator<meshtastic_QueueStatus> &queueStatusPool;
extern Allocator<meshtastic_MqttClientProxyMessage> &mqttClientProxyMessagePool;

/**
 * Top level app for this service.  keeps the mesh, the radio config and the queue of received packets.
 *
 */
class MeshService
{
    CallbackObserver<MeshService, const meshtastic::GPSStatus *> gpsObserver =
        CallbackObserver<MeshService, const meshtastic::GPSStatus *>(this, &MeshService::onGPSChanged);

    /// received packets waiting for the phone to process them
    /// FIXME, change to a DropOldestQueue and keep a count of the number of dropped packets to ensure
    /// we never hang because android hasn't been there in a while
    /// FIXME - save this to flash on deep sleep
    PointerQueue<meshtastic_MeshPacket> toPhoneQueue;

    // keep list of QueueStatus packets to be send to the phone
    PointerQueue<meshtastic_QueueStatus> toPhoneQueueStatusQueue;

    // keep list of MqttClientProxyMessages to be send to the client for delivery
    PointerQueue<meshtastic_MqttClientProxyMessage> toPhoneMqttProxyQueue;

    // This holds the last QueueStatus send
    meshtastic_QueueStatus lastQueueStatus;

    /// The current nonce for the newest packet which has been queued for the phone
    uint32_t fromNum = 0;

    /// Updated in loop() to detect when fromNum changes
    uint32_t oldFromNum = 0;

  public:
    static bool isTextPayload(const meshtastic_MeshPacket *p)
    {
        if (moduleConfig.range_test.enabled && p->decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP) {
            return true;
        }
        return p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
               p->decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP;
    }
    /// Called when some new packets have arrived from one of the radios
    Observable<uint32_t> fromNumChanged;

    /// Called when radio config has changed (radios should observe this and set their hardware as required)
    Observable<void *> configChanged;

    MeshService();

    void init();

    /// Do idle processing (mostly processing messages which have been queued from the radio)
    void loop();

    /// Return the next packet destined to the phone.  FIXME, somehow use fromNum to allow the phone to retry the
    /// last few packets if needs to.
    meshtastic_MeshPacket *getForPhone() { return toPhoneQueue.dequeuePtr(0); }

    /// Allows the bluetooth handler to free packets after they have been sent
    void releaseToPool(meshtastic_MeshPacket *p) { packetPool.release(p); }

    /// Return the next QueueStatus packet destined to the phone.
    meshtastic_QueueStatus *getQueueStatusForPhone() { return toPhoneQueueStatusQueue.dequeuePtr(0); }

    /// Return the next MqttClientProxyMessage packet destined to the phone.
    meshtastic_MqttClientProxyMessage *getMqttClientProxyMessageForPhone() { return toPhoneMqttProxyQueue.dequeuePtr(0); }

    // search the queue for a request id and return the matching nodenum
    NodeNum getNodenumFromRequestId(uint32_t request_id);

    // Release QueueStatus packet to pool
    void releaseQueueStatusToPool(meshtastic_QueueStatus *p) { queueStatusPool.release(p); }

    // Release MqttClientProxyMessage packet to pool
    void releaseMqttClientProxyMessageToPool(meshtastic_MqttClientProxyMessage *p) { mqttClientProxyMessagePool.release(p); }

    /**
     *  Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
     * Called by PhoneAPI.handleToRadio.  Note: p is a scratch buffer, this function is allowed to write to it but it can not keep
     * a reference
     */
    void handleToRadio(meshtastic_MeshPacket &p);

    /** The radioConfig object just changed, call this to force the hw to change to the new settings
     * @return true if client devices should be sent a new set of radio configs
     */
    bool reloadConfig(int saveWhat = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);

    /// The owner User record just got updated, update our node DB and broadcast the info into the mesh
    void reloadOwner(bool shouldSave = true);

    /// Called when the user wakes up our GUI, normally sends our latest location to the mesh (if we have it), otherwise at least
    /// sends our owner
    void sendNetworkPing(NodeNum dest, bool wantReplies = false);

    /// Send a packet into the mesh - note p must have been allocated from packetPool.  We will return it to that pool after
    /// sending. This is the ONLY function you should use for sending messages into the mesh, because it also updates the nodedb
    /// cache
    void sendToMesh(meshtastic_MeshPacket *p, RxSource src = RX_SRC_LOCAL, bool ccToPhone = false);

    /** Attempt to cancel a previously sent packet from this _local_ node.  Returns true if a packet was found we could cancel */
    bool cancelSending(PacketId id);

    /// Pull the latest power and time info into my nodeinfo
    meshtastic_NodeInfoLite *refreshLocalMeshNode();

    /// Send a packet to the phone
    void sendToPhone(meshtastic_MeshPacket *p);

    /// Send an MQTT message to the phone for client proxying
    void sendMqttMessageToClientProxy(meshtastic_MqttClientProxyMessage *m);

    bool isToPhoneQueueEmpty();

    ErrorCode sendQueueStatusToPhone(const meshtastic_QueueStatus &qs, ErrorCode res, uint32_t mesh_packet_id);

  private:
    /// Called when our gps position has changed - updates nodedb and sends Location message out into the mesh
    /// returns 0 to allow further processing
    int onGPSChanged(const meshtastic::GPSStatus *arg);

    /// Handle a packet that just arrived from the radio.  This method does _ReliableRouternot_ free the provided packet.  If it
    /// needs to keep the packet around it makes a copy
    int handleFromRadio(const meshtastic_MeshPacket *p);
    friend class RoutingModule;
};

extern MeshService service;