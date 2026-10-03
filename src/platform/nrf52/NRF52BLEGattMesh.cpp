#include "configuration.h"

#if HAS_BLE_GATT_MESH && defined(ARCH_NRF52)

#include "NRF52BLEGattMesh.h"
#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include "mesh/BLEGattMeshLinks.h"
#include <bluefruit.h>

namespace
{
// Shared between Bluefruit's callback task and the main task (the pump). Callers hold `lock` around
// every use of `state`, and never log while they do.
concurrency::Lock lock;
BLEGattMeshLinks<4> state;
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

// Counted since boot so a dropped write carries its own denominator: the log reaches a host as a
// sparse LogRecord stream, and one surviving line has to be enough to compute a rate from.
uint32_t rxArrived = 0;

void onWrite(uint16_t conn, BLECharacteristic *, uint8_t *data, uint16_t len)
{
    // Bluefruit's callback task: copy the value out and wake the pump. Nothing here touches the mesh.
    if (len == 0 || len > BLE_GATT_MESH_MAX_CHUNK) {
        LOG_WARN("BLE GATT mesh: write from conn %u refused at the door, %u bytes", conn, len);
        return;
    }
    bool accepted;
    uint32_t accCount, dropCount;
    {
        concurrency::LockGuard guard(&lock);
        state.add(conn);
        accepted = state.pushRx(conn, data, len);
        accCount = state.rxAccepted;
        dropCount = state.rxDropped;
    }
    // After the lock, never inside it. Every arrival is logged because the question this answers is
    // whether writes reach the door at all.
    rxArrived++;
    if (!accepted)
        LOG_WARN("BLE GATT mesh: RX queue full, dropping a %u-byte write from conn %u (accepted %u, dropped %u)", len, conn,
                 (unsigned)accCount, (unsigned)dropCount);
    LOG_DEBUG("BLE GATT mesh: write %u bytes from conn %u (arrived %u, accepted %u, dropped %u)", len, conn, (unsigned)rxArrived,
              (unsigned)accCount, (unsigned)dropCount);
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
        state.setSubscribed(conn, subscribed);
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
        state.reset();
        serviceReady = true;
    }
    LOG_INFO("BLE GATT mesh: mesh-peer service registered");
}

bool NRF52BLEGattMesh::addToScanResponse()
{
    if (!protocolEnabled())
        return false;
    // 18 of the scan response's 31 bytes; the name that follows is shortened to what is left.
    Bluefruit.ScanResponse.addService(meshPeerService);
    LOG_INFO("BLE GATT mesh: advertising the mesh-peer service in the scan response");
    return true;
}

void NRF52BLEGattMesh::rearmAdvertising()
{
    // Bluefruit stops the connectable advertisement on connect and restarts it only when no peripheral
    // link is left, so with the phone and a peer sharing the radio the free slot would otherwise go
    // unadvertised. Bluefruit's own connect/disconnect handling ran synchronously before the deferred
    // callbacks this is reached from.
    if (protocolEnabled() && Bluefruit.Periph.connected() < 2 && !Bluefruit.Advertising.isRunning())
        Bluefruit.Advertising.start(0);
}

void NRF52BLEGattMesh::onConnect(uint16_t conn)
{
    {
        concurrency::LockGuard guard(&lock);
        state.add(conn);
    }
    rearmAdvertising();
}

bool NRF52BLEGattMesh::onDisconnect(uint16_t conn)
{
    bool subscribed;
    {
        concurrency::LockGuard guard(&lock);
        subscribed = state.remove(conn);
    }
    LOG_INFO("BLE GATT mesh: conn %u disconnected (%s)", conn, subscribed ? "was a mesh peer" : "never subscribed");
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
    // Bluefruit's restartOnDisconnect only fires when no peripheral link is left; with one still up,
    // whoever just dropped could never come back without this.
    rearmAdvertising();
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
    // setupService() runs inside NRF52Bluetooth::setup() after Bluefruit.begin(), so a registered
    // service already means the SoftDevice is up.
    concurrency::LockGuard guard(&lock);
    return serviceReady;
}

size_t NRF52BLEGattMesh::platformPeers(BLEGattMeshPeer *out, size_t cap)
{
    concurrency::LockGuard guard(&lock);
    const size_t n = state.subscribed(out, cap);
    for (size_t i = 0; i < n; i++)
        out[i].chunk = chunkFor(out[i].id);
    return n;
}

bool NRF52BLEGattMesh::platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len)
{
    // Bluefruit's notify blocks up to 100 ms waiting for a buffer, so a peer whose link is gone must be
    // refused here, not discovered by timing out fifty times on the main task.
    if (!Bluefruit.connected(peer))
        return false;
    return meshPeerCharacteristic.notify(peer, data, (uint16_t)len);
}

bool NRF52BLEGattMesh::platformPollInbound(BLEGattPeerId &peer, uint8_t *buf, size_t cap, size_t &len)
{
    concurrency::LockGuard guard(&lock);
    return state.popRx(peer, buf, cap, len);
}

#endif // HAS_BLE_GATT_MESH && ARCH_NRF52
