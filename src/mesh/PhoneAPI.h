#pragma once

#include "Observer.h"
#include "concurrency/Lock.h"
#include "mesh-pb-constants.h"
#include "meshtastic/portnums.pb.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

// NodeNum stored as raw uint32_t below; including MeshTypes.h here breaks the
// LOG_WARN include order via Arduino.h/MemoryPool.h.

// Make sure that we never let our packets grow too large for one BLE packet
#define MAX_TO_FROM_RADIO_SIZE 512

#if meshtastic_FromRadio_size > MAX_TO_FROM_RADIO_SIZE
#error "meshtastic_FromRadio_size is too large for our BLE packets"
#endif
#if meshtastic_ToRadio_size > MAX_TO_FROM_RADIO_SIZE
#error "meshtastic_ToRadio_size is too large for our BLE packets"
#endif

#define SPECIAL_NONCE_ONLY_CONFIG 69420
#define SPECIAL_NONCE_ONLY_NODES 69421 // ( ͡° ͜ʖ ͡°)

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
        STATE_SEND_NOTHING, // Initial state, don't send anything until the client starts asking for config
        STATE_SEND_UIDATA,  // send stored data for device-ui
        STATE_SEND_MY_INFO, // send our my info record
        STATE_SEND_OWN_NODEINFO,
        STATE_SEND_METADATA,
        STATE_SEND_REGION_PRESETS,  // Send the region->valid-preset map (one message)
        STATE_SEND_CHANNELS,        // Send all channels
        STATE_SEND_CONFIG,          // Replacement for the old Radioconfig
        STATE_SEND_MODULECONFIG,    // Send Module specific config
        STATE_SEND_OTHER_NODEINFOS, // states progress in this order as the device sends to to the client
        STATE_SEND_FILEMANIFEST,    // Send file manifest
        STATE_SEND_COMPLETE_ID,
        STATE_SEND_PACKETS // live mesh packets + any cached satellite-DB replay that trails sync completion
    };

    // Satellite-DB replay (positions / telemetry / environment / status) used to live
    // as four top-level states between STATE_SEND_OTHER_NODEINFOS and STATE_SEND_FILEMANIFEST.
    // It now drains *after* config_complete_id has been emitted: the phone considers the
    // initial sync done as soon as headers + manifest are delivered, and the cached
    // position/telemetry/etc. trickle in alongside live mesh traffic inside STATE_SEND_PACKETS.
    enum ReplayPhase : uint8_t {
        REPLAY_PHASE_IDLE = 0, // not replaying (legacy clients, no-op DBs, or replay finished)
        REPLAY_PHASE_POSITIONS,
        REPLAY_PHASE_TELEMETRY,
        REPLAY_PHASE_ENVIRONMENT,
        REPLAY_PHASE_STATUS,
    };

    State state = STATE_SEND_NOTHING;

    uint8_t config_state = 0;

    // Hashmap of timestamps for last time we received a packet on the API per portnum
    std::unordered_map<meshtastic_PortNum, uint32_t> lastPortNumToRadio;
    uint32_t recentToRadioPacketIds[20]; // Last 20 ToRadio MeshPacket IDs we have seen

    /**
     * Each packet sent to the phone has an incrementing count
     */
    uint32_t fromRadioNum = 0;

    /// We temporarily keep the packet here between the call to available and getFromRadio.  We will free it after the phone
    /// downloads it
    meshtastic_MeshPacket *packetForPhone = NULL;

    // file transfer packets destined for phone. Push it to the queue then free it.
    meshtastic_XModem xmodemPacketForPhone = meshtastic_XModem_init_zero;

    // Keep QueueStatus packet just as packetForPhone
    meshtastic_QueueStatus *queueStatusPacketForPhone = NULL;

    // Keep MqttClientProxyMessage packet just as packetForPhone
    meshtastic_MqttClientProxyMessage *mqttClientProxyMessageForPhone = NULL;

    // Keep ClientNotification packet just as packetForPhone
    meshtastic_ClientNotification *clientNotification = NULL;

    /// We temporarily keep the nodeInfo here between the call to available and getFromRadio
    meshtastic_NodeInfo nodeInfoForPhone = meshtastic_NodeInfo_init_default;
    // Prefetched node info entries ready for immediate transmission to the phone.
    std::deque<meshtastic_NodeInfo> nodeInfoQueue;
    // Tunable size of the node info cache so we can keep BLE reads non-blocking.
    static constexpr size_t kNodePrefetchDepth = 4;
    // Protect nodeInfoForPhone + nodeInfoQueue because NimBLE callbacks run in a separate FreeRTOS task.
    concurrency::Lock nodeInfoMutex;

    // Synthetic-packet replay queue (paced via prefetch).
    std::deque<meshtastic_MeshPacket> replayQueue;
    static constexpr size_t kReplayPrefetchDepth = 4;
    // NodeNum snapshots taken at each replay phase start so concurrent
    // satellite-map mutations don't invalidate iteration.
    std::vector<uint32_t> replayPositionOrder;
    std::vector<uint32_t> replayTelemetryOrder;
    std::vector<uint32_t> replayEnvironmentOrder;
    std::vector<uint32_t> replayStatusOrder;
    size_t replayPositionIndex = 0;
    size_t replayTelemetryIndex = 0;
    size_t replayEnvironmentIndex = 0;
    size_t replayStatusIndex = 0;
    ReplayPhase replayPhase = REPLAY_PHASE_IDLE; // armed by sendConfigComplete() for full/default sync

    meshtastic_ToRadio toRadioScratch = {
        0}; // this is a static scratch object, any data must be copied elsewhere before returning

    /// Use to ensure that clients don't get confused about old messages from the radio
    uint32_t config_nonce = 0;
    uint32_t readIndex = 0;

    std::vector<meshtastic_FileInfo> filesManifest = {};

    void resetReadIndex() { readIndex = 0; }

  public:
    PhoneAPI();

    /// Destructor - calls close()
    virtual ~PhoneAPI();

    // Call this when the client drops the connection, resets the state to STATE_SEND_NOTHING
    // Unregisters our observer.  A closed connection **can** be reopened by calling init again.
    virtual void close();

    /**
     * Handle a ToRadio protobuf
     * @return true true if a packet was queued for sending (so that caller can yield)
     */
    virtual bool handleToRadio(const uint8_t *buf, size_t len);

    /**
     * Send a (client)notification to the phone
     */
    virtual void sendNotification(meshtastic_LogRecord_Level level, uint32_t replyId, const char *message);

    /**
     * Get the next packet we want to send to the phone
     *
     * We assume buf is at least FromRadio_size bytes long.
     * Returns number of bytes in the FromRadio packet (or 0 if no packet available)
     */
    size_t getFromRadio(uint8_t *buf);

    void sendConfigComplete();

    /**
     * Return true if we have data available to send to the phone
     */
    bool available();

    bool isConnected() { return state != STATE_SEND_NOTHING; }
    bool isSendingPackets() { return state == STATE_SEND_PACKETS; }

#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
    /// Per-connection auth: tracked in a small file-scope slot table keyed
    /// by PhoneAPI*. Adding state members directly to PhoneAPI broke
    /// USB-CDC enumeration on current nRF52 framework - even one extra
    /// per-instance uint32_t was enough. Keeping all state out-of-line
    /// avoids the issue.
    void setAdminAuthorized(bool authorized);
    bool getAdminAuthorized() const;

    /// Lock Now: O(1) invalidation of every connection's auth by advancing
    /// the global epoch. Subsequent gate checks see slot.myEpoch != epoch
    /// and treat the connection as unauthenticated.
    static void revokeAllAuth();

    /// Called from the main loop after NodeDB::reloadFromDisk() finishes.
    /// On reloadOk=true: any connection marked pending-unlock-after-reload
    /// is promoted to authorized and receives an UNLOCKED status; the
    /// screen-lock latch clears. On reloadOk=false: those connections
    /// receive a LOCKED(storage_corrupt) status and remain unauthorized
    /// so they cannot drive set_config against the corrupt baseline.
    static void completePendingUnlocks(bool reloadOk);

    /// Queue a LockdownStatus FromRadio for THIS connection only. Each
    /// PhoneAPI owns its own pending-status slot in a file-scope table
    /// (file-scope because adding fields directly to PhoneAPI broke
    /// USB-CDC enumeration on nRF52); a status produced here will not
    /// be delivered to any other connection. `lock_reason` may be
    /// nullptr / empty for non-LOCKED states.
    void queueLockdownStatus(meshtastic_LockdownStatus_State state, const char *lock_reason, uint8_t boots_remaining,
                             uint32_t valid_until_epoch, uint32_t backoff_seconds);

    /// Queue the same LockdownStatus on every active connection's slot.
    /// Use for events with no specific originating connection (session
    /// expiry tick in main.cpp, broadcast revocations, etc.). Per-
    /// connection callers should prefer the instance method above to
    /// avoid leaking one client's auth state to another.
    static void broadcastLockdownStatus(meshtastic_LockdownStatus_State state, const char *lock_reason, uint8_t boots_remaining,
                                        uint32_t valid_until_epoch, uint32_t backoff_seconds);

    /// True iff this connection has a pending lockdown_status drain.
    bool hasPendingLockdownStatus() const;
#endif

  protected:
    /// Our fromradio packet while it is being assembled
    meshtastic_FromRadio fromRadioScratch = {};

    /** the last msec we heard from the client on the other side of this link */
    uint32_t lastContactMsec = 0;

    /// Hookable to find out when connection changes
    virtual void onConnectionChanged(bool connected) {}

    /// If we haven't heard from the other side in a while then say not connected. Returns true if timeout occurred
    bool checkConnectionTimeout();

    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() = 0;

    /**
     * Subclasses can use this as a hook to provide custom notifications for their transport (i.e. bluetooth notifies)
     */
    virtual void onNowHasData(uint32_t fromRadioNum) {}

    /// Subclasses can use these lifecycle hooks for transport-specific behavior around config/steady-state
    /// (i.e. BLE connection params)
    virtual void onConfigStart() {}
    virtual void onConfigComplete() {}

    /// begin a new connection
    void handleStartConfig();

    enum APIType {
        TYPE_NONE, // Initial state, don't send anything until the client starts asking for config
        TYPE_BLE,
        TYPE_WIFI,
        TYPE_SERIAL,
        TYPE_PACKET,
        TYPE_HTTP,
        TYPE_ETH
    };

    APIType api_type = TYPE_NONE;

#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
    // No per-instance auth members - see method-level note. All state lives
    // in a file-scope slot table in PhoneAPI.cpp keyed by `this` pointer.

    // Pending LockdownStatus storage is NOT a class member - having a
    // meshtastic_LockdownStatus (~50 bytes with the char[33] lock_reason)
    // as a PhoneAPI member broke USB-CDC enumeration on the nRF52 Adafruit
    // framework. The exact mechanism wasn't pinned down, but moving the
    // storage to a file-scope static in PhoneAPI.cpp side-steps it cleanly.
    // Trade-off: all PhoneAPI instances share one pending slot. Acceptable
    // because only one transport delivers a lockdown command at a time in
    // any realistic scenario.
#endif

  private:
    void releasePhonePacket();

    void releaseQueueStatusPhonePacket();

    void prefetchNodeInfos();
    void beginReplayPositions();
    void prefetchReplayPositions();
    void beginReplayTelemetry();
    void prefetchReplayTelemetry();
    void beginReplayEnvironment();
    void prefetchReplayEnvironment();
    void beginReplayStatus();
    void prefetchReplayStatus();
    meshtastic_MeshPacket makeReplayPositionPacket(uint32_t num, const meshtastic_PositionLite &pos);
    meshtastic_MeshPacket makeReplayTelemetryPacket(uint32_t num, const meshtastic_DeviceMetrics &metrics);
    meshtastic_MeshPacket makeReplayEnvironmentPacket(uint32_t num, const meshtastic_EnvironmentMetrics &env);
    meshtastic_MeshPacket makeReplayStatusPacket(uint32_t num, const meshtastic_StatusMessage &status);

    // Post-sync replay drain: pop one cached packet from the active phase, advancing
    // through positions -> telemetry -> environment -> status until everything is drained.
    bool popReplayPacket(meshtastic_MeshPacket &out);
    void advanceReplayPhase();
    bool replayPending() const { return replayPhase != REPLAY_PHASE_IDLE; }

    void releaseMqttClientProxyPhonePacket();

    void releaseClientNotification();

    bool wasSeenRecently(uint32_t packetId);

    /**
     * Handle a packet that the phone wants us to send.  We can write to it but can not keep a reference to it
     * @return true true if a packet was queued for sending
     */
    bool handleToRadioPacket(meshtastic_MeshPacket &p);

#if defined(MESHTASTIC_ENCRYPTED_STORAGE) && defined(MESHTASTIC_PHONEAPI_ACCESS_CONTROL)
    /// Synchronously handle a lockdown_auth AdminMessage from the local
    /// client. Runs inside handleToRadioPacket so the originating
    /// connection is reachable via `this` - avoids the async context
    /// loss that broke the previous AdminModule path. Always consumes the
    /// packet (returns true): lockdown_auth is local-only and must not be
    /// forwarded to the mesh router.
    bool handleLockdownAuthInline(const meshtastic_LockdownAuth &la);
#endif

    /// If the mesh service tells us fromNum has changed, tell the phone
    virtual int onNotify(uint32_t newValue) override;
};
