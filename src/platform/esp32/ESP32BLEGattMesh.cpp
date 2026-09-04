#include "configuration.h"

#if HAS_BLE_GATT_MESH && defined(ARCH_ESP32) && BLE_MESH_USE_EXT_ADV

#include "ESP32BLEGattMesh.h"
#include "main.h"
#include "nimble/NimbleBluetooth.h"

#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <array>
#include <atomic>
#include <mutex>

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"

namespace
{
// Shared between the NimBLE host task (the callbacks) and the main task (the pump), hence the lock.
// std::mutex is fine here: this file is ESP32-only, like NimbleBluetooth.cpp.
std::mutex lock;

struct Link {
    bool used;
    uint16_t conn;
    uint16_t chunk;
    bool subscribed; // wrote the CCCD: a notify target
    // Arrived through the mesh-peer advertisement (instance 2). Set in onGapEvent CONNECT and nowhere
    // else: it is the slot accounting for that advertisement and the "not the phone's session" signal
    // NimbleBluetooth's onDisconnect keys off, so it must mean exactly "came in on instance 2". It used
    // to be set by onSubscribe too, and that conflation is what put both advertisements to sleep: a
    // central that raced onto the phone-API set (both sets share the public address, so a CONNECT_IND
    // lands on whichever ADV_IND it catches) and subscribed was counted against this slot, and when it
    // dropped the early return in NimbleBluetooth skipped the phone-API re-arm as well.
    bool viaMeshAdv;
};
// Every link the server can hold: a phone-API link that subscribes to the mesh characteristic is a peer too.
std::array<Link, 4> links{};

struct RxChunk {
    uint16_t conn;
    uint16_t len; // 0 marks a disconnect
    uint8_t data[BLE_GATT_MESH_MAX_CHUNK];
};
std::array<RxChunk, BLE_GATT_MESH_RX_QUEUE_SIZE> rxQueue{};
size_t rxHead = 0;
size_t rxTail = 0;
size_t rxCount = 0;

BLEServer *meshServer = nullptr;
BLECharacteristic *meshCharacteristic = nullptr;

// Set from GAP callbacks, serviced by runOnce() on the main task: re-entering ble_gap_* from a callback
// while the host is mid-reset crashes (see pendingStartAdvertising in NimbleBluetooth.cpp).
std::atomic<bool> pendingAdvertising{false};

// 4d657368-4e6f-6465-4741-545400000001 (BLE_GATT_MESH_SERVICE_UUID) as NimBLE wants it: little-endian.
const ble_uuid128_t meshServiceUuid =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x54, 0x54, 0x41, 0x47, 0x65, 0x64, 0x6f, 0x4e, 0x68, 0x73, 0x65, 0x4d);
uint16_t chunkFor(uint16_t conn)
{
    const uint16_t mtu = ble_att_mtu(conn);
    return mtu > 3 ? mtu - 3 : BLE_GATT_MESH_MIN_CHUNK;
}

// Callers hold `lock` for everything below this line.
Link *findLink(uint16_t conn)
{
    for (auto &l : links) {
        if (l.used && l.conn == conn)
            return &l;
    }
    return nullptr;
}

Link *addLink(uint16_t conn, bool viaMeshAdv)
{
    if (Link *l = findLink(conn)) {
        l->viaMeshAdv = l->viaMeshAdv || viaMeshAdv;
        return l;
    }
    for (auto &l : links) {
        if (l.used)
            continue;
        l.used = true;
        l.conn = conn;
        l.chunk = chunkFor(conn);
        l.subscribed = false;
        l.viaMeshAdv = viaMeshAdv;
        return &l;
    }
    return nullptr;
}

// Links that came in through the mesh-peer advertisement; `holder` gets the conn of the first one, so a
// "slots full" log can name who is occupying it instead of leaving that to guesswork on the bench.
size_t meshLinkCount(uint16_t *holder = nullptr)
{
    size_t n = 0;
    for (const auto &l : links) {
        if (!l.used || !l.viaMeshAdv)
            continue;
        if (n == 0 && holder)
            *holder = l.conn;
        n++;
    }
    return n;
}

// NimBLE's rc for the two ways a connectable set fails to start that look identical without this:
// ENOMEM (6) is the host with no free connection slot - CONFIG_BT_NIMBLE_MAX_CONNECTIONS all in use,
// so the set cannot be started until a link drops; 519 is HCI 0x07 Memory Capacity Exceeded, the
// controller's activity budget (CONFIG_BT_CTRL_BLE_MAX_ACT) counting advertising sets, scans and
// connections together. Anything else is unexpected.
const char *advRcHint(int rc)
{
    switch (rc) {
    case BLE_HS_ENOMEM:
        return " (no free connection slot; retried on the next disconnect)";
    case BLE_HS_ERR_HCI_BASE + 0x07:
        return " (controller activity budget exhausted: CONFIG_BT_CTRL_BLE_MAX_ACT)";
    case BLE_HS_EALREADY:
        return " (already advertising)";
    default:
        return "";
    }
}

void pushRx(uint16_t conn, const uint8_t *data, uint16_t len)
{
    if (rxCount >= rxQueue.size()) {
        LOG_WARN("BLE GATT mesh: RX queue full, dropping a %u-byte write from conn %u", len, conn);
        return;
    }
    RxChunk &r = rxQueue[rxTail];
    r.conn = conn;
    r.len = len;
    if (len)
        memcpy(r.data, data, len);
    rxTail = (rxTail + 1) % rxQueue.size();
    rxCount++;
}

void resetState()
{
    for (auto &l : links)
        l.used = false;
    rxHead = rxTail = rxCount = 0;
}

class MeshPeerCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *characteristic, ble_gap_conn_desc *desc) override
    {
        // NimBLE task: copy the value out and wake the pump. Nothing here may touch the mesh.
        const size_t len = characteristic->getLength();
        if (len == 0 || len > BLE_GATT_MESH_MAX_CHUNK)
            return;
        {
            std::lock_guard<std::mutex> guard(lock);
            addLink(desc->conn_handle, false);
            pushRx(desc->conn_handle, characteristic->getData(), (uint16_t)len);
        }
        if (bleGattMeshHandler)
            bleGattMeshHandler->wake();
    }

    void onSubscribe(BLECharacteristic *, ble_gap_conn_desc *desc, uint16_t subValue) override
    {
        // The canonical "this link is a mesh peer" signal: a CCCD write enabling notifications. Only a
        // subscribed link is a notify target, so a plain phone-API client is never sent mesh frames.
        // It says nothing about which advertisement the link arrived on, so viaMeshAdv stays as the
        // CONNECT event left it (false here: a link unknown until now came in on the phone-API set).
        const bool subscribed = (subValue & 0x0001) != 0; // notifications, not indications
        bool viaMeshAdv = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (Link *l = addLink(desc->conn_handle, false)) {
                l->subscribed = subscribed;
                l->chunk = chunkFor(desc->conn_handle);
                viaMeshAdv = l->viaMeshAdv;
            }
        }
        LOG_INFO("BLE GATT mesh: conn %u %s (via %s advertisement)", desc->conn_handle,
                 subscribed ? "subscribed" : "unsubscribed", viaMeshAdv ? "mesh-peer" : "phone-API");
        if (bleGattMeshHandler)
            bleGattMeshHandler->wake(); // flush anything queued while this peer was still connecting
    }
};
} // namespace

void ESP32BLEGattMesh::setupService(BLEServer *server)
{
    if (!server)
        return;

    BLEService *service = server->createService(BLE_GATT_MESH_SERVICE_UUID);
    if (!service) {
        LOG_ERROR("BLE GATT mesh: failed to create the mesh-peer service");
        return;
    }
    // Deliberately no encryption or pairing requirement: a mesh peer is a stranger by design, exactly
    // as on LoRa, and the channel PSK is the security. The client library does not pair.
    BLECharacteristic *characteristic = service->createCharacteristic(
        BLE_GATT_MESH_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
    // Static like the phone API's callback objects: setupService() re-runs on every BLE re-enable and
    // the library never frees these.
    static MeshPeerCallbacks callbacks;
    characteristic->setCallbacks(&callbacks);
    service->start();

    // Force the server's GATT start now. The wrapper only resolves characteristic value handles (and
    // builds its notify/subscribe tables) inside BLEServer::start(), which it normally triggers from
    // BLEAdvertising::start(). This firmware advertises through the raw ble_gap_ext_adv API and never
    // calls the wrapper's advertising, so without this our characteristic's handle stays 0xFFFF and
    // notifications go nowhere. start() is guarded by m_gattsStarted, so this is a no-op if something
    // already ran it.
    server->start();

    {
        std::lock_guard<std::mutex> guard(lock);
        meshServer = server;
        meshCharacteristic = characteristic;
        resetState();
    }
    LOG_INFO("BLE GATT mesh: mesh-peer service registered");
}

void ESP32BLEGattMesh::startAdvertising()
{
    BLEServer *server;
    size_t linksNow;
    uint16_t holder = BLE_HS_CONN_HANDLE_NONE;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!meshCharacteristic)
            return;
        server = meshServer;
        linksNow = meshLinkCount(&holder);
    }
    if (linksNow >= BLE_GATT_MESH_MAX_LINKS) {
        // INFO, not DEBUG, and with the holder: this is the line that says "a second phone cannot find
        // this node over GATT right now, and here is who has the slot". At DEBUG it was invisible on a
        // bench where exactly that was the question.
        LOG_INFO("BLE GATT mesh: peer slot held by conn %u (%u/%u), not advertising", holder, (unsigned)linksNow,
                 (unsigned)BLE_GATT_MESH_MAX_LINKS);
        return;
    }
    // Re-armed on every disconnect (see onDisconnect), so this is frequently reached with the set
    // already running; leave it alone rather than stop-and-restart it for a gap in the advertisement.
    if (ble_gap_ext_adv_active(BLE_GATT_MESH_ADV_INSTANCE)) {
        LOG_DEBUG("BLE GATT mesh: mesh-peer advertisement already running");
        return;
    }

    ble_gap_ext_adv_stop(BLE_GATT_MESH_ADV_INSTANCE);

    // Legacy ADV_IND like the phone API's set: a phone without BLE 5 still finds it.
    struct ble_gap_ext_adv_params params = {};
    params.connectable = 1;
    params.scannable = 1;
    params.legacy_pdu = 1;
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;
    params.itvl_min = BLE_GATT_MESH_ADV_ITVL_MIN;
    params.itvl_max = BLE_GATT_MESH_ADV_ITVL_MAX;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127;
    params.sid = 1;

    int8_t selectedTxPower = 0;
    int rc = ble_gap_ext_adv_configure(BLE_GATT_MESH_ADV_INSTANCE, &params, &selectedTxPower, onGapEvent, (void *)server);
    if (rc != 0) {
        LOG_ERROR("BLE GATT mesh: ext adv configure failed: rc=%d%s", rc, advRcHint(rc));
        return;
    }

    // The service UUID is the discovery key; it is also what lets an iPhone find us from the background.
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &meshServiceUuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    struct os_mbuf *advData = os_msys_get_pkthdr(BLE_HS_ADV_MAX_SZ, 0);
    if (!advData) {
        LOG_ERROR("BLE GATT mesh: no mbuf for the advertisement");
        return;
    }
    rc = ble_hs_adv_set_fields_mbuf(&fields, advData);
    if (rc != 0) {
        os_mbuf_free_chain(advData);
        LOG_ERROR("BLE GATT mesh: ext adv set fields failed: rc=%d", rc);
        return;
    }
    // Takes ownership on success, frees on failure - do not touch advData after this either way.
    rc = ble_gap_ext_adv_set_data(BLE_GATT_MESH_ADV_INSTANCE, advData);
    if (rc != 0) {
        LOG_ERROR("BLE GATT mesh: ext adv set data failed: rc=%d", rc);
        return;
    }

    // The name rides in the scan response, as the phone API's does; it does not fit beside a 128-bit UUID.
    const char *name = getDeviceName();
    struct ble_hs_adv_fields rspFields = {};
    rspFields.name = (const uint8_t *)name;
    rspFields.name_len = (uint8_t)strlen(name);
    rspFields.name_is_complete = 1;
    struct os_mbuf *rspData = os_msys_get_pkthdr(BLE_HS_ADV_MAX_SZ, 0);
    if (rspData) {
        if (ble_hs_adv_set_fields_mbuf(&rspFields, rspData) == 0) {
            if (ble_gap_ext_adv_rsp_set_data(BLE_GATT_MESH_ADV_INSTANCE, rspData) != 0)
                LOG_WARN("BLE GATT mesh: scan response rejected; advertising without a name");
        } else {
            os_mbuf_free_chain(rspData);
        }
    }

    rc = ble_gap_ext_adv_start(BLE_GATT_MESH_ADV_INSTANCE, 0, 0);
    if (rc == BLE_HS_ENOMEM) {
        // Every connection slot is taken (the phone plus a mesh peer, or two mesh peers): expected on a
        // full node, not an error. onDisconnect re-arms us, so the set comes back with the first free slot.
        LOG_WARN("BLE GATT mesh: ext adv start deferred: rc=%d%s", rc, advRcHint(rc));
    } else if (rc != 0) {
        LOG_ERROR("BLE GATT mesh: ext adv start failed: rc=%d%s", rc, advRcHint(rc));
    } else {
        LOG_INFO("BLE GATT mesh: advertising the mesh-peer service on instance %d", BLE_GATT_MESH_ADV_INSTANCE);
    }
}

void ESP32BLEGattMesh::onConnect(uint16_t connHandle)
{
    // Fires for every inbound link from the server callback, whichever advertisement it arrived on.
    // Just track the link (chunk size, MTU); it becomes a notify target only once onSubscribe marks it
    // subscribed, so a plain phone-API client that never enables the mesh CCCD is never notified.
    std::lock_guard<std::mutex> guard(lock);
    if (Link *l = addLink(connHandle, false))
        l->chunk = chunkFor(connHandle);
}

bool ESP32BLEGattMesh::onDisconnect(uint16_t connHandle)
{
    bool viaMeshAdv = false;
    bool subscribed = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (Link *l = findLink(connHandle)) {
            viaMeshAdv = l->viaMeshAdv;
            subscribed = l->subscribed;
            l->used = false;
            pushRx(connHandle, nullptr, 0); // the pump drops its half-built packets
        }
    }
    LOG_INFO("BLE GATT mesh: conn %u disconnected (via %s advertisement, %s)", connHandle, viaMeshAdv ? "mesh-peer" : "phone-API",
             subscribed ? "was subscribed" : "never subscribed");
    // Re-arm on every drop, not only a mesh-peer one, and let startAdvertising() decide: a phone-API
    // link dropping is what frees the connection slot a deferred start (rc=ENOMEM) was waiting for,
    // and a set that is already running is left alone there. Only re-arming for mesh-peer links left
    // instance 2 dark whenever the slot had been taken by a link that arrived on the other set.
    pendingAdvertising = true;
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
    return viaMeshAdv;
}

void ESP32BLEGattMesh::teardown()
{
    std::lock_guard<std::mutex> guard(lock);
    meshCharacteristic = nullptr;
    meshServer = nullptr;
    pendingAdvertising = false;
    resetState();
}

int ESP32BLEGattMesh::onGapEvent(struct ble_gap_event *event, void *arg)
{
    // The wrapper's handler first, so its connection, MTU and subscription bookkeeping sees this link
    // exactly as it sees the phone API's. DISCONNECT reaches the server callback, which routes every
    // link through onDisconnect().
    const int rc = nimbleServerGapEvent(event, arg);

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            {
                std::lock_guard<std::mutex> guard(lock);
                addLink(event->connect.conn_handle, true);
            }
            LOG_INFO("BLE GATT mesh: peer conn %u connected", event->connect.conn_handle);
        } else {
            pendingAdvertising = true; // the set stopped for a connection that never formed
        }
        if (bleGattMeshHandler)
            bleGattMeshHandler->wake();
        break;
    case BLE_GAP_EVENT_MTU: {
        std::lock_guard<std::mutex> guard(lock);
        if (Link *l = findLink(event->mtu.conn_handle))
            l->chunk = event->mtu.value > 3 ? event->mtu.value - 3 : BLE_GATT_MESH_MIN_CHUNK;
        break;
    }
    default:
        break;
    }
    return rc;
}

void ESP32BLEGattMesh::start()
{
    if (isRunning)
        return;
    isRunning = true;
    LOG_INFO("BLE GATT mesh started (waiting for Bluetooth ready)");
}

void ESP32BLEGattMesh::stop()
{
    if (!isRunning)
        return;
    isRunning = false;
    if (platformReady())
        ble_gap_ext_adv_stop(BLE_GATT_MESH_ADV_INSTANCE);
    LOG_INFO("BLE GATT mesh stopped");
}

bool ESP32BLEGattMesh::platformReady()
{
    std::lock_guard<std::mutex> guard(lock);
    return nimbleBluetooth && nimbleBluetooth->isActive() && meshCharacteristic != nullptr;
}

size_t ESP32BLEGattMesh::platformPeers(BLEGattMeshPeer *out, size_t cap)
{
    std::lock_guard<std::mutex> guard(lock);
    size_t n = 0;
    for (const auto &l : links) {
        if (!l.used || !l.subscribed)
            continue;
        if (n >= cap)
            break;
        out[n].id = l.conn;
        out[n].chunk = l.chunk;
        n++;
    }
    return n;
}

bool ESP32BLEGattMesh::platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len)
{
    uint16_t valueHandle;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!meshCharacteristic)
            return false;
        valueHandle = meshCharacteristic->getHandle();
    }
    if (valueHandle == 0 || valueHandle == 0xFFFF)
        return false;

    // Notify this one connection. getHandle() is the value handle the wrapper resolved in setupService()
    // (via the forced server->start()); ble_gatts_notify_custom sends the fragment to exactly this peer,
    // which is what lets the pump skip the peer a relayed packet arrived from. Consumes the mbuf either
    // way; a refusal is the host briefly out of buffers, and the pump retries.
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (!om)
        return false;
    const int rc = ble_gatts_notify_custom(peer, valueHandle, om);
    if (rc != 0) {
        LOG_DEBUG("BLE GATT mesh: notify to conn %u refused: rc=%d", peer, rc);
        return false;
    }
    return true;
}

bool ESP32BLEGattMesh::platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len)
{
    std::lock_guard<std::mutex> guard(lock);
    if (rxCount == 0)
        return false;
    const RxChunk &r = rxQueue[rxHead];
    peer = r.conn;
    len = std::min<size_t>(r.len, cap);
    if (len)
        memcpy(buf, r.data, len);
    rxHead = (rxHead + 1) % rxQueue.size();
    rxCount--;
    return true;
}

int32_t ESP32BLEGattMesh::runOnce()
{
    if (pendingAdvertising && platformReady() && ble_hs_synced()) {
        pendingAdvertising = false;
        startAdvertising();
    }
    return BLEGattMeshHandler::runOnce();
}

#endif // HAS_BLE_GATT_MESH && ARCH_ESP32 && BLE_MESH_USE_EXT_ADV
