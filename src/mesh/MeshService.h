#pragma once

#include <Arduino.h>
#include <assert.h>
#include <string>

#include "GPSStatus.h"
#include "MemoryPool.h"
#include "MeshRadio.h"
#include "MeshTypes.h"
#include "Observer.h"
#include "concurrency/Lock.h"
#if defined(ARCH_PORTDUINO)
#include "PointerQueue.h"
#else
#include "StaticPointerQueue.h"
#endif
#include "mesh-pb-constants.h"
#if defined(ARCH_PORTDUINO)
#include "../platform/portduino/SimRadio.h"
#endif
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
#include "modules/StoreForwardModule.h"
#endif
#endif

extern Allocator<meshtastic_QueueStatus> &queueStatusPool;
extern Allocator<meshtastic_MqttClientProxyMessage> &mqttClientProxyMessagePool;
extern Allocator<meshtastic_ClientNotification> &clientNotificationPool;

class PhoneAPI;

#ifndef MAX_PHONE_API_CLIENTS
#define MAX_PHONE_API_CLIENTS 3
#endif

/**
 * Top level app for this service.  keeps the mesh, the radio config and the queue of received packets.
 *
 */
class MeshService
{
  public:
    enum APIState {
        STATE_DISCONNECTED, // Initial state, no API is connected
        STATE_BLE,
        STATE_WIFI,
        STATE_SERIAL,
        STATE_PACKET,
        STATE_HTTP,
        STATE_ETH
    };

    static constexpr uint32_t apiStateBit(APIState s)
    {
        return (s == STATE_DISCONNECTED) ? 0u : (1u << (static_cast<uint32_t>(s) - 1u));
    }

  private:
#if HAS_GPS
    CallbackObserver<MeshService, const meshtastic::GPSStatus *> gpsObserver =
        CallbackObserver<MeshService, const meshtastic::GPSStatus *>(this, &MeshService::onGPSChanged);
#endif

    struct PacketFanoutEntry {
        meshtastic_MeshPacket *payload = nullptr;
        uint8_t refcount = 0;
    };

    struct QueueStatusFanoutEntry {
        meshtastic_QueueStatus *payload = nullptr;
        uint8_t refcount = 0;
    };

    struct MqttProxyFanoutEntry {
        meshtastic_MqttClientProxyMessage *payload = nullptr;
        uint8_t refcount = 0;
    };

    struct ClientNotificationFanoutEntry {
        meshtastic_ClientNotification *payload = nullptr;
        uint8_t refcount = 0;
    };

    struct PhoneClientSlot {
        PhoneAPI *client = nullptr;
        APIState state = STATE_DISCONNECTED;
        bool active = false;

#if defined(ARCH_PORTDUINO)
        PointerQueue<PacketFanoutEntry> packetQueue;
        PointerQueue<QueueStatusFanoutEntry> queueStatusQueue;
        PointerQueue<MqttProxyFanoutEntry> mqttProxyQueue;
        PointerQueue<ClientNotificationFanoutEntry> clientNotificationQueue;
#else
        StaticPointerQueue<PacketFanoutEntry, MAX_RX_TOPHONE> packetQueue;
        StaticPointerQueue<QueueStatusFanoutEntry, MAX_RX_QUEUESTATUS_TOPHONE> queueStatusQueue;
        StaticPointerQueue<MqttProxyFanoutEntry, MAX_RX_MQTTPROXY_TOPHONE> mqttProxyQueue;
        StaticPointerQueue<ClientNotificationFanoutEntry, MAX_RX_NOTIFICATION_TOPHONE> clientNotificationQueue;
#endif

        PacketFanoutEntry *packetInflight = nullptr;
        QueueStatusFanoutEntry *queueStatusInflight = nullptr;
        MqttProxyFanoutEntry *mqttProxyInflight = nullptr;
        ClientNotificationFanoutEntry *clientNotificationInflight = nullptr;

#if defined(ARCH_PORTDUINO)
        PhoneClientSlot()
            : packetQueue(MAX_RX_TOPHONE), queueStatusQueue(MAX_RX_QUEUESTATUS_TOPHONE),
              mqttProxyQueue(MAX_RX_MQTTPROXY_TOPHONE), clientNotificationQueue(MAX_RX_NOTIFICATION_TOPHONE)
        {
        }
#endif
    };

#if defined(ARCH_PORTDUINO)
    MemoryDynamic<PacketFanoutEntry> packetFanoutPool;
    MemoryDynamic<QueueStatusFanoutEntry> queueStatusFanoutPool;
    MemoryDynamic<MqttProxyFanoutEntry> mqttProxyFanoutPool;
    MemoryDynamic<ClientNotificationFanoutEntry> clientNotificationFanoutPool;
#else
    static constexpr int kPacketFanoutPoolSize = MAX_PHONE_API_CLIENTS * (MAX_RX_TOPHONE + 1);
    static constexpr int kQueueStatusFanoutPoolSize = MAX_PHONE_API_CLIENTS * (MAX_RX_QUEUESTATUS_TOPHONE + 1);
    static constexpr int kMqttFanoutPoolSize = MAX_PHONE_API_CLIENTS * (MAX_RX_MQTTPROXY_TOPHONE + 1);
    static constexpr int kNotificationFanoutPoolSize = MAX_PHONE_API_CLIENTS * (MAX_RX_NOTIFICATION_TOPHONE + 1);

    MemoryPool<PacketFanoutEntry, kPacketFanoutPoolSize> packetFanoutPool;
    MemoryPool<QueueStatusFanoutEntry, kQueueStatusFanoutPoolSize> queueStatusFanoutPool;
    MemoryPool<MqttProxyFanoutEntry, kMqttFanoutPoolSize> mqttProxyFanoutPool;
    MemoryPool<ClientNotificationFanoutEntry, kNotificationFanoutPoolSize> clientNotificationFanoutPool;
#endif

    PhoneClientSlot phoneClients[MAX_PHONE_API_CLIENTS];

    /// Protects fanout client registry and all per-client fanout queues
    concurrency::Lock phoneClientsLock;

    /// Per APIState connected client counts
    uint8_t apiStateCounts[STATE_ETH + 1] = {0};

    // This holds the last QueueStatus send
    meshtastic_QueueStatus lastQueueStatus;

    /// The current nonce for the newest packet which has been queued for the phone
    uint32_t fromNum = 0;

    /// Updated in loop() to detect when fromNum changes
    uint32_t oldFromNum = 0;

    int findClientSlotByPtrLocked(const PhoneAPI *client) const;
    int findClientSlotByPtrLocked(const PhoneAPI *client);
    int findFreeClientSlotLocked() const;
    void clearClientSlotLocked(PhoneClientSlot &slot);
    void updateApiStateLocked(APIState preferred = STATE_DISCONNECTED);

    void releasePacketFanoutEntryLocked(PacketFanoutEntry *entry);
    void releaseQueueStatusFanoutEntryLocked(QueueStatusFanoutEntry *entry);
    void releaseMqttProxyFanoutEntryLocked(MqttProxyFanoutEntry *entry);
    void releaseClientNotificationFanoutEntryLocked(ClientNotificationFanoutEntry *entry);

    bool enqueuePacketFanoutLocked(meshtastic_MeshPacket *p);
    bool enqueueQueueStatusFanoutLocked(meshtastic_QueueStatus *qs);
    bool enqueueMqttProxyFanoutLocked(meshtastic_MqttClientProxyMessage *m);
    bool enqueueClientNotificationFanoutLocked(meshtastic_ClientNotification *cn);

  public:
    APIState api_state = STATE_DISCONNECTED;
    uint32_t api_state_mask = 0;

    static bool isTextPayload(const meshtastic_MeshPacket *p)
    {
        if (moduleConfig.range_test.enabled && p->decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP) {
            return true;
        }
        return p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
               p->decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP ||
               p->decoded.portnum == meshtastic_PortNum_ALERT_APP;
    }
    /// Called when some new packets have arrived from one of the radios
    Observable<uint32_t> fromNumChanged;

    /// Called when radio config has changed (radios should observe this and set their hardware as required)
    Observable<void *> configChanged;

    MeshService();

    void init();

    /// Do idle processing (mostly processing messages which have been queued from the radio)
    void loop();

    bool registerPhoneClient(PhoneAPI *client, APIState state);
    void unregisterPhoneClient(PhoneAPI *client);

    /// Return the next packet destined to the specified phone client.
    meshtastic_MeshPacket *getForPhone(PhoneAPI *client);

    /// Allows handlers to free packets after they have been sent
    void releaseToPool(meshtastic_MeshPacket *p) { packetPool.release(p); }

    /// Return the next QueueStatus packet destined to the specified phone client.
    meshtastic_QueueStatus *getQueueStatusForPhone(PhoneAPI *client);

    /// Return the next MqttClientProxyMessage packet destined to the specified phone client.
    meshtastic_MqttClientProxyMessage *getMqttClientProxyMessageForPhone(PhoneAPI *client);

    /// Return the next ClientNotification packet destined to the specified phone client.
    meshtastic_ClientNotification *getClientNotificationForPhone(PhoneAPI *client);

    void releaseToPoolForPhone(PhoneAPI *client, meshtastic_MeshPacket *p);
    void releaseQueueStatusToPoolForPhone(PhoneAPI *client, meshtastic_QueueStatus *p);
    void releaseMqttClientProxyMessageToPoolForPhone(PhoneAPI *client, meshtastic_MqttClientProxyMessage *p);
    void releaseClientNotificationToPoolForPhone(PhoneAPI *client, meshtastic_ClientNotification *p);

    // search the queue for a request id and return the matching nodenum
    NodeNum getNodenumFromRequestId(uint32_t request_id);

    // Release QueueStatus packet to pool
    void releaseQueueStatusToPool(meshtastic_QueueStatus *p) { queueStatusPool.release(p); }

    // Release MqttClientProxyMessage packet to pool
    void releaseMqttClientProxyMessageToPool(meshtastic_MqttClientProxyMessage *p) { mqttClientProxyMessagePool.release(p); }

    /// Release the next ClientNotification packet to pool.
    void releaseClientNotificationToPool(meshtastic_ClientNotification *p) { clientNotificationPool.release(p); }

    /**
     *  Given a ToRadio buffer parse it and properly handle it (setup radio, owner or send packet into the mesh)
     * Called by PhoneAPI.handleToRadio.  Note: p is a scratch buffer, this function is allowed to write to it but it can not keep
     * a reference
     */
    void handleToRadio(meshtastic_MeshPacket &p);

    /** The radioConfig object just changed, call this to force the hw to change to the new settings
     * @return true if client devices should be sent a new set of radio configs
     */
    void reloadConfig(int saveWhat = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);

    /// The owner User record just got updated, update our node DB and broadcast the info into the mesh
    void reloadOwner(bool shouldSave = true);

    /// Called when the user wakes up our GUI, normally sends our latest location to the mesh (if we have it), otherwise at least
    /// sends our nodeinfo
    /// returns true if we sent a position
    bool trySendPosition(NodeNum dest, bool wantReplies = false);

    /// Send a packet into the mesh - note p must have been allocated from packetPool.  We will return it to that pool after
    /// sending. This is the ONLY function you should use for sending messages into the mesh, because it also updates the nodedb
    /// cache
    void sendToMesh(meshtastic_MeshPacket *p, RxSource src = RX_SRC_LOCAL, bool ccToPhone = false);

    /** Attempt to cancel a previously sent packet from this _local_ node.  Returns true if a packet was found we could cancel */
    bool cancelSending(PacketId id);

    /// Pull the latest power and time info into my nodeinfo
    meshtastic_NodeInfoLite *refreshLocalMeshNode();

    /// Send a packet to active phone clients
    void sendToPhone(meshtastic_MeshPacket *p);

    /// Send an MQTT message to active phone clients for client proxying
    virtual void sendMqttMessageToClientProxy(meshtastic_MqttClientProxyMessage *m);

    /// Send a ClientNotification to active phone clients
    virtual void sendClientNotification(meshtastic_ClientNotification *cn);

    /// Send an error response to the phone
    void sendRoutingErrorResponse(meshtastic_Routing_Error error, const meshtastic_MeshPacket *mp);

    bool isToPhoneQueueEmpty();

    ErrorCode sendQueueStatusToPhone(const meshtastic_QueueStatus &qs, ErrorCode res, uint32_t mesh_packet_id);

    uint32_t GetTimeSinceMeshPacket(const meshtastic_MeshPacket *mp);

  private:
#if HAS_GPS
    /// Called when our gps position has changed - updates nodedb and sends Location message out into the mesh
    /// returns 0 to allow further processing
    int onGPSChanged(const meshtastic::GPSStatus *arg);
#endif
    /// Handle a packet that just arrived from the radio.  This method does _not_ free the provided packet.  If it
    /// needs to keep the packet around it makes a copy
    int handleFromRadio(const meshtastic_MeshPacket *p);
    friend class RoutingModule;
};

extern MeshService *service;
