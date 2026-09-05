#include "configuration.h"

#if HAS_BLE_GATT_MESH && defined(ARCH_NRF52)

#include "NRF52BLEGattMesh.h"
#include "NRF52Bluetooth.h"
#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include <array>
#include <bluefruit.h>

namespace
{
// Shared between Bluefruit's callback task and the main task (the pump).
concurrency::Lock lock;

struct Link {
    bool used;
    uint16_t conn;
    bool subscribed; // wrote the CCCD: a notify target, and the mark of a mesh peer
};
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
bool serviceReady = false;

// BLE_GATT_MESH_SERVICE_UUID / _CHARACTERISTIC_UUID as the SoftDevice wants them: little-endian.
const uint8_t serviceUuid[16] = {0x01, 0x00, 0x00, 0x00, 0x54, 0x54, 0x41, 0x47, 0x65, 0x64, 0x6f, 0x4e, 0x68, 0x73, 0x65, 0x4d};
const uint8_t characteristicUuid[16] = {0x02, 0x00, 0x00, 0x00, 0x54, 0x54, 0x41, 0x47,
                                        0x65, 0x64, 0x6f, 0x4e, 0x68, 0x73, 0x65, 0x4d};
BLEService meshPeerService = BLEService(BLEUuid(serviceUuid));
BLECharacteristic meshPeerCharacteristic = BLECharacteristic(BLEUuid(characteristicUuid));

// The negotiated MTU is read live: Bluefruit exposes no MTU-changed callback.
uint16_t chunkFor(uint16_t conn)
{
    BLEConnection *c = Bluefruit.Connection(conn);
    const uint16_t mtu = c ? c->getMtu() : 0;
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
        return &l;
    }
    return nullptr;
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

void onWrite(uint16_t conn, BLECharacteristic *, uint8_t *data, uint16_t len)
{
    // Bluefruit's callback task: copy the value out and wake the pump. Nothing here touches the mesh.
    if (len == 0 || len > BLE_GATT_MESH_MAX_CHUNK)
        return;
    {
        concurrency::LockGuard guard(&lock);
        addLink(conn);
        pushRx(conn, data, len);
    }
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
}

void onCccd(uint16_t conn, BLECharacteristic *, uint16_t value)
{
    // The canonical "this link is a mesh peer" signal: a CCCD write enabling notifications. Only a
    // subscribed link is a notify target, so a plain phone-API client is never sent mesh frames.
    const bool subscribed = (value & 0x0001) != 0;
    {
        concurrency::LockGuard guard(&lock);
        if (Link *l = addLink(conn))
            l->subscribed = subscribed;
    }
    LOG_INFO("BLE GATT mesh: conn %u %s (chunk %u)", conn, subscribed ? "subscribed" : "unsubscribed", chunkFor(conn));
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
}
} // namespace

void NRF52BLEGattMesh::setupService()
{
    // Deliberately no encryption or pairing requirement: a mesh peer is a stranger by design, exactly
    // as on LoRa, and the channel PSK is the security. The client library does not pair.
    meshPeerService.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    meshPeerService.begin();
    meshPeerCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY);
    meshPeerCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    meshPeerCharacteristic.setFixedLen(0);
    meshPeerCharacteristic.setMaxLen(BLE_GATT_MESH_MAX_CHUNK);
    meshPeerCharacteristic.setWriteCallback(onWrite, true);
    meshPeerCharacteristic.setCccdWriteCallback(onCccd, true);
    meshPeerCharacteristic.begin();
    {
        concurrency::LockGuard guard(&lock);
        for (auto &l : links)
            l.used = false;
        rxHead = rxTail = rxCount = 0;
        serviceReady = true;
    }
    LOG_INFO("BLE GATT mesh: mesh-peer service registered");
}

bool NRF52BLEGattMesh::addToScanResponse()
{
    if (!(config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER))
        return false;
    // 18 of the scan response's 31 bytes; the name that follows is shortened to what is left.
    Bluefruit.ScanResponse.addService(meshPeerService);
    LOG_INFO("BLE GATT mesh: advertising the mesh-peer service in the scan response");
    return true;
}

void NRF52BLEGattMesh::onConnect(uint16_t conn)
{
    concurrency::LockGuard guard(&lock);
    addLink(conn);
}

bool NRF52BLEGattMesh::onDisconnect(uint16_t conn)
{
    bool subscribed = false;
    {
        concurrency::LockGuard guard(&lock);
        if (Link *l = findLink(conn)) {
            subscribed = l->subscribed;
            l->used = false;
            pushRx(conn, nullptr, 0); // the pump drops its half-built packets
        }
    }
    LOG_INFO("BLE GATT mesh: conn %u disconnected (%s)", conn, subscribed ? "was a mesh peer" : "never subscribed");
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
    return subscribed;
}

void NRF52BLEGattMesh::start()
{
    if (isRunning)
        return;
    isRunning = true;
    LOG_INFO("BLE GATT mesh started (waiting for Bluetooth ready)");
}

void NRF52BLEGattMesh::stop()
{
    if (!isRunning)
        return;
    isRunning = false;
    LOG_INFO("BLE GATT mesh stopped");
}

bool NRF52BLEGattMesh::platformReady()
{
    concurrency::LockGuard guard(&lock);
    return serviceReady && nrf52BluetoothReady;
}

size_t NRF52BLEGattMesh::platformPeers(BLEGattMeshPeer *out, size_t cap)
{
    concurrency::LockGuard guard(&lock);
    size_t n = 0;
    for (const auto &l : links) {
        if (!l.used || !l.subscribed)
            continue;
        if (n >= cap)
            break;
        out[n].id = l.conn;
        out[n].chunk = chunkFor(l.conn);
        n++;
    }
    return n;
}

bool NRF52BLEGattMesh::platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len)
{
    // Bluefruit refuses when the peer has not enabled notifications or the SoftDevice queue is full;
    // the pump retries the latter and the former clears itself on the CCCD write.
    return meshPeerCharacteristic.notify(peer, data, (uint16_t)len);
}

bool NRF52BLEGattMesh::platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len)
{
    concurrency::LockGuard guard(&lock);
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

#endif // HAS_BLE_GATT_MESH && ARCH_NRF52
