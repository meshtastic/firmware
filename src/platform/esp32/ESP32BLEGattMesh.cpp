#include "configuration.h"

#if HAS_BLE_GATT_MESH && defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_BLUETOOTH

#include "ESP32BLEGattMesh.h"
#include "main.h"
#include "nimble/NimbleBluetooth.h"

#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLEUUID.h>
#include <array>
#include <mutex>

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"

namespace
{
// Shared between the NimBLE host task (the callbacks) and the main task (the pump).
std::mutex lock;

struct Link {
    bool used;
    uint16_t conn;
    bool subscribed;     // wrote the CCCD: a notify target, and the mark of a mesh peer
    bool everSubscribed; // subscribed at any point, which outlives an unsubscribe
};
std::array<Link, BLE_GATT_MESH_ESP32_LINKS> links{};

struct RxChunk {
    uint16_t conn;
    uint16_t len; // 0 marks a disconnect
    uint8_t data[BLE_GATT_MESH_MAX_CHUNK];
};
std::array<RxChunk, BLE_GATT_MESH_RX_QUEUE_SIZE> rxQueue{};
size_t rxHead = 0;
size_t rxTail = 0;
size_t rxCount = 0;

BLECharacteristic *meshCharacteristic = nullptr;

uint16_t chunkFor(uint16_t conn)
{
    const uint16_t mtu = ble_att_mtu(conn);
    return mtu > 3 ? mtu - 3 : BLE_GATT_MESH_MIN_CHUNK;
}

// Callers hold `lock` for everything below this line, and never log while they do.
Link *findLink(uint16_t conn)
{
    for (auto &l : links) {
        if (l.used && l.conn == conn)
            return &l;
    }
    return nullptr;
}

Link *addLink(uint16_t conn)
{
    if (Link *l = findLink(conn))
        return l;
    for (auto &l : links) {
        if (l.used)
            continue;
        l.used = true;
        l.conn = conn;
        l.subscribed = false;
        l.everSubscribed = false;
        return &l;
    }
    return nullptr;
}

size_t linkCount()
{
    size_t n = 0;
    for (const auto &l : links)
        n += l.used ? 1 : 0;
    return n;
}

uint32_t rxAccepted = 0;
uint32_t rxDropped = 0;

// Returns false when a write was turned away, for the caller to log outside the lock.
bool pushRx(uint16_t conn, const uint8_t *data, uint16_t len)
{
    if (rxCount >= rxQueue.size()) {
        if (len) {
            rxDropped++;
            return false;
        }
        // A disconnect marker must land or the pump keeps that handle's half-built packets for the
        // next peer the stack gives it: overwrite the newest chunk, which belongs to a dead link anyway.
        rxTail = (rxTail + rxQueue.size() - 1) % rxQueue.size();
        rxCount--;
    }
    RxChunk &r = rxQueue[rxTail];
    r.conn = conn;
    r.len = len;
    if (len)
        memcpy(r.data, data, len);
    rxTail = (rxTail + 1) % rxQueue.size();
    rxCount++;
    if (len)
        rxAccepted++;
    return true;
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
        // NimBLE task: copy the value out and wake the pump. Nothing here touches the mesh.
        const size_t len = characteristic->getLength();
        if (len == 0 || len > BLE_GATT_MESH_MAX_CHUNK) {
            LOG_WARN("BLE GATT mesh: write from conn %u refused at the door, %u bytes", desc->conn_handle, (unsigned)len);
            return;
        }
        bool accepted;
        uint32_t accCount, dropCount;
        {
            std::lock_guard<std::mutex> guard(lock);
            addLink(desc->conn_handle);
            accepted = pushRx(desc->conn_handle, characteristic->getData(), (uint16_t)len);
            accCount = rxAccepted;
            dropCount = rxDropped;
        }
        if (!accepted)
            LOG_WARN("BLE GATT mesh: RX queue full, dropping a %u-byte write from conn %u (accepted %u, dropped %u)",
                     (unsigned)len, desc->conn_handle, (unsigned)accCount, (unsigned)dropCount);
        if (bleGattMeshHandler)
            bleGattMeshHandler->wake();
    }

    void onSubscribe(BLECharacteristic *, ble_gap_conn_desc *desc, uint16_t subValue) override
    {
        // The canonical "this link is a mesh peer" signal: a CCCD write enabling notifications. Only a
        // subscribed link is a notify target, so a plain phone-API client is never sent mesh frames.
        const bool subscribed = (subValue & 0x0001) != 0; // NIMBLE_SUB_NOTIFY
        {
            std::lock_guard<std::mutex> guard(lock);
            if (Link *l = addLink(desc->conn_handle)) {
                l->subscribed = subscribed;
                l->everSubscribed |= subscribed;
            }
        }
        LOG_INFO("BLE GATT mesh: conn %u %s (chunk %u)", desc->conn_handle, subscribed ? "subscribed" : "unsubscribed",
                 chunkFor(desc->conn_handle));
        if (bleGattMeshHandler)
            bleGattMeshHandler->wake();
    }
};
} // namespace

bool ESP32BLEGattMesh::enabled()
{
    return config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER;
}

void ESP32BLEGattMesh::setupService(BLEServer *server)
{
    if (!server || !enabled())
        return;
    BLEService *service = server->createService(BLE_GATT_MESH_SERVICE_UUID);
    if (!service) {
        LOG_ERROR("BLE GATT mesh: failed to create the mesh-peer service");
        return;
    }
    // Deliberately no encryption or pairing requirement: a mesh peer is a stranger by design, exactly
    // as on LoRa, and the channel PSK is the security. NimBLE's CCCD takes its permissions from the
    // characteristic, so the subscribe is open too.
    BLECharacteristic *characteristic = service->createCharacteristic(
        BLE_GATT_MESH_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
    // Static: setupService() re-runs on every BLE re-enable and the library never frees callbacks.
    static MeshPeerCallbacks callbacks;
    characteristic->setCallbacks(&callbacks);
    service->start();
    {
        std::lock_guard<std::mutex> guard(lock);
        meshCharacteristic = characteristic;
        resetState();
    }
    LOG_INFO("BLE GATT mesh: mesh-peer service registered");
}

void ESP32BLEGattMesh::fillScanResponse(BLEAdvertisementData &scan, const char *name)
{
    // 18 of the scan response's 31 bytes; the name that follows is shortened to what is left.
    constexpr size_t nameRoom = 31 - 18 - 2;
    scan.setCompleteServices(BLEUUID(BLE_GATT_MESH_SERVICE_UUID));
    const String full(name);
    if (full.length() <= nameRoom)
        scan.setName(full);
    else
        scan.setShortName(full.substring(0, nameRoom));
}

void ESP32BLEGattMesh::onConnect(uint16_t conn)
{
    std::lock_guard<std::mutex> guard(lock);
    addLink(conn);
}

bool ESP32BLEGattMesh::onDisconnect(uint16_t conn)
{
    bool subscribed = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (Link *l = findLink(conn)) {
            subscribed = l->everSubscribed;
            l->used = false;
            pushRx(conn, nullptr, 0); // the pump drops its half-built packets
        }
    }
    LOG_INFO("BLE GATT mesh: conn %u disconnected (%s)", conn, subscribed ? "was a mesh peer" : "never subscribed");
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
    return subscribed;
}

bool ESP32BLEGattMesh::hasFreeLink()
{
    std::lock_guard<std::mutex> guard(lock);
    return linkCount() < links.size();
}

size_t ESP32BLEGattMesh::linkHandles(uint16_t *out, size_t cap)
{
    std::lock_guard<std::mutex> guard(lock);
    size_t n = 0;
    for (const auto &l : links) {
        if (l.used && n < cap)
            out[n++] = l.conn;
    }
    return n;
}

void ESP32BLEGattMesh::teardown()
{
    std::lock_guard<std::mutex> guard(lock);
    meshCharacteristic = nullptr;
    resetState();
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
    LOG_INFO("BLE GATT mesh stopped");
}

bool ESP32BLEGattMesh::platformReady()
{
    std::lock_guard<std::mutex> guard(lock);
    return nimbleBluetooth && nimbleBluetooth->isActive() && meshCharacteristic != nullptr;
}

size_t ESP32BLEGattMesh::platformPeers(BLEGattMeshPeer *out, size_t cap)
{
    size_t n = 0;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (const auto &l : links) {
            if (!l.used || !l.subscribed)
                continue;
            if (n >= cap)
                break;
            out[n].id = l.conn;
            out[n].outbound = false; // every link here was dialled by the peer
            n++;
        }
    }
    // Outside the lock: the MTU read takes the host's own lock, which the host task holds when it
    // calls into ours.
    for (size_t i = 0; i < n; i++)
        out[i].chunk = chunkFor(out[i].id);
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
    // The handle resolves when the server starts, at the first advertisement after setupService().
    if (valueHandle == 0 || valueHandle == 0xFFFF)
        return false;
    // A gone link is refused here rather than retried by the pump until it gives up.
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(peer, &desc) != 0)
        return false;
    // To this one connection, which is what lets the pump skip the peer a relay arrived from; the
    // wrapper's notify() goes to every subscriber. The host consumes the mbuf whether or not it sends.
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (!om)
        return false;
    return ble_gatts_notify_custom(peer, valueHandle, om) == 0;
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

#endif // HAS_BLE_GATT_MESH && ARCH_ESP32 && !MESHTASTIC_EXCLUDE_BLUETOOTH
