#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif
#ifdef MESHTASTIC_LOCKDOWN
#include "security/LockdownDisplay.h"
#endif
#include "Channels.h"
#include "Default.h"
#include "FSCommon.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PacketHistory.h"
#include "PhoneAPI.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include "Router.h"
#include "SPILock.h"
#include "TypeConversions.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include "modules/NodeInfoModule.h"
#include "xmodem.h"

#if FromRadio_size > MAX_TO_FROM_RADIO_SIZE
#error FromRadio is too big
#endif

#if ToRadio_size > MAX_TO_FROM_RADIO_SIZE
#error ToRadio is too big
#endif
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif
#include "Throttle.h"
#include <RTC.h>

namespace
{
constexpr uint8_t FILES_MANIFEST_LEVELS = 3;
constexpr size_t FILES_MANIFEST_MAX_COUNT = 64;

void releaseFilesManifest(std::vector<meshtastic_FileInfo> &filesManifest)
{
    std::vector<meshtastic_FileInfo>().swap(filesManifest);
}
} // namespace

// Flag to indicate a heartbeat was received and we should send queue status
bool heartbeatReceived = false;

#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
// Auth-slot table and status-slot table are both sized to the typical
// SerialConsole + BluetoothPhoneAPI footprint plus room for WiFi/TCP
// transports. Sized together so both tables are keyed identically.
static constexpr size_t MAX_AUTH_SLOTS = 6;

// Per-PhoneAPI pending LockdownStatus. One slot per connection so a
// status produced for connection A (e.g. UNLOCKED with the active TTL,
// or UNLOCK_FAILED with a backoff) cannot be drained by connection B,
// which would otherwise learn that A just authenticated or just failed
// - a real information leak across local clients.
//
// File-scope rather than a per-PhoneAPI member because adding any
// non-trivial state directly to PhoneAPI broke USB-CDC enumeration on
// the current nRF52 framework; the auth-slot table next door uses the
// same workaround. Lifecycle is tied to the auth slot table - both are
// keyed by PhoneAPI*, both are cleared together in clearAuthSlot_LH,
// and both share g_authSlotsMutex.
struct PendingStatusSlot {
    PhoneAPI *who = nullptr;
    meshtastic_LockdownStatus status = {};
    bool hasPending = false;
    // True between a successful passphrase verify and the main-loop
    // reloadFromDisk that follows. While set, the connection is NOT
    // yet authorized and no UNLOCKED status has been emitted - the
    // client still sees LOCKED, and any admin op it tries is dropped
    // by the existing unauth gates. Cleared either way by
    // completePendingUnlocks once reload finishes.
    bool pendingUnlockAfterReload = false;
};
static PendingStatusSlot g_statusSlots[MAX_AUTH_SLOTS];

// Lock-held helpers ---------------------------------------------------------

static PendingStatusSlot *findOrAllocStatusSlot_LH(PhoneAPI *p)
{
    if (!p)
        return nullptr;
    for (auto &s : g_statusSlots)
        if (s.who == p)
            return &s;
    for (auto &s : g_statusSlots) {
        if (s.who == nullptr) {
            s.who = p;
            s.hasPending = false;
            s.pendingUnlockAfterReload = false;
            memset(&s.status, 0, sizeof(s.status));
            return &s;
        }
    }
    // Mirror the auth-slot eviction policy: stale slots can be reused.
    // A connection that lost its auth slot has nothing meaningful to be
    // told via a pending status anyway. Never evict a slot mid-unlock
    // (pendingUnlockAfterReload set) - completing that flow on the
    // wrong PhoneAPI would authorize the wrong connection.
    for (auto &s : g_statusSlots) {
        if (!s.hasPending && !s.pendingUnlockAfterReload) {
            s.who = p;
            memset(&s.status, 0, sizeof(s.status));
            return &s;
        }
    }
    return nullptr;
}

static void clearStatusSlot_LH(const PhoneAPI *p)
{
    if (!p)
        return;
    for (auto &s : g_statusSlots) {
        if (s.who == p) {
            s.who = nullptr;
            s.hasPending = false;
            s.pendingUnlockAfterReload = false;
            memset(&s.status, 0, sizeof(s.status));
            return;
        }
    }
}

// Build a LockdownStatus message under lock from the supplied fields,
// applying the audit's M13 redaction so token_* tamper-detection
// strings are not leaked to unauth clients over the wire.
static void buildStatus_LH(meshtastic_LockdownStatus &out, meshtastic_LockdownStatus_State state, const char *lock_reason,
                           uint8_t boots_remaining, uint32_t valid_until_epoch, uint32_t backoff_seconds)
{
    memset(&out, 0, sizeof(out));
    out.state = state;
    // Collapse the specific token_* reasons to a generic "locked" over
    // the wire - full detail still goes to local logs. An unauth client
    // does not need to know whether HMAC failed vs the boot count
    // hit zero vs the file was the wrong size; all of those mean the
    // same thing to the client ("locked, ask for passphrase") but
    // telling them apart over the network lets an attacker confirm
    // that their tampering or rollback attempt was noticed.
    const char *wireReason = lock_reason;
    if (state == meshtastic_LockdownStatus_State_LOCKED && wireReason && wireReason[0] != '\0') {
        if (strncmp(wireReason, "token_", 6) == 0)
            wireReason = "locked";
    }
    if (wireReason && wireReason[0] != '\0')
        strncpy(out.lock_reason, wireReason, sizeof(out.lock_reason) - 1);
    out.boots_remaining = boots_remaining;
    out.valid_until_epoch = valid_until_epoch;
    out.backoff_seconds = backoff_seconds;
}

// Per-connection auth state table keyed by PhoneAPI*. Searched linearly;
// cost is negligible compared to the redaction gates that call it.
struct PhoneAuthSlot {
    PhoneAPI *who = nullptr;
    bool authorized = false;
    uint32_t epoch = 0;
};
static PhoneAuthSlot g_authSlots[MAX_AUTH_SLOTS];

// Global auth epoch. Lock Now bumps it; per-slot `epoch` compared against
// this. Wraps at 2^32 revocations - practically unreachable; on wrap the
// only behavioral effect is that any slot whose epoch happens to match the
// new low value would be treated as authorized again, which requires a
// pre-existing authorized slot to survive 2^32 lockNow events on the same
// boot.
static uint32_t g_authEpoch = 1;

// Single mutex guarding g_authSlots and g_authEpoch. All readers and
// writers - including const getters like getAdminAuthorized - must take
// it. Granularity is fine because the critical sections are short (a
// fixed-size linear scan over 6 entries) and contention is dominated by
// getFromRadio's per-call redaction checks, which tolerate brief
// blocking.
static concurrency::Lock g_authSlotsMutex;

// Find or allocate the auth slot for `p`. Caller must hold g_authSlotsMutex.
// When the table is full of *unauthorized* slots from prior dead PhoneAPIs,
// evicts the first unauthorized slot found. Refuses to evict an authorized
// slot (those represent a live operator session and must outlive the table
// pressure of reconnect churn). Returns nullptr only if every slot is
// occupied by a different live, authorized PhoneAPI - practically only
// reachable as a DoS via 7+ simultaneous authed connections, in which
// case fail-closed and log.
static PhoneAuthSlot *findOrAllocSlot_LH(PhoneAPI *p)
{
    if (!p)
        return nullptr;
    for (auto &s : g_authSlots)
        if (s.who == p)
            return &s;
    // First pass: free (who==nullptr) slot.
    for (auto &s : g_authSlots) {
        if (s.who == nullptr) {
            s.who = p;
            s.authorized = false;
            s.epoch = 0;
            return &s;
        }
    }
    // Second pass: evict an unauthorized stale slot. Don't touch authorized
    // ones - those still represent an operator-authenticated session.
    for (auto &s : g_authSlots) {
        if (!s.authorized) {
            s.who = p;
            s.epoch = 0;
            LOG_WARN("Lockdown: auth slot table full, evicted stale unauthorized slot for new PhoneAPI %p", p);
            return &s;
        }
    }
    LOG_WARN("Lockdown: auth slot table full of authorized sessions, refusing new PhoneAPI %p (fail-closed)", p);
    return nullptr;
}

// Drop p's slot from both the auth table and the status-queue table.
// Lock-held variant.
static void clearAuthSlot_LH(const PhoneAPI *p)
{
    if (!p)
        return;
    for (auto &s : g_authSlots) {
        if (s.who == p) {
            s.authorized = false;
            s.epoch = 0;
            s.who = nullptr;
            break;
        }
    }
    clearStatusSlot_LH(p);
}
#endif

PhoneAPI::PhoneAPI()
{
    lastContactMsec = millis();
    std::fill(std::begin(recentToRadioPacketIds), std::end(recentToRadioPacketIds), 0);
}

PhoneAPI::~PhoneAPI()
{
    close();
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
    // Free the auth slot unconditionally, regardless of whether close()'s
    // slot-clear branch ran (it skips when state == STATE_SEND_NOTHING).
    // Leaving a stale slot.who pointing at freed memory lets a future
    // PhoneAPI heap-allocated at the same address inherit the prior
    // session's authorization through findOrAllocSlot.
    {
        concurrency::LockGuard g(&g_authSlotsMutex);
        clearAuthSlot_LH(this);
    }
#endif
}

void PhoneAPI::handleStartConfig()
{
    // Must be before setting state (because state is how we know !connected)
    if (!isConnected()) {
        onConnectionChanged(true);
        observe(&service->fromNumChanged);
#ifdef FSCom
        observe(&xModem.packetReady);
#endif
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        // New physical connection: clear this PhoneAPI's auth slot so the new
        // client must present a passphrase or PKC admin signature before
        // seeing full config. Do NOT reset on a subsequent want_config_id
        // within the same connection: after a successful unlock the client
        // re-requests config to pull the now-unredacted values, and re-locking
        // that same-link re-fetch would strip the auth it just earned (config
        // comes back redacted and set_config writes get dropped).
        //
        // The security boundary is therefore the physical connection, not the
        // want_config handshake. For BLE that boundary is enforced in
        // onConnect() (which fires once per link and also resets the slot), so
        // a reconnect re-locks even if this !isConnected() transition was
        // missed because the prior link's close() raced the new config burst.
        {
            concurrency::LockGuard g(&g_authSlotsMutex);
            if (auto *slot = findOrAllocSlot_LH(this)) {
                slot->authorized = false;
                slot->epoch = 0;
            }
        }
#endif
    }

    // Allow subclasses to prepare for high-throughput config traffic
    onConfigStart();

    // even if we were already connected - restart our state machine
    if (config_nonce == SPECIAL_NONCE_ONLY_NODES) {
        // If client only wants node info, jump directly to sending nodes
        state = STATE_SEND_OWN_NODEINFO;
        LOG_INFO("Client only wants node info, skipping other config");
    } else {
        state = STATE_SEND_MY_INFO;
    }
    pauseBluetoothLogging = true;
#if defined(MESHTASTIC_EXCLUDE_FILES_MANIFEST)
    // Skip the recursive FS walk. Used by platforms whose Zephyr LittleFS
    // backend can't safely traverse a deep tree (e.g. nRF54L15) and platforms
    // that don't support OTA browsing - the manifest is only consumed by
    // companion apps for those flows.
    releaseFilesManifest(filesManifest);
#else
    // Manifest is never read on the node-info-only path (STATE_SEND_FILEMANIFEST
    // short-circuits to sendConfigComplete), so skip the SPI lock + FS walk.
    if (config_nonce != SPECIAL_NONCE_ONLY_NODES) {
        bool filesManifestLimited = false;
        {
            concurrency::LockGuard guard(spiLock);
            filesManifest = getFiles("/", FILES_MANIFEST_LEVELS, FILES_MANIFEST_MAX_COUNT, &filesManifestLimited);
        }
        if (filesManifestLimited) {
            LOG_WARN("Got %zu files in manifest (limited to %zu entries/depth %u)", filesManifest.size(),
                     FILES_MANIFEST_MAX_COUNT, static_cast<unsigned>(FILES_MANIFEST_LEVELS));
        } else {
            LOG_DEBUG("Got %zu files in manifest", filesManifest.size());
        }
    } else {
        releaseFilesManifest(filesManifest);
    }
#endif

    LOG_INFO("Start API client config millis=%u", millis());
    // Protect against concurrent BLE callbacks: they run in NimBLE's FreeRTOS task and also touch nodeInfoQueue.
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        nodeInfoForPhone = {};
        nodeInfoQueue.clear();
        replayQueue.clear();
        replayPositionOrder.clear();
        replayTelemetryOrder.clear();
        replayEnvironmentOrder.clear();
        replayStatusOrder.clear();
        replayPositionIndex = 0;
        replayTelemetryIndex = 0;
        replayEnvironmentIndex = 0;
        replayStatusIndex = 0;
    }
    resetReadIndex();
}

void PhoneAPI::close()
{
    LOG_DEBUG("PhoneAPI::close()");
    if (service->api_state == service->STATE_BLE && api_type == TYPE_BLE)
        service->api_state = service->STATE_DISCONNECTED;
    else if (service->api_state == service->STATE_WIFI && api_type == TYPE_WIFI)
        service->api_state = service->STATE_DISCONNECTED;
    else if (service->api_state == service->STATE_SERIAL && api_type == TYPE_SERIAL)
        service->api_state = service->STATE_DISCONNECTED;
    else if (service->api_state == service->STATE_PACKET && api_type == TYPE_PACKET)
        service->api_state = service->STATE_DISCONNECTED;
    else if (service->api_state == service->STATE_HTTP && api_type == TYPE_HTTP)
        service->api_state = service->STATE_DISCONNECTED;
    else if (service->api_state == service->STATE_ETH && api_type == TYPE_ETH)
        service->api_state = service->STATE_DISCONNECTED;

    if (state != STATE_SEND_NOTHING) {
        state = STATE_SEND_NOTHING;
        resetReadIndex();
        unobserve(&service->fromNumChanged);
#ifdef FSCom
        unobserve(&xModem.packetReady);
#endif
        releasePhonePacket(); // Don't leak phone packets on shutdown
        releaseQueueStatusPhonePacket();
        releaseMqttClientProxyPhonePacket();
        releaseClientNotification();
        onConnectionChanged(false);
        fromRadioScratch = {};
        toRadioScratch = {};
        // Clear cached node info under lock because NimBLE callbacks can still be draining it.
        {
            concurrency::LockGuard guard(&nodeInfoMutex);
            nodeInfoForPhone = {};
            nodeInfoQueue.clear();
            replayQueue.clear();
            replayPositionOrder.clear();
            replayTelemetryOrder.clear();
            replayEnvironmentOrder.clear();
            replayStatusOrder.clear();
            replayPositionIndex = 0;
            replayTelemetryIndex = 0;
            replayEnvironmentIndex = 0;
            replayStatusIndex = 0;
            replayPhase = REPLAY_PHASE_IDLE;
        }
        packetForPhone = NULL;
        releaseFilesManifest(filesManifest);
        lastPortNumToRadio.clear();
        fromRadioNum = 0;
        config_nonce = 0;
        config_state = 0;
        pauseBluetoothLogging = false;
        heartbeatReceived = false;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        {
            concurrency::LockGuard g(&g_authSlotsMutex);
            clearAuthSlot_LH(this);
        }
#endif
    }
}

bool PhoneAPI::checkConnectionTimeout()
{
    if (isConnected()) {
        bool newContact = checkIsConnected();
        if (!newContact) {
            LOG_INFO("Lost phone connection");
            close();
            return true;
        }
    }
    return false;
}

/**
 * Handle a ToRadio protobuf
 */
bool PhoneAPI::handleToRadio(const uint8_t *buf, size_t bufLength)
{
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // As long as the phone keeps talking to us, don't let the radio go to sleep
    lastContactMsec = millis();

    memset(&toRadioScratch, 0, sizeof(toRadioScratch));
    if (pb_decode_from_bytes(buf, bufLength, &meshtastic_ToRadio_msg, &toRadioScratch)) {
        switch (toRadioScratch.which_payload_variant) {
        case meshtastic_ToRadio_packet_tag:
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Allow admin messages addressed to this device - passphrase delivery must get through.
                // AdminModule handles its own is_managed gate for those.
                // Block everything else - unauthorized clients cannot inject mesh traffic.
                // Require the packet to carry a decoded (not encrypted) payload so portnum is valid.
                // Refuse to match when our own node number is still 0 (NodeDB
                // not yet loaded - happens during the locked-default boot path
                // before reloadFromDisk). Otherwise a packet with to==0 would
                // satisfy the equality and bypass the gate.
                NodeNum ourNum = nodeDB->getNodeNum();
                bool isLocalAdmin =
                    ourNum != 0 && toRadioScratch.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
                    toRadioScratch.packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP && toRadioScratch.packet.to == ourNum;
                if (!isLocalAdmin) {
                    LOG_INFO("Lockdown: Dropping non-admin ToRadio packet from unauthorized client");
                    return false;
                }
            }
#endif
            return handleToRadioPacket(toRadioScratch.packet);
        case meshtastic_ToRadio_want_config_id_tag:
            config_nonce = toRadioScratch.want_config_id;
            LOG_INFO("Client wants config, nonce=%u", config_nonce);
            handleStartConfig();
            break;
        case meshtastic_ToRadio_disconnect_tag:
            LOG_INFO("Disconnect from phone");
            close();
            break;
        case meshtastic_ToRadio_xmodemPacket_tag:
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                LOG_INFO("Lockdown: Dropping xmodem packet from unauthorized client");
                break;
            }
#endif
            LOG_INFO("Got xmodem packet");
#ifdef FSCom
            xModem.handlePacket(toRadioScratch.xmodemPacket);
#endif
            break;
#if !MESHTASTIC_EXCLUDE_MQTT
        case meshtastic_ToRadio_mqttClientProxyMessage_tag:
            LOG_DEBUG("Got MqttClientProxy message");
            if (state != STATE_SEND_PACKETS) {
                LOG_WARN("Ignore MqttClientProxy message while completing config handshake");
                break;
            }
            if (mqtt && moduleConfig.mqtt.proxy_to_client_enabled && moduleConfig.mqtt.enabled &&
                (channels.anyMqttEnabled() || moduleConfig.mqtt.map_reporting_enabled)) {
                mqtt->onClientProxyReceive(toRadioScratch.mqttClientProxyMessage);
            } else {
                LOG_WARN("MqttClientProxy received but proxy is not enabled, no channels have up/downlink, or map reporting "
                         "not enabled");
            }
            break;
#endif
        case meshtastic_ToRadio_heartbeat_tag:
            // nonce==1 is a special "nodeinfo ping" trigger: force a fresh
            // NodeInfo broadcast on the 60-second shorterTimeout path so
            // peers can re-learn our public key after a reboot or
            // factory_reset without waiting out the normal 10-minute
            // NodeInfo send cooldown. Mirrors the TCP/UDP path in
            // `src/mesh/api/PacketAPI.cpp:74-79` for serial clients.
            // Default nonce (0) remains a plain keepalive that triggers
            // a queue-status reply.
            if (toRadioScratch.heartbeat.nonce == 1) {
                if (nodeInfoModule) {
                    LOG_INFO("Broadcasting nodeinfo ping (serial)");
                    nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, true, 0, true);
                }
            } else {
                LOG_DEBUG("Got client heartbeat");
                heartbeatReceived = true;
            }
            break;
        default:
            // Ignore nop messages
            break;
        }
    } else {
        LOG_ERROR("Error: ignore malformed toradio");
    }

    return false;
}

/**
 * Get the next packet we want to send to the phone, or NULL if no such packet is available.
 *
 * We assume buf is at least FromRadio_size bytes long.
 *
 * Our sending states progress in the following sequence (the client apps ASSUME THIS SEQUENCE, DO NOT CHANGE IT):
    STATE_SEND_MY_INFO, // send our my info record
    STATE_SEND_UIDATA,
    STATE_SEND_OWN_NODEINFO,
    STATE_SEND_METADATA,
    STATE_SEND_REGION_PRESETS, // region -> valid modem presets (one message)
    STATE_SEND_CHANNELS,
    STATE_SEND_CONFIG,
    STATE_SEND_MODULECONFIG,
    STATE_SEND_OTHER_NODEINFOS, // states progress in this order as the device sends to the client
    STATE_SEND_FILEMANIFEST,
    STATE_SEND_COMPLETE_ID,
    STATE_SEND_PACKETS // send packets or debug strings
 */

size_t PhoneAPI::getFromRadio(uint8_t *buf)
{
    // Respond to heartbeat by sending queue status
    if (heartbeatReceived) {
        memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
        fromRadioScratch.queueStatus = router->getQueueStatus();
        heartbeatReceived = false;
        size_t numbytes = pb_encode_to_bytes(buf, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratch);
        LOG_DEBUG("FromRadio=STATE_SEND_QUEUE_STATUS, numbytes=%u", numbytes);
        return numbytes;
    }

    if (!available()) {
        return 0;
    }
    // In case we send a FromRadio packet
    memset(&fromRadioScratch, 0, sizeof(fromRadioScratch));

    // Advance states as needed
    switch (state) {
    case STATE_SEND_NOTHING:
        LOG_DEBUG("FromRadio=STATE_SEND_NOTHING");
        break;
    case STATE_SEND_MY_INFO:
        LOG_DEBUG("FromRadio=STATE_SEND_MY_INFO");
        // If the user has specified they don't want our node to share its location, make sure to tell the phone
        // app not to send locations on our behalf.
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_my_info_tag;
        strncpy(myNodeInfo.pio_env, optstr(APP_ENV), sizeof(myNodeInfo.pio_env));
        myNodeInfo.nodedb_count = static_cast<uint16_t>(nodeDB->getNumMeshNodes());
        fromRadioScratch.my_info = myNodeInfo;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        if (!getAdminAuthorized()) {
            // device_id is a stable hardware identifier - useful for an attacker
            // to fingerprint / correlate the device across observations. Strip it
            // for unauthenticated clients. my_node_num is kept (it's broadcast
            // on the mesh anyway). pio_env / min_app_version reveal the exact
            // build flavour, useful only for picking which known-CVE to try.
            // nodedb_count stays - clients need it to decide whether to pull
            // the node DB after unlocking.
            fromRadioScratch.my_info.device_id.size = 0;
            memset(fromRadioScratch.my_info.device_id.bytes, 0, sizeof(fromRadioScratch.my_info.device_id.bytes));
            memset(fromRadioScratch.my_info.pio_env, 0, sizeof(fromRadioScratch.my_info.pio_env));
            fromRadioScratch.my_info.min_app_version = 0;
        }
#endif
        state = STATE_SEND_UIDATA;

        service->refreshLocalMeshNode(); // Update my NodeInfo because the client will be asking for it soon.
        break;

    case STATE_SEND_UIDATA:
        LOG_INFO("getFromRadio=STATE_SEND_UIDATA");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_deviceuiConfig_tag;
        fromRadioScratch.deviceuiConfig = uiconfig;
        state = STATE_SEND_OWN_NODEINFO;
        break;

    case STATE_SEND_OWN_NODEINFO: {
        LOG_DEBUG("Send My NodeInfo");
        auto us = nodeDB->readNextMeshNode(readIndex);
        if (us) {
            auto info = TypeConversions::ConvertToNodeInfo(us);
            info.has_hops_away = false;
            info.is_favorite = true;
            {
                concurrency::LockGuard guard(&nodeInfoMutex);
                nodeInfoForPhone = info;
            }
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            fromRadioScratch.node_info = info;
            // Should allow us to resume sending NodeInfo in STATE_SEND_OTHER_NODEINFOS
            {
                concurrency::LockGuard guard(&nodeInfoMutex);
                nodeInfoForPhone.num = 0;
            }
        }
        if (config_nonce == SPECIAL_NONCE_ONLY_NODES) {
            // If client only wants node info, jump directly to sending nodes
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                state = STATE_SEND_COMPLETE_ID; // Unauthorized: skip node DB
            } else
#endif
            {
                state = STATE_SEND_OTHER_NODEINFOS;
                onNowHasData(0);
            }
        } else {
            state = STATE_SEND_METADATA;
        }
        break;
    }

    case STATE_SEND_METADATA:
        LOG_DEBUG("Send device metadata");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_metadata_tag;
        fromRadioScratch.metadata = getDeviceMetadata();
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        if (!getAdminAuthorized()) {
            // DeviceMetadata is one large fingerprint vector for an unauth
            // client: firmware_version, device_state_version, hw_model,
            // hw_model_string, has_bluetooth/has_wifi/has_ethernet, role,
            // position_flags, excluded_modules, optionsCount. None of it
            // is needed to drive lockdown_auth, and most of it tells an
            // attacker which CVE / behavior quirks to probe. Wipe the
            // whole struct - clients re-fetch once authenticated.
            memset(&fromRadioScratch.metadata, 0, sizeof(fromRadioScratch.metadata));
        }
#endif
        state = STATE_SEND_REGION_PRESETS;
        break;

    case STATE_SEND_REGION_PRESETS:
        // Tell the client which modem presets are legal in each region so its UI
        // can block illegal region+preset combinations. This is public RF /
        // regulatory information (region and modem_preset are already in the
        // unauthenticated LoRa whitelist below), so it is sent unconditionally -
        // even an unauthorized/locked-down client can render a correct picker.
        LOG_DEBUG("Send region preset map");
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_region_presets_tag;
        getRegionPresetMap(fromRadioScratch.region_presets);
        state = STATE_SEND_CHANNELS;
        config_state = 0; // STATE_SEND_CHANNELS indexes channels starting at 0
        break;

    case STATE_SEND_CHANNELS:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_channel_tag;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        if (!getAdminAuthorized()) {
            // Unauthenticated: emit a zero-initialized Channel. fromRadioScratch
            // was memset(0) at the top of getFromRadio(), so leaving .channel
            // untouched gives the client an empty entry - no name, no PSK, no
            // role. Advances the state machine normally so config_complete_id
            // still fires.
        } else
#endif
        {
            fromRadioScratch.channel = channels.getByIndex(config_state);
        }
        config_state++;
        // Advance when we have sent all of our Channels
        if (config_state >= MAX_NUM_CHANNELS) {
            LOG_DEBUG("Send channels %d", config_state);
            state = STATE_SEND_CONFIG;
            config_state = _meshtastic_AdminMessage_ConfigType_MIN + 1;
        }
        break;

    case STATE_SEND_CONFIG:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_tag;
        switch (config_state) {
        case meshtastic_Config_device_tag:
            LOG_DEBUG("Send config: device");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_device_tag;
            fromRadioScratch.config.payload_variant.device = config.device;
            break;
        case meshtastic_Config_position_tag:
            LOG_DEBUG("Send config: position");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_position_tag;
            fromRadioScratch.config.payload_variant.position = config.position;
            break;
        case meshtastic_Config_power_tag:
            LOG_DEBUG("Send config: power");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_power_tag;
            fromRadioScratch.config.payload_variant.power = config.power;
            fromRadioScratch.config.payload_variant.power.ls_secs = default_ls_secs;
            break;
        case meshtastic_Config_network_tag:
            LOG_DEBUG("Send config: network");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_network_tag;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Unauthenticated: emit an empty NetworkConfig (zero-init from the
                // top-of-loop memset). No wifi_psk, no SSID, no static IP info.
            } else
#endif
            {
                fromRadioScratch.config.payload_variant.network = config.network;
            }
            break;
        case meshtastic_Config_display_tag:
            LOG_DEBUG("Send config: display");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_display_tag;
            fromRadioScratch.config.payload_variant.display = config.display;
            break;
        case meshtastic_Config_lora_tag:
            LOG_DEBUG("Send config: lora");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_lora_tag;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Whitelist only the spec-mandated radio identity fields that
                // are intrinsically observable on the air anyway: region,
                // modem_preset, use_preset, channel_num, hop_limit. Operator-
                // private knobs (ignore_incoming list, override_duty_cycle,
                // override_frequency, sx126x_rx_boosted_gain, tx_power,
                // ignore_mqtt, fem_lna_mode, config_ok_to_mqtt, ...) stay
                // hidden - they tell an attacker how the operator has tuned
                // the device but are not needed by an unauth client.
                meshtastic_Config_LoRaConfig whitelist = {};
                whitelist.use_preset = config.lora.use_preset;
                whitelist.modem_preset = config.lora.modem_preset;
                whitelist.region = config.lora.region;
                whitelist.channel_num = config.lora.channel_num;
                whitelist.hop_limit = config.lora.hop_limit;
                fromRadioScratch.config.payload_variant.lora = whitelist;
            } else
#endif
            {
                fromRadioScratch.config.payload_variant.lora = config.lora;
            }
            break;
        case meshtastic_Config_bluetooth_tag:
            LOG_DEBUG("Send config: bluetooth");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_bluetooth_tag;
            fromRadioScratch.config.payload_variant.bluetooth = config.bluetooth;
            break;
        case meshtastic_Config_security_tag:
            LOG_DEBUG("Send config: security");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_security_tag;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Unauthenticated: emit an empty SecurityConfig (zero-init from
                // the top-of-loop memset). No private_key, no admin_keys, no
                // public_key - nothing for an attacker to inspect.
                //
                // Provisioning state (NEEDS_PROVISION vs LOCKED) is conveyed via
                // the FromRadio.lockdown_status proto sent post-config; clients
                // should consume that rather than inferring from this empty
                // security config.
            } else
#endif
            {
                fromRadioScratch.config.payload_variant.security = config.security;
            }
            break;
        case meshtastic_Config_sessionkey_tag:
            LOG_DEBUG("Send config: sessionkey");
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_sessionkey_tag;
            break;
        case meshtastic_Config_device_ui_tag: // NOOP!
            fromRadioScratch.config.which_payload_variant = meshtastic_Config_device_ui_tag;
            break;
        default:
            LOG_ERROR("Unknown config type %d", config_state);
        }
        // NOTE: The phone app needs to know the ls_secs value so it can properly expect sleep behavior.
        // So even if we internally use 0 to represent 'use default' we still need to send the value we are
        // using to the app (so that even old phone apps work with new device loads).

        config_state++;
        // Advance when we have sent all of our config objects
        if (config_state > (_meshtastic_AdminMessage_ConfigType_MAX + 1)) {
            state = STATE_SEND_MODULECONFIG;
            config_state = _meshtastic_AdminMessage_ModuleConfigType_MIN + 1;
        }
        break;

    case STATE_SEND_MODULECONFIG:
        fromRadioScratch.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
        switch (config_state) {
        case meshtastic_ModuleConfig_mqtt_tag:
            LOG_DEBUG("Send module config: mqtt");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Unauthenticated: emit an empty MQTTConfig (zero-init from
                // the top-of-loop memset). MQTT broker username/password, the
                // server address, and root_topic are credentials/config that
                // shouldn't be visible to an unauth client.
            } else
#endif
            {
                fromRadioScratch.moduleConfig.payload_variant.mqtt = moduleConfig.mqtt;
            }
            break;
        case meshtastic_ModuleConfig_serial_tag:
            LOG_DEBUG("Send module config: serial");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
            fromRadioScratch.moduleConfig.payload_variant.serial = moduleConfig.serial;
            break;
        case meshtastic_ModuleConfig_external_notification_tag:
            LOG_DEBUG("Send module config: ext notification");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
            fromRadioScratch.moduleConfig.payload_variant.external_notification = moduleConfig.external_notification;
            break;
        case meshtastic_ModuleConfig_store_forward_tag:
            LOG_DEBUG("Send module config: store forward");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
            fromRadioScratch.moduleConfig.payload_variant.store_forward = moduleConfig.store_forward;
            break;
        case meshtastic_ModuleConfig_range_test_tag:
            LOG_DEBUG("Send module config: range test");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
            fromRadioScratch.moduleConfig.payload_variant.range_test = moduleConfig.range_test;
            break;
        case meshtastic_ModuleConfig_telemetry_tag:
            LOG_DEBUG("Send module config: telemetry");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
            fromRadioScratch.moduleConfig.payload_variant.telemetry = moduleConfig.telemetry;
            break;
        case meshtastic_ModuleConfig_canned_message_tag:
            LOG_DEBUG("Send module config: canned message");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
            fromRadioScratch.moduleConfig.payload_variant.canned_message = moduleConfig.canned_message;
            break;
        case meshtastic_ModuleConfig_audio_tag:
            LOG_DEBUG("Send module config: audio");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
            fromRadioScratch.moduleConfig.payload_variant.audio = moduleConfig.audio;
            break;
        case meshtastic_ModuleConfig_remote_hardware_tag:
            LOG_DEBUG("Send module config: remote hardware");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
            fromRadioScratch.moduleConfig.payload_variant.remote_hardware = moduleConfig.remote_hardware;
            break;
        case meshtastic_ModuleConfig_neighbor_info_tag:
            LOG_DEBUG("Send module config: neighbor info");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_neighbor_info_tag;
            fromRadioScratch.moduleConfig.payload_variant.neighbor_info = moduleConfig.neighbor_info;
            break;
        case meshtastic_ModuleConfig_detection_sensor_tag:
            LOG_DEBUG("Send module config: detection sensor");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_detection_sensor_tag;
            fromRadioScratch.moduleConfig.payload_variant.detection_sensor = moduleConfig.detection_sensor;
            break;
        case meshtastic_ModuleConfig_ambient_lighting_tag:
            LOG_DEBUG("Send module config: ambient lighting");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_ambient_lighting_tag;
            fromRadioScratch.moduleConfig.payload_variant.ambient_lighting = moduleConfig.ambient_lighting;
            break;
        case meshtastic_ModuleConfig_paxcounter_tag:
            LOG_DEBUG("Send module config: paxcounter");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_paxcounter_tag;
            fromRadioScratch.moduleConfig.payload_variant.paxcounter = moduleConfig.paxcounter;
            break;
        case meshtastic_ModuleConfig_traffic_management_tag:
            LOG_DEBUG("Send module config: traffic management");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_traffic_management_tag;
            fromRadioScratch.moduleConfig.payload_variant.traffic_management = moduleConfig.traffic_management;
            break;
        case meshtastic_ModuleConfig_tak_tag:
            LOG_DEBUG("Send module config: tak");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_tak_tag;
            fromRadioScratch.moduleConfig.payload_variant.tak = moduleConfig.tak;
            break;
#if !MESHTASTIC_EXCLUDE_BEACON
        case meshtastic_ModuleConfig_mesh_beacon_tag:
            LOG_DEBUG("Send module config: mesh beacon");
            fromRadioScratch.moduleConfig.which_payload_variant = meshtastic_ModuleConfig_mesh_beacon_tag;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Unauthenticated: emit an empty MeshBeaconConfig (zero-init from
                // the top-of-loop memset). The embedded ChannelSettings
                // (broadcast_offer_channel / broadcast_on_channel) carry PSKs that
                // must not be visible to an unauth client.
            } else
#endif
            {
                fromRadioScratch.moduleConfig.payload_variant.mesh_beacon = moduleConfig.mesh_beacon;
            }
            break;
#endif
        default:
            LOG_DEBUG("Unhandled module config type %d", config_state);
        }

        config_state++;
        // Advance when we have sent all of our ModuleConfig objects
        if (config_state > (_meshtastic_AdminMessage_ModuleConfigType_MAX + 1)) {
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                // Unauthorized client: skip node DB and file manifest - only send config complete
                state = STATE_SEND_COMPLETE_ID;
            } else
#endif
                // Handle special nonce behaviors:
                // - SPECIAL_NONCE_ONLY_CONFIG: Skip node info, go directly to file manifest
                // - SPECIAL_NONCE_ONLY_NODES: After sending nodes, skip to complete
                if (config_nonce == SPECIAL_NONCE_ONLY_CONFIG) {
                    state = STATE_SEND_FILEMANIFEST;
                } else {
                    state = STATE_SEND_OTHER_NODEINFOS;
                    onNowHasData(0);
                }
            config_state = 0;
        }
        break;

    case STATE_SEND_OTHER_NODEINFOS: {
        if (readIndex == 2) { //  readIndex==2 will be true for the first non-us node
            LOG_INFO("Start sending nodeinfos millis=%u", millis());
        }

        meshtastic_NodeInfo infoToSend = {};
        {
            concurrency::LockGuard guard(&nodeInfoMutex);
            if (nodeInfoForPhone.num == 0 && !nodeInfoQueue.empty()) {
                // Serve the next cached node without re-reading from the DB iterator.
                nodeInfoForPhone = nodeInfoQueue.front();
                nodeInfoQueue.pop_front();
            }
            infoToSend = nodeInfoForPhone;
            if (infoToSend.num != 0)
                nodeInfoForPhone = {};
        }

        if (infoToSend.num != 0) {
            // Just in case we stored a different user.id in the past, but should never happen going forward
            sprintf(infoToSend.user.id, "!%08x", infoToSend.num);

            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            fromRadioScratch.node_info = infoToSend;
            prefetchNodeInfos();
        } else {
            LOG_DEBUG("Done sending %d of %d nodeinfos millis=%u", readIndex, nodeDB->getNumMeshNodes(), millis());
            nodeInfoMutex.lock();
            nodeInfoQueue.clear();
            nodeInfoMutex.unlock();
            // Satellite-DB replay (positions/telemetry/environment/status) now happens
            // *after* config_complete_id, interleaved with live traffic in STATE_SEND_PACKETS.
            state = STATE_SEND_FILEMANIFEST;
            return getFromRadio(buf);
        }
        break;
    }

    case STATE_SEND_FILEMANIFEST: {
        LOG_DEBUG("FromRadio=STATE_SEND_FILEMANIFEST");
        // ONLY_NODES variants skip the manifest.
        if (config_state == filesManifest.size() || config_nonce == SPECIAL_NONCE_ONLY_NODES) {
            config_state = 0;
            releaseFilesManifest(filesManifest);
            // Skip to complete packet
            sendConfigComplete();
        } else {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_fileInfo_tag;
            fromRadioScratch.fileInfo = filesManifest.at(config_state);
            LOG_DEBUG("File: %s (%d) bytes", fromRadioScratch.fileInfo.file_name, fromRadioScratch.fileInfo.size_bytes);
            config_state++;
        }
        break;
    }

    case STATE_SEND_COMPLETE_ID:
        sendConfigComplete();
        break;

    case STATE_SEND_PACKETS:
        pauseBluetoothLogging = false;
        // Do we have a message from the mesh or packet from the local device?
        LOG_DEBUG("FromRadio=STATE_SEND_PACKETS");
        if (queueStatusPacketForPhone) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            fromRadioScratch.queueStatus = *queueStatusPacketForPhone;
            releaseQueueStatusPhonePacket();
        } else if (mqttClientProxyMessageForPhone) {
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                releaseMqttClientProxyPhonePacket(); // Discard - unauthorized client
            } else
#endif
            {
                fromRadioScratch.which_payload_variant = meshtastic_FromRadio_mqttClientProxyMessage_tag;
                fromRadioScratch.mqttClientProxyMessage = *mqttClientProxyMessageForPhone;
                releaseMqttClientProxyPhonePacket();
            }
        } else if (xmodemPacketForPhone.control != meshtastic_XModem_Control_NUL) {
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                xmodemPacketForPhone = meshtastic_XModem_init_zero; // Discard - unauthorized client
            } else
#endif
            {
                fromRadioScratch.which_payload_variant = meshtastic_FromRadio_xmodemPacket_tag;
                fromRadioScratch.xmodemPacket = xmodemPacketForPhone;
                xmodemPacketForPhone = meshtastic_XModem_init_zero;
            }
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        } else if (hasPendingLockdownStatus()) {
            concurrency::LockGuard guard(&g_authSlotsMutex);
            // Look up our own slot only - never another connection's. Re-check
            // hasPending under the lock since a concurrent drain on the same
            // connection (unlikely but possible if multiple transport
            // callbacks race against one PhoneAPI) may have grabbed it.
            if (auto *slot = findOrAllocStatusSlot_LH(this); slot && slot->hasPending) {
                fromRadioScratch.which_payload_variant = meshtastic_FromRadio_lockdown_status_tag;
                fromRadioScratch.lockdown_status = slot->status;
                memset(&slot->status, 0, sizeof(slot->status));
                slot->hasPending = false;
            }
#endif
        } else if (clientNotification) {
            fromRadioScratch.which_payload_variant = meshtastic_FromRadio_clientNotification_tag;
            fromRadioScratch.clientNotification = *clientNotification;
            releaseClientNotification();
        } else if (packetForPhone) {
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
            if (!getAdminAuthorized()) {
                releasePhonePacket(); // Discard mesh traffic - unauthorized client
            } else
#endif
            {
                printPacket("phone downloaded packet", packetForPhone);
                // Encapsulate as a FromRadio packet
                fromRadioScratch.which_payload_variant = meshtastic_FromRadio_packet_tag;
                fromRadioScratch.packet = *packetForPhone;
                releasePhonePacket();
            }
        } else if (replayPending()) {
            // No live packet pending - feed the phone one cached satellite-DB packet.
            // popReplayPacket advances through positions->telemetry->environment->status,
            // and flips replayPhase back to IDLE when everything has been drained.
            meshtastic_MeshPacket replayPkt;
            if (popReplayPacket(replayPkt)) {
                printPacket("replay packet to phone", &replayPkt);
                fromRadioScratch.which_payload_variant = meshtastic_FromRadio_packet_tag;
                fromRadioScratch.packet = replayPkt;
            }
        }
        break;

    default:
        LOG_ERROR("getFromRadio unexpected state %d", state);
    }

    // Do we have a message from the mesh?
    if (fromRadioScratch.which_payload_variant != 0) {
        // Encapsulate as a FromRadio packet
        size_t numbytes = pb_encode_to_bytes(buf, meshtastic_FromRadio_size, &meshtastic_FromRadio_msg, &fromRadioScratch);

        // VERY IMPORTANT to not print debug messages while writing to fromRadioScratch - because we use that same buffer
        // for logging (when we are encapsulating with protobufs)
        return numbytes;
    }

    LOG_DEBUG("No FromRadio packet available");
    return 0;
}

void PhoneAPI::sendConfigComplete()
{
    LOG_INFO("Config Send Complete millis=%u", millis());
    const bool shouldReplaySatellites = (config_nonce != SPECIAL_NONCE_ONLY_CONFIG);
    // The phone sees config_complete_id first (treats sync as done), then the cached
    // satellite-DB packets (positions / telemetry / environment / status) trickle in
    // afterward as ordinary mesh packets (except SPECIAL_NONCE_ONLY_CONFIG, which
    // skips node/satellite sync entirely). Any client that handles live POSITION_APP /
    // TELEMETRY_APP / NODE_STATUS_APP packets handles these identically. STM32WL and
    // other builds that compile the satellite DBs out produce no replay packets and
    // the phase advances to IDLE in microseconds.
    fromRadioScratch.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    fromRadioScratch.config_complete_id = config_nonce;
    config_nonce = 0;
    state = STATE_SEND_PACKETS;
    replayPhase = shouldReplaySatellites ? REPLAY_PHASE_POSITIONS : REPLAY_PHASE_IDLE;
    if (api_type == TYPE_BLE) {
        service->api_state = service->STATE_BLE;
    } else if (api_type == TYPE_WIFI) {
        service->api_state = service->STATE_WIFI;
    } else if (api_type == TYPE_SERIAL) {
        service->api_state = service->STATE_SERIAL;
    } else if (api_type == TYPE_PACKET) {
        service->api_state = service->STATE_PACKET;
    } else if (api_type == TYPE_HTTP) {
        service->api_state = service->STATE_HTTP;
    } else if (api_type == TYPE_ETH) {
        service->api_state = service->STATE_ETH;
    }

#if defined(MESHTASTIC_ENCRYPTED_STORAGE) && defined(MESHTASTIC_PHONEAPI_ACCESS_CONTROL)
    if (!EncryptedStorage::isLockdownActive()) {
        // Lockdown-capable firmware, but lockdown is not active on this
        // device (never provisioned, or disabled). Tell the client so its
        // "lockdown mode" toggle renders OFF. Note getAdminAuthorized()
        // returns true in this state, so the redaction gates are no-ops and
        // the client just received the full, unredacted config above.
        queueLockdownStatus(meshtastic_LockdownStatus_State_DISABLED, "", 0, 0, 0);
        LOG_INFO("PhoneAPI: DISABLED (lockdown not active) sent to client");
    } else if (!getAdminAuthorized()) {
        if (!EncryptedStorage::isProvisioned()) {
            queueLockdownStatus(meshtastic_LockdownStatus_State_NEEDS_PROVISION, "", 0, 0, 0);
            LOG_INFO("PhoneAPI: NEEDS_PROVISION sent to client");
        } else if (!EncryptedStorage::isUnlocked()) {
            queueLockdownStatus(meshtastic_LockdownStatus_State_LOCKED, EncryptedStorage::getLockReason(), 0, 0, 0);
            LOG_INFO("PhoneAPI: LOCKED (%s) sent to client", EncryptedStorage::getLockReason());
        } else {
            queueLockdownStatus(meshtastic_LockdownStatus_State_LOCKED, "needs_auth", 0, 0, 0);
            LOG_INFO("PhoneAPI: LOCKED (needs_auth) sent to client");
        }
    }
#endif

    // Allow subclasses to know we've entered steady-state so they can lower power consumption
    onConfigComplete();

    pauseBluetoothLogging = false;
}

void PhoneAPI::releasePhonePacket()
{
    if (packetForPhone) {
        service->releaseToPool(packetForPhone); // we just copied the bytes, so don't need this buffer anymore
        packetForPhone = NULL;
    }
}

void PhoneAPI::releaseQueueStatusPhonePacket()
{
    if (queueStatusPacketForPhone) {
        service->releaseQueueStatusToPool(queueStatusPacketForPhone);
        queueStatusPacketForPhone = NULL;
    }
}

void PhoneAPI::prefetchNodeInfos()
{
    bool added = false;
    bool wasEmpty = false;
    // Other-node NodeInfos always go out thin (no bundled position/device_metrics).
    // The post-config_complete_id replay drain delivers those as ordinary mesh packets.
    // Keep the queue topped up so BLE reads stay responsive even if DB fetches take a moment.
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        wasEmpty = nodeInfoQueue.empty();
        while (nodeInfoQueue.size() < kNodePrefetchDepth) {
            auto nextNode = nodeDB->readNextMeshNode(readIndex);
            if (!nextNode)
                break;

            auto info = TypeConversions::ConvertToNodeInfoThin(nextNode);
            bool isUs = info.num == nodeDB->getNodeNum();
            info.hops_away = isUs ? 0 : info.hops_away;
            info.last_heard = isUs ? getValidTime(RTCQualityFromNet) : info.last_heard;
            info.snr = isUs ? 0 : info.snr;
            info.via_mqtt = isUs ? false : info.via_mqtt;
            info.is_favorite = info.is_favorite || isUs;
            nodeInfoQueue.push_back(info);
            // Log progress here (at fetch time) so readIndex is accurate and each value logs only once.
            if (readIndex == 2 || readIndex % 20 == 0) {
                LOG_DEBUG("nodeinfo: %d/%d", readIndex, nodeDB->getNumMeshNodes());
            }
            added = true;
        }
    }

    if (added && wasEmpty)
        onNowHasData(0);
}

meshtastic_MeshPacket PhoneAPI::makeReplayPositionPacket(NodeNum num, const meshtastic_PositionLite &pos)
{
    // Shape this exactly like a fresh live broadcast Position from the peer so the
    // phone runs it through its normal "live position broadcast" handler path.
    // to=ourNum would read as a DM-from-peer and never lands in node detail UI.
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_default;
    pkt.from = num;
    pkt.to = NODENUM_BROADCAST;
    pkt.id = generatePacketId();
    pkt.rx_time = pos.time;
    pkt.channel = 0;
    pkt.hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
    pkt.hop_start = pkt.hop_limit;
    pkt.priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    // Mark as if heard over the air, not internally generated
    pkt.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_POSITION_APP;
    meshtastic_Position fullPos = TypeConversions::ConvertToPosition(pos);
    size_t len =
        pb_encode_to_bytes(pkt.decoded.payload.bytes, sizeof(pkt.decoded.payload.bytes), &meshtastic_Position_msg, &fullPos);
    pkt.decoded.payload.size = (pb_size_t)len;
    return pkt;
}

meshtastic_MeshPacket PhoneAPI::makeReplayTelemetryPacket(NodeNum num, const meshtastic_DeviceMetrics &metrics)
{
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_default;
    pkt.from = num;
    pkt.to = NODENUM_BROADCAST;
    pkt.id = generatePacketId();
    // No native timestamp on telemetry packets here; use last_heard.
    const meshtastic_NodeInfoLite *header = nodeDB->getMeshNode(num);
    pkt.rx_time = header ? header->last_heard : 0;
    pkt.channel = 0;
    pkt.hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
    pkt.hop_start = pkt.hop_limit;
    pkt.priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    // Mark as if heard over the air, not internally generated - iOS client filters
    // TRANSPORT_INTERNAL packets out of broadcast peer state updates.
    pkt.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    meshtastic_Telemetry fullTel = meshtastic_Telemetry_init_default;
    fullTel.time = pkt.rx_time;
    fullTel.which_variant = meshtastic_Telemetry_device_metrics_tag;
    fullTel.variant.device_metrics = metrics;
    size_t len =
        pb_encode_to_bytes(pkt.decoded.payload.bytes, sizeof(pkt.decoded.payload.bytes), &meshtastic_Telemetry_msg, &fullTel);
    pkt.decoded.payload.size = (pb_size_t)len;
    return pkt;
}

void PhoneAPI::beginReplayPositions()
{
#if MESHTASTIC_EXCLUDE_POSITIONDB
    // Build excluded entirely - leave the order list empty so the phase
    // immediately drains and advances.
    replayPositionOrder.clear();
    replayPositionIndex = 0;
#else
    // Caller (popReplayPacket) only invokes us when replayPhase is armed.
    // Snapshot the keyset at phase start so concurrent inserts/erases on the
    // map don't invalidate iteration. Skip our own node - the phone already
    // got our position bundled in STATE_SEND_OWN_NODEINFO.
    replayPositionOrder = nodeDB->snapshotPositionNodeNums(nodeDB->getNodeNum());
    replayPositionIndex = 0;
    LOG_INFO("Begin position replay: %u entries millis=%u", (unsigned)replayPositionOrder.size(), millis());
#endif
}

void PhoneAPI::prefetchReplayPositions()
{
#if MESHTASTIC_EXCLUDE_POSITIONDB
    return;
#else
    bool added = false;
    bool wasEmpty = false;
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        wasEmpty = replayQueue.empty();
        while (replayQueue.size() < kReplayPrefetchDepth && replayPositionIndex < replayPositionOrder.size()) {
            NodeNum num = replayPositionOrder[replayPositionIndex++];
            meshtastic_PositionLite pos;
            if (!nodeDB->copyNodePosition(num, pos))
                continue; // entry was evicted between snapshot and now
            replayQueue.push_back(makeReplayPositionPacket(num, pos));
            added = true;
        }
    }
    if (added && wasEmpty)
        onNowHasData(0);
#endif
}

void PhoneAPI::beginReplayTelemetry()
{
#if MESHTASTIC_EXCLUDE_TELEMETRYDB
    replayTelemetryOrder.clear();
    replayTelemetryIndex = 0;
#else
    replayTelemetryOrder = nodeDB->snapshotTelemetryNodeNums(nodeDB->getNodeNum());
    replayTelemetryIndex = 0;
    LOG_INFO("Begin telemetry replay: %u entries millis=%u", (unsigned)replayTelemetryOrder.size(), millis());
#endif
}

void PhoneAPI::prefetchReplayTelemetry()
{
#if MESHTASTIC_EXCLUDE_TELEMETRYDB
    return;
#else
    bool added = false;
    bool wasEmpty = false;
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        wasEmpty = replayQueue.empty();
        while (replayQueue.size() < kReplayPrefetchDepth && replayTelemetryIndex < replayTelemetryOrder.size()) {
            NodeNum num = replayTelemetryOrder[replayTelemetryIndex++];
            meshtastic_DeviceMetrics dm;
            if (!nodeDB->copyNodeTelemetry(num, dm))
                continue;
            replayQueue.push_back(makeReplayTelemetryPacket(num, dm));
            added = true;
        }
    }
    if (added && wasEmpty)
        onNowHasData(0);
#endif
}

meshtastic_MeshPacket PhoneAPI::makeReplayEnvironmentPacket(uint32_t num, const meshtastic_EnvironmentMetrics &env)
{
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_default;
    pkt.from = num;
    pkt.to = NODENUM_BROADCAST;
    pkt.id = generatePacketId();
    const meshtastic_NodeInfoLite *header = nodeDB->getMeshNode(num);
    pkt.rx_time = header ? header->last_heard : 0;
    pkt.channel = 0;
    pkt.hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
    pkt.hop_start = pkt.hop_limit;
    pkt.priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    // Mark as if heard over the air, not internally generated - iOS client filters
    // TRANSPORT_INTERNAL packets out of broadcast peer state updates.
    pkt.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    meshtastic_Telemetry fullTel = meshtastic_Telemetry_init_default;
    fullTel.time = pkt.rx_time;
    fullTel.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    fullTel.variant.environment_metrics = env;
    size_t len =
        pb_encode_to_bytes(pkt.decoded.payload.bytes, sizeof(pkt.decoded.payload.bytes), &meshtastic_Telemetry_msg, &fullTel);
    pkt.decoded.payload.size = (pb_size_t)len;
    return pkt;
}

void PhoneAPI::beginReplayEnvironment()
{
#if MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    replayEnvironmentOrder.clear();
    replayEnvironmentIndex = 0;
#else
    replayEnvironmentOrder = nodeDB->snapshotEnvironmentNodeNums(nodeDB->getNodeNum());
    replayEnvironmentIndex = 0;
    LOG_INFO("Begin environment replay: %u entries millis=%u", (unsigned)replayEnvironmentOrder.size(), millis());
#endif
}

void PhoneAPI::prefetchReplayEnvironment()
{
#if MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    return;
#else
    bool added = false;
    bool wasEmpty = false;
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        wasEmpty = replayQueue.empty();
        while (replayQueue.size() < kReplayPrefetchDepth && replayEnvironmentIndex < replayEnvironmentOrder.size()) {
            NodeNum num = replayEnvironmentOrder[replayEnvironmentIndex++];
            meshtastic_EnvironmentMetrics env;
            if (!nodeDB->copyNodeEnvironment(num, env))
                continue;
            replayQueue.push_back(makeReplayEnvironmentPacket(num, env));
            added = true;
        }
    }
    if (added && wasEmpty)
        onNowHasData(0);
#endif
}

meshtastic_MeshPacket PhoneAPI::makeReplayStatusPacket(uint32_t num, const meshtastic_StatusMessage &status)
{
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_default;
    pkt.from = num;
    pkt.to = NODENUM_BROADCAST;
    pkt.id = generatePacketId();
    // StatusMessage has no native timestamp; use last_heard.
    const meshtastic_NodeInfoLite *header = nodeDB->getMeshNode(num);
    pkt.rx_time = header ? header->last_heard : 0;
    pkt.channel = 0;
    pkt.hop_limit = Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit);
    pkt.hop_start = pkt.hop_limit;
    pkt.priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    // Mark as if heard over the air, not internally generated - client filters
    pkt.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    pkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt.decoded.portnum = meshtastic_PortNum_NODE_STATUS_APP;
    size_t len =
        pb_encode_to_bytes(pkt.decoded.payload.bytes, sizeof(pkt.decoded.payload.bytes), &meshtastic_StatusMessage_msg, &status);
    pkt.decoded.payload.size = (pb_size_t)len;
    return pkt;
}

void PhoneAPI::beginReplayStatus()
{
#if MESHTASTIC_EXCLUDE_STATUSDB
    replayStatusOrder.clear();
    replayStatusIndex = 0;
#else
    replayStatusOrder = nodeDB->snapshotStatusNodeNums(nodeDB->getNodeNum());
    replayStatusIndex = 0;
    LOG_INFO("Begin status replay: %u entries millis=%u", (unsigned)replayStatusOrder.size(), millis());
#endif
}

void PhoneAPI::prefetchReplayStatus()
{
#if MESHTASTIC_EXCLUDE_STATUSDB
    return;
#else
    bool added = false;
    bool wasEmpty = false;
    {
        concurrency::LockGuard guard(&nodeInfoMutex);
        wasEmpty = replayQueue.empty();
        while (replayQueue.size() < kReplayPrefetchDepth && replayStatusIndex < replayStatusOrder.size()) {
            NodeNum num = replayStatusOrder[replayStatusIndex++];
            meshtastic_StatusMessage status;
            if (!nodeDB->copyNodeStatus(num, status) || status.status[0] == '\0')
                continue;
            replayQueue.push_back(makeReplayStatusPacket(num, status));
            added = true;
        }
    }
    if (added && wasEmpty)
        onNowHasData(0);
#endif
}

// Pop one cached satellite-DB packet from the active replay phase.
// Phases drain in order: positions -> telemetry -> environment -> status.
// When the current phase's cursor is exhausted (queue empty AND no more entries
// to snapshot), advance to the next phase. When all four phases are done,
// flip replayPhase back to IDLE and release the snapshot vectors.
//
// Returns true if a packet was placed in `out`; false if everything is drained.
bool PhoneAPI::popReplayPacket(meshtastic_MeshPacket &out)
{
    while (replayPhase != REPLAY_PHASE_IDLE) {
        // Prime the active phase: seed the snapshot vector on first entry,
        // top up replayQueue from the snapshot up to kReplayPrefetchDepth.
        switch (replayPhase) {
        case REPLAY_PHASE_POSITIONS:
            if (replayPositionOrder.empty() && replayPositionIndex == 0)
                beginReplayPositions();
            prefetchReplayPositions();
            break;
        case REPLAY_PHASE_TELEMETRY:
            if (replayTelemetryOrder.empty() && replayTelemetryIndex == 0)
                beginReplayTelemetry();
            prefetchReplayTelemetry();
            break;
        case REPLAY_PHASE_ENVIRONMENT:
            if (replayEnvironmentOrder.empty() && replayEnvironmentIndex == 0)
                beginReplayEnvironment();
            prefetchReplayEnvironment();
            break;
        case REPLAY_PHASE_STATUS:
            if (replayStatusOrder.empty() && replayStatusIndex == 0)
                beginReplayStatus();
            prefetchReplayStatus();
            break;
        default:
            break;
        }

        {
            concurrency::LockGuard guard(&nodeInfoMutex);
            if (!replayQueue.empty()) {
                out = replayQueue.front();
                replayQueue.pop_front();
                return true;
            }
        }

        // Queue empty AND no more entries to feed it - phase is exhausted.
        advanceReplayPhase();
    }
    return false;
}

void PhoneAPI::advanceReplayPhase()
{
    switch (replayPhase) {
    case REPLAY_PHASE_POSITIONS:
        LOG_DEBUG("Replay drain: positions done (count=%u) millis=%u", (unsigned)replayPositionIndex, millis());
        replayPhase = REPLAY_PHASE_TELEMETRY;
        break;
    case REPLAY_PHASE_TELEMETRY:
        LOG_DEBUG("Replay drain: telemetry done (count=%u) millis=%u", (unsigned)replayTelemetryIndex, millis());
        replayPhase = REPLAY_PHASE_ENVIRONMENT;
        break;
    case REPLAY_PHASE_ENVIRONMENT:
        LOG_DEBUG("Replay drain: environment done (count=%u) millis=%u", (unsigned)replayEnvironmentIndex, millis());
        replayPhase = REPLAY_PHASE_STATUS;
        break;
    case REPLAY_PHASE_STATUS:
        LOG_INFO("Replay drain complete (status count=%u) millis=%u", (unsigned)replayStatusIndex, millis());
        replayPositionOrder.clear();
        replayPositionOrder.shrink_to_fit();
        replayTelemetryOrder.clear();
        replayTelemetryOrder.shrink_to_fit();
        replayEnvironmentOrder.clear();
        replayEnvironmentOrder.shrink_to_fit();
        replayStatusOrder.clear();
        replayStatusOrder.shrink_to_fit();
        replayPositionIndex = 0;
        replayTelemetryIndex = 0;
        replayEnvironmentIndex = 0;
        replayStatusIndex = 0;
        replayPhase = REPLAY_PHASE_IDLE;
        break;
    default:
        break;
    }
}

void PhoneAPI::releaseMqttClientProxyPhonePacket()
{
    if (mqttClientProxyMessageForPhone) {
        service->releaseMqttClientProxyMessageToPool(mqttClientProxyMessageForPhone);
        mqttClientProxyMessageForPhone = NULL;
    }
}

void PhoneAPI::releaseClientNotification()
{
    if (clientNotification) {
        service->releaseClientNotificationToPool(clientNotification);
        clientNotification = NULL;
    }
}

/**
 * Return true if we have data available to send to the phone
 */
bool PhoneAPI::available()
{
    switch (state) {
    case STATE_SEND_NOTHING:
        return false;
    case STATE_SEND_MY_INFO:
    case STATE_SEND_UIDATA:
    case STATE_SEND_CHANNELS:
    case STATE_SEND_CONFIG:
    case STATE_SEND_MODULECONFIG:
    case STATE_SEND_METADATA:
    case STATE_SEND_REGION_PRESETS:
    case STATE_SEND_OWN_NODEINFO:
    case STATE_SEND_FILEMANIFEST:
    case STATE_SEND_COMPLETE_ID:
        return true;

    case STATE_SEND_OTHER_NODEINFOS: {
        concurrency::LockGuard guard(&nodeInfoMutex);
        if (nodeInfoQueue.empty()) {
            // Drop the lock before prefetching; prefetchNodeInfos() will re-acquire it.
            goto PREFETCH_NODEINFO;
        }
    }
        return true; // Always say we have something, because we might need to advance our state machine
    PREFETCH_NODEINFO:
        prefetchNodeInfos();
        return true;
    case STATE_SEND_PACKETS: {
        if (!queueStatusPacketForPhone)
            queueStatusPacketForPhone = service->getQueueStatusForPhone();
        if (!mqttClientProxyMessageForPhone)
            mqttClientProxyMessageForPhone = service->getMqttClientProxyMessageForPhone();
        if (!clientNotification)
            clientNotification = service->getClientNotificationForPhone();
        bool hasPacket = !!queueStatusPacketForPhone || !!mqttClientProxyMessageForPhone || !!clientNotification;
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
        if (hasPendingLockdownStatus())
            hasPacket = true;
#endif
        if (hasPacket)
            return true;

#ifdef FSCom
        if (xmodemPacketForPhone.control == meshtastic_XModem_Control_NUL)
            xmodemPacketForPhone = xModem.getForPhone();
        if (xmodemPacketForPhone.control != meshtastic_XModem_Control_NUL) {
            xModem.resetForPhone();
            return true;
        }
#endif

#ifdef ARCH_ESP32
#if !MESHTASTIC_EXCLUDE_STOREFORWARD
        // Check if StoreForward has packets stored for us.
        if (!packetForPhone && storeForwardModule)
            packetForPhone = storeForwardModule->getForPhone();
#endif
#endif

        if (!packetForPhone)
            packetForPhone = service->getForPhone();
        hasPacket = !!packetForPhone;
        if (hasPacket)
            return true;
        // Trailing replay drain - feeds cached satellite-DB packets alongside
        // (lower priority than) live traffic.
        return replayPending();
    }
    default:
        LOG_ERROR("PhoneAPI::available unexpected state %d", state);
    }

    return false;
}

void PhoneAPI::sendNotification(meshtastic_LogRecord_Level level, uint32_t replyId, const char *message)
{
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    cn->has_reply_id = true;
    cn->reply_id = replyId;
    cn->level = meshtastic_LogRecord_Level_WARNING;
    cn->time = getValidTime(RTCQualityFromNet);
    strncpy(cn->message, message, sizeof(cn->message));
    service->sendClientNotification(cn);
}

bool PhoneAPI::wasSeenRecently(uint32_t id)
{
    for (int i = 0; i < 20; i++) {
        if (recentToRadioPacketIds[i] == id) {
            return true;
        }
        if (recentToRadioPacketIds[i] == 0) {
            recentToRadioPacketIds[i] = id;
            return false;
        }
    }
    // If the array is full, shift all elements to the left and add the new id at the end
    memmove(recentToRadioPacketIds, recentToRadioPacketIds + 1, (19) * sizeof(uint32_t));
    recentToRadioPacketIds[19] = id;
    return false;
}

/**
 * Handle a packet that the phone wants us to send.  It is our responsibility to free the packet to the pool
 */
bool PhoneAPI::handleToRadioPacket(meshtastic_MeshPacket &p)
{
    printPacket("PACKET FROM PHONE", &p);

#if defined(MESHTASTIC_ENCRYPTED_STORAGE) && defined(MESHTASTIC_PHONEAPI_ACCESS_CONTROL)
    // Local admin gating happens here, synchronously on the dispatching
    // task. Two distinct cases:
    //
    //   (a) lockdown_auth: handled inline. Passphrase never enters the
    //       routed MeshPacket queue, and authorize-this-connection
    //       runs while `this` is still on the call stack.
    //
    //   (b) Any other admin payload from an unauthorized connection:
    //       dropped here. The previous design relied on AdminModule
    //       to apply isLocalAdminAuthorized() during dispatch, but
    //       AdminModule runs on the Router task - by then the
    //       PhoneAPI dispatching task has already exited and the
    //       per-connection auth context is unrecoverable. Putting
    //       the gate here closes that race and covers H6/H7 from the
    //       audit: get_config_request and set_config from unauthed
    //       clients no longer reach AdminModule at all.
    if (p.from == 0 && p.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p.decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
        meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
        if (pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_AdminMessage_msg, &admin)) {
            if (admin.which_payload_variant == meshtastic_AdminMessage_lockdown_auth_tag) {
                handleLockdownAuthInline(admin.lockdown_auth);
                // Wipe the decoded passphrase scratch - the byte array in
                // p.decoded.payload.bytes is wiped by handleLockdownAuthInline.
                volatile uint8_t *adminVol = const_cast<volatile uint8_t *>(admin.lockdown_auth.passphrase.bytes);
                for (size_t i = 0; i < sizeof(admin.lockdown_auth.passphrase.bytes); i++)
                    adminVol[i] = 0;
                return true;
            }
            if (!getAdminAuthorized()) {
                LOG_WARN("Lockdown: dropping admin payload variant=%d from unauthorized connection", admin.which_payload_variant);
                return false;
            }
        }
        // pb_decode failure: fall through to normal handling so the
        // regular Router/AdminModule reject path can respond.
    }
#endif

#if defined(ARCH_PORTDUINO)
    // For use with the simulator, we should not ignore duplicate packets from the phone
    if (SimRadio::instance == nullptr)
#endif
        if (p.id > 0 && wasSeenRecently(p.id)) {
            LOG_DEBUG("Ignore packet from phone, already seen recently");
            return false;
        }

    if (p.decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP && lastPortNumToRadio[p.decoded.portnum] &&
        Throttle::isWithinTimespanMs(lastPortNumToRadio[p.decoded.portnum], THIRTY_SECONDS_MS)) {
        LOG_WARN("Rate limit portnum %d", p.decoded.portnum);
        sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "TraceRoute can only be sent once every 30 seconds");
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        return false;
    } else if (p.decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP && isBroadcast(p.to) && p.hop_limit > 0) {
        sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "Multi-hop traceroute to broadcast address is not allowed");
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        return false;
    } else if (IS_ONE_OF(p.decoded.portnum, meshtastic_PortNum_POSITION_APP, meshtastic_PortNum_WAYPOINT_APP,
                         meshtastic_PortNum_ALERT_APP, meshtastic_PortNum_TELEMETRY_APP) &&
               lastPortNumToRadio[p.decoded.portnum] &&
               Throttle::isWithinTimespanMs(lastPortNumToRadio[p.decoded.portnum], TEN_SECONDS_MS)) {
        // TODO: [Issue #6700] Make this rate limit throttling scale up / down with the preset
        LOG_WARN("Rate limit portnum %d", p.decoded.portnum);
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        // FIXME: Figure out why this continues to happen
        // sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "Position can only be sent once every 5 seconds");
        return false;
    } else if (p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && lastPortNumToRadio[p.decoded.portnum] &&
               Throttle::isWithinTimespanMs(lastPortNumToRadio[p.decoded.portnum], TWO_SECONDS_MS)) {
        LOG_WARN("Rate limit portnum %d", p.decoded.portnum);
        meshtastic_QueueStatus qs = router->getQueueStatus();
        service->sendQueueStatusToPhone(qs, 0, p.id);
        service->sendRoutingErrorResponse(meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED, &p);
        // sendNotification(meshtastic_LogRecord_Level_WARNING, p.id, "Text messages can only be sent once every 2 seconds");
        return false;
    }

    // Upgrade traceroute requests from phone to use reliable delivery, matching TraceRouteModule
    if (p.decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP && !isBroadcast(p.to)) {
        // Use reliable delivery for traceroute requests (which will be copied to traceroute responses by setReplyTo)
        p.want_ack = true;
    }

    lastPortNumToRadio[p.decoded.portnum] = millis();
    service->handleToRadio(p);
    return true;
}

/// If the mesh service tells us fromNum has changed, tell the phone
int PhoneAPI::onNotify(uint32_t newValue)
{
    bool timeout = checkConnectionTimeout(); // a handy place to check if we've heard from the phone (since the BLE version
                                             // doesn't call this from idle)

    if (state == STATE_SEND_PACKETS) {
        LOG_INFO("Tell client we have new packets %u", newValue);
        onNowHasData(newValue);
    } else {
        LOG_DEBUG("Client not yet interested in packets (state=%d)", state);
    }

    return timeout ? -1 : 0; // If we timed out, MeshService should stop iterating through observers as we just removed one
}

#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL
bool PhoneAPI::getAdminAuthorized() const
{
    // Runtime-toggle model: when lockdown is NOT active (a lockdown-capable
    // build that hasn't been provisioned, or that was disabled), there is
    // nothing to protect - every connection is implicitly authorized, so
    // all the `if (!getAdminAuthorized())` redaction gates throughout
    // getFromRadio() / handleToRadio() become no-ops and the device serves
    // config exactly like stock firmware. Only once provisioned (lockdown
    // active) do we consult the per-connection auth slot table.
#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (!EncryptedStorage::isLockdownActive())
        return true;
#endif
    concurrency::LockGuard g(&g_authSlotsMutex);
    // const_cast is safe - findOrAllocSlot_LH only mutates the slot table,
    // not the PhoneAPI itself, and the table key is just the pointer.
    const auto *slot = findOrAllocSlot_LH(const_cast<PhoneAPI *>(this));
    return slot && slot->authorized && slot->epoch == g_authEpoch;
}

void PhoneAPI::setAdminAuthorized(bool authorized)
{
    concurrency::LockGuard g(&g_authSlotsMutex);
    auto *slot = findOrAllocSlot_LH(this);
    if (!slot)
        return; // slot table full - fail-closed
    if (authorized) {
        slot->epoch = g_authEpoch;
        slot->authorized = true;
    } else {
        slot->authorized = false;
        slot->epoch = 0;
    }
}

void PhoneAPI::revokeAllAuth()
{
    {
        concurrency::LockGuard g(&g_authSlotsMutex);
        g_authEpoch++;
    }
    LOG_INFO("Lockdown: All connection auth revoked (Lock Now)");
}

void PhoneAPI::completePendingUnlocks(bool reloadOk)
{
    // Snapshot fields that we'll need outside the lock (we cannot call
    // EncryptedStorage / setAdminAuthorized / unlockScreen while holding
    // g_authSlotsMutex without risking re-entry - setAdminAuthorized
    // itself takes the same lock).
    constexpr size_t kMaxSnapshots = MAX_AUTH_SLOTS;
    PhoneAPI *targets[kMaxSnapshots] = {};
    size_t targetCount = 0;
    {
        concurrency::LockGuard guard(&g_authSlotsMutex);
        for (auto &s : g_statusSlots) {
            if (!s.pendingUnlockAfterReload || !s.who)
                continue;
            if (targetCount < kMaxSnapshots)
                targets[targetCount++] = s.who;
            // Clear the pending flag either way - failure path must not
            // leave it set so a subsequent successful reload retries
            // against the wrong PhoneAPI.
            s.pendingUnlockAfterReload = false;
        }
    }

    if (reloadOk) {
        uint8_t boots = EncryptedStorage::getBootsRemaining();
        uint32_t until = EncryptedStorage::getValidUntilEpoch();
        for (size_t i = 0; i < targetCount; i++) {
            PhoneAPI *p = targets[i];
            p->setAdminAuthorized(true);
            p->queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCKED, "", boots, until, 0);
        }
        // Screen-lock latch is cleared once any client successfully
        // unlocks - the operator has proven the passphrase. Matches the
        // re-verify path's behavior.
        if (targetCount > 0)
            meshtastic_security::unlockScreen();
        LOG_INFO("Lockdown: post-reload completion: authorized %u connection(s)", (unsigned)targetCount);
    } else {
        // Storage corrupt - emit LOCKED(storage_corrupt) to every slot
        // that was awaiting the unlock. setAdminAuthorized is NOT called
        // so the connection stays redacted and any set_config it sends
        // is dropped at the existing unauth gates. Caller (main.cpp) has
        // already lockNow'd storage and broadcast-revoked.
        for (size_t i = 0; i < targetCount; i++) {
            targets[i]->queueLockdownStatus(meshtastic_LockdownStatus_State_LOCKED, "storage_corrupt", 0, 0, 0);
        }
        LOG_ERROR("Lockdown: post-reload completion: storage corrupt, notified %u connection(s)", (unsigned)targetCount);
    }
}

void PhoneAPI::queueLockdownStatus(meshtastic_LockdownStatus_State state, const char *lock_reason, uint8_t boots_remaining,
                                   uint32_t valid_until_epoch, uint32_t backoff_seconds)
{
    {
        concurrency::LockGuard guard(&g_authSlotsMutex);
        auto *slot = findOrAllocStatusSlot_LH(this);
        if (!slot)
            return; // slot table exhausted - fail-closed, no status delivered
        buildStatus_LH(slot->status, state, lock_reason, boots_remaining, valid_until_epoch, backoff_seconds);
        slot->hasPending = true;
    }
    if (service)
        service->nudgeFromNum();
}

void PhoneAPI::broadcastLockdownStatus(meshtastic_LockdownStatus_State state, const char *lock_reason, uint8_t boots_remaining,
                                       uint32_t valid_until_epoch, uint32_t backoff_seconds)
{
    bool anyOverwritten = false;
    {
        concurrency::LockGuard guard(&g_authSlotsMutex);
        for (auto &s : g_statusSlots) {
            if (s.who) {
                buildStatus_LH(s.status, state, lock_reason, boots_remaining, valid_until_epoch, backoff_seconds);
                s.hasPending = true;
                anyOverwritten = true;
            }
        }
    }
    // Service nudge is shared across connections; one nudge wakes every
    // drainer. Skip if no connection currently has a slot.
    if (anyOverwritten && service)
        service->nudgeFromNum();
}

bool PhoneAPI::hasPendingLockdownStatus() const
{
    concurrency::LockGuard guard(&g_authSlotsMutex);
    for (const auto &s : g_statusSlots) {
        if (s.who == this && s.hasPending)
            return true;
    }
    return false;
}

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
bool PhoneAPI::handleLockdownAuthInline(const meshtastic_LockdownAuth &la)
{
    // Wipe passphrase bytes in the caller's decoded scratch on every exit.
    auto zeroPassphrase = [&]() {
        volatile uint8_t *ppVol = const_cast<volatile uint8_t *>(la.passphrase.bytes);
        for (pb_size_t zi = 0; zi < la.passphrase.size; zi++)
            ppVol[zi] = 0;
    };

    // Lock Now - only honored from a connection that has already proven
    // the passphrase. Unauthenticated clients used to be able to trigger
    // a reboot, which was a trivial local-presence DoS (any BLE/USB
    // attacker could brick-loop the device). Now lock_now requires
    // prior auth on this connection.
    if (la.lock_now) {
        if (!getAdminAuthorized()) {
            LOG_WARN("Lockdown: LOCK NOW from unauthorized connection - denied");
            queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCK_FAILED, "", 0, 0, 0);
            zeroPassphrase();
            return true;
        }
        LOG_INFO("Lockdown: LOCK NOW command received from authorized connection");
        EncryptedStorage::lockNow();
        revokeAllAuth();
        queueLockdownStatus(meshtastic_LockdownStatus_State_LOCKED, "", 0, 0, 0);
        zeroPassphrase();
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        return true;
    }

    // Disable lockdown entirely. Requires the passphrase (must prove
    // ownership before reverting at-rest encryption). We verify it here to
    // load the DEK, then hand the heavy decrypt-revert work to the main
    // loop via lockdownDisablePending - exactly like the unlock reload
    // path, because decrypting + rewriting nodes.proto is too heavy for
    // this transport-callback stack. APPROTECT is NOT reversed.
    if (la.disable) {
        if (la.passphrase.size < 1) {
            LOG_WARN("Lockdown: disable with empty passphrase - rejecting");
            queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCK_FAILED, "", 0, 0, 0);
            zeroPassphrase();
            return true;
        }
        if (!EncryptedStorage::isLockdownActive()) {
            // Already off - nothing to do; report DISABLED so the client UI settles.
            LOG_INFO("Lockdown: disable requested but lockdown is not active");
            queueLockdownStatus(meshtastic_LockdownStatus_State_DISABLED, "", 0, 0, 0);
            zeroPassphrase();
            return true;
        }
        // Re-verify the passphrase (loads the DEK needed to decrypt files).
        bool ok = EncryptedStorage::unlockWithPassphrase(la.passphrase.bytes, la.passphrase.size,
                                                         EncryptedStorage::TOKEN_DEFAULT_BOOTS, 0, 0);
        if (!ok) {
            uint32_t backoff = EncryptedStorage::getBackoffSecondsRemaining();
            queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCK_FAILED, "", 0, 0, backoff);
            LOG_WARN("Lockdown: disable passphrase verification failed");
            zeroPassphrase();
            return true;
        }
        setAdminAuthorized(true);
        lockdownDisablePending = true; // main loop runs nodeDB->disableLockdownToPlaintext() then reboots
        LOG_INFO("Lockdown: disable authorized, deferring decrypt-revert to main loop");
        zeroPassphrase();
        return true;
    }

    // Empty-passphrase auth was previously a silent success - clients
    // got no feedback and the device looked the same as it would after
    // an actual no-op. Emit UNLOCK_FAILED with no backoff so honest
    // clients can detect their own bug and an attacker still learns
    // nothing they wouldn't from any other failed attempt.
    if (la.passphrase.size < 1) {
        LOG_WARN("Lockdown: lockdown_auth with empty passphrase and lock_now=false - rejecting");
        queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCK_FAILED, "", 0, 0, 0);
        zeroPassphrase();
        return true;
    }

    // boots_remaining is uint32 on the wire but the token field is uint8.
    // Silently truncating (256 -> 0 -> default 50) hides a real client
    // bug. Reject explicitly so the client can correct its request.
    if (la.boots_remaining > 255) {
        LOG_WARN("Lockdown: boots_remaining=%u exceeds uint8 cap, rejecting", la.boots_remaining);
        queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCK_FAILED, "", 0, 0, 0);
        zeroPassphrase();
        return true;
    }

    uint8_t boots = la.boots_remaining != 0 ? (uint8_t)la.boots_remaining : EncryptedStorage::TOKEN_DEFAULT_BOOTS;
    uint32_t validUntilEpoch = la.valid_until_epoch;
    // Client-supplied session cap when present; otherwise the
    // firmware-side default. 0 from the client means "use firmware
    // default", consistent with the boots_remaining sentinel.
    uint32_t sessionMaxSeconds =
        la.max_session_seconds != 0 ? la.max_session_seconds : MESHTASTIC_LOCKDOWN_SESSION_DEFAULT_SECONDS;

    bool ok = false;
    bool needsReload = false;
    if (!EncryptedStorage::isUnlocked()) {
        if (!EncryptedStorage::isProvisioned()) {
            LOG_INFO("Lockdown: first-time provisioning with passphrase");
            ok = EncryptedStorage::provisionPassphrase(la.passphrase.bytes, la.passphrase.size, boots, validUntilEpoch,
                                                       sessionMaxSeconds);
        } else {
            LOG_INFO("Lockdown: unlock with passphrase");
            ok = EncryptedStorage::unlockWithPassphrase(la.passphrase.bytes, la.passphrase.size, boots, validUntilEpoch,
                                                        sessionMaxSeconds);
        }
        if (ok) {
            needsReload = true;
            // Mark this slot for the main-loop completion handler. Don't
            // authorize or emit UNLOCKED yet - `config` / `channelFile`
            // / `nodeDatabase` still hold the locked-default placeholders
            // installed by loadFromDisk()'s !isUnlocked() branch. If we
            // flipped the connection to authorized here, the client could
            // read those placeholders as if they were the operator's real
            // settings, or set_config write a corrupted baseline that
            // overwrites the real config when reloadFromDisk swaps them
            // in. completePendingUnlocks() runs on the main thread after
            // reloadFromDisk has populated the real values and the radio
            // has been reconfigured.
            {
                concurrency::LockGuard guard(&g_authSlotsMutex);
                if (auto *slot = findOrAllocStatusSlot_LH(this))
                    slot->pendingUnlockAfterReload = true;
            }
            lockdownReloadPending = true;
            LOG_INFO("Lockdown: storage unlocked, awaiting reload before client visibility");
        }
    } else {
        LOG_INFO("Lockdown: passphrase re-verify for admin authorization");
        ok = EncryptedStorage::unlockWithPassphrase(la.passphrase.bytes, la.passphrase.size, boots, validUntilEpoch,
                                                    sessionMaxSeconds);
        if (ok) {
            // Storage was already unlocked - no reload needed. Authorize
            // and surface UNLOCKED to the client immediately.
            setAdminAuthorized(true);
            LOG_INFO("Lockdown: passphrase verified, this connection authorized");
        }
    }

    if (ok && !needsReload) {
        // Re-verify path: storage was already unlocked. Clear the screen
        // latch and emit UNLOCKED now. The cold-unlock path defers both
        // of these to completePendingUnlocks() once reloadFromDisk finishes.
        meshtastic_security::unlockScreen();
        queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCKED, "", EncryptedStorage::getBootsRemaining(),
                            EncryptedStorage::getValidUntilEpoch(), 0);
    } else if (ok && needsReload) {
        // Cold-unlock path: deliberately no status emission yet - the
        // client keeps seeing LOCKED until completePendingUnlocks()
        // runs after a successful reload.
    } else {
        uint32_t backoff = EncryptedStorage::getBackoffSecondsRemaining();
        queueLockdownStatus(meshtastic_LockdownStatus_State_UNLOCK_FAILED, "", 0, 0, backoff);
        // Don't log backoff seconds - the client receives it in the
        // UNLOCK_FAILED status anyway, and in non-DEBUG_MUTE builds the
        // numeric value would otherwise spill onto a USB-attached
        // attacker's serial terminal alongside other diagnostic noise.
        LOG_WARN("Lockdown: passphrase verification failed");
        (void)backoff;
    }

    zeroPassphrase();
    return true;
}
#endif // MESHTASTIC_ENCRYPTED_STORAGE
#endif // MESHTASTIC_PHONEAPI_ACCESS_CONTROL
