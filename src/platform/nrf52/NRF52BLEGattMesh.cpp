#include "configuration.h"

#if HAS_BLE_GATT_MESH && defined(ARCH_NRF52)

#include "NRF52BLEGattMesh.h"
#include "NRF52Bluetooth.h"
#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include "mesh/Throttle.h"
#include <array>
#include <bluefruit.h>

namespace
{
// Shared between Bluefruit's callback task and the main task (the pump).
concurrency::Lock lock;

struct Link {
    bool used;
    uint16_t conn;
    bool subscribed;     // wrote the CCCD: a notify target, and the mark of a mesh peer
    bool everSubscribed; // subscribed at any point, which outlives an unsubscribe
    bool outbound;       // this node dialled it: frames go out as GATT client writes, not notifies
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
        l.everSubscribed = false;
        l.outbound = false;
        return &l;
    }
    return nullptr;
}

// Counted since boot so a dropped write carries its own denominator: the log reaches a host as a
// sparse LogRecord stream, and one surviving line has to be enough to compute a rate from.
uint32_t rxArrived = 0;
uint32_t rxAccepted = 0;
uint32_t rxDropped = 0;

void pushRx(uint16_t conn, const uint8_t *data, uint16_t len)
{
    if (rxCount >= rxQueue.size()) {
        if (len) {
            rxDropped++;
            LOG_WARN("BLE GATT mesh: RX queue full, dropping a %u-byte write from conn %u (accepted %u, dropped %u)", len, conn,
                     (unsigned)rxAccepted, (unsigned)rxDropped);
            return;
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
}

void onWrite(uint16_t conn, BLECharacteristic *, uint8_t *data, uint16_t len)
{
    // Bluefruit's callback task: copy the value out and wake the pump. Nothing here touches the mesh.
    if (len == 0 || len > BLE_GATT_MESH_MAX_CHUNK) {
        LOG_WARN("BLE GATT mesh: write from conn %u refused at the door, %u bytes", conn, len);
        return;
    }
    {
        concurrency::LockGuard guard(&lock);
        addLink(conn);
        pushRx(conn, data, len);
    }
    // After the lock, never inside it: a LOG_ that takes one deadlocks against this same mutex.
    // Every arrival is logged because the question this answers is whether writes reach the door at
    // all - pushRx speaks only when it turns one away.
    rxArrived++;
    LOG_DEBUG("BLE GATT mesh: write %u bytes from conn %u (arrived %u, accepted %u, dropped %u)", len, conn, (unsigned)rxArrived,
              (unsigned)rxAccepted, (unsigned)rxDropped);
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
        if (Link *l = addLink(conn)) {
            l->subscribed = subscribed;
            l->everSubscribed |= subscribed;
        }
    }
    LOG_INFO("BLE GATT mesh: conn %u %s (chunk %u)", conn, subscribed ? "subscribed" : "unsubscribed", chunkFor(conn));
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
}

#if BLE_GATT_MESH_DIAL
// The outbound half: this node as the central, on the one central link Bluefruit.begin(2, 1) configures.
BLEClientService meshClientService = BLEClientService(BLEUuid(serviceUuid));
BLEClientCharacteristic meshClientCharacteristic = BLEClientCharacteristic(BLEUuid(characteristicUuid));
bool dialing = false;
ble_gap_addr_t dialAddr{};
// A peer that dropped, refused or never answered is not redialled for a minute: the scanner reports
// it again within 100 ms. One length for every outcome, on purpose. An iPhone that reached this node
// by itself keeps advertising the overflow bit under an address it rotates, and the dial of that
// address is refused (0x3e) every time - so a dual-role iPhone in range costs one refused dial a
// minute, about a second of scan each. A longer wait after 0x3e would spare that and instead make a
// backgrounded iPhone that hit a transient refusal (a reflash race, an app relaunch) unreachable for
// that long, which is the worse trade for a mesh. Telling the two apart needs identity, not address.
#ifndef BLE_GATT_MESH_DIAL_COOLDOWN_MS
#define BLE_GATT_MESH_DIAL_COOLDOWN_MS 60000
#endif
// How long a dial may wait for the peer to accept the connection. Bluefruit's own default is forever.
#ifndef BLE_GATT_MESH_DIAL_TIMEOUT_MS
#define BLE_GATT_MESH_DIAL_TIMEOUT_MS 4000
#endif
bool cooldownArmed = false;
ble_gap_addr_t cooldownAddr{};
uint32_t cooldownSinceMs = 0;

// The proof-of-life write request in flight on the dialled link, and how it ended. Bluefruit's own
// write_resp() gives the peer 100 ms, which a backgrounded iPhone on a long connection interval
// misses while still answering every time; this waits a few intervals and reads the response event
// through the mesh's event tap instead.
#ifndef BLE_GATT_MESH_PROBE_WAIT_MS
#define BLE_GATT_MESH_PROBE_WAIT_MS 1500
#endif
enum ProbeState : uint8_t { PROBE_IDLE, PROBE_PENDING, PROBE_ANSWERED, PROBE_REFUSED };
volatile uint16_t probeConn = BLE_CONN_HANDLE_INVALID;
volatile uint8_t probeState = PROBE_IDLE;

void armCooldown()
{
    cooldownAddr = dialAddr;
    cooldownSinceMs = millis();
    cooldownArmed = true;
    dialing = false;
}

void onNotify(BLEClientCharacteristic *chr, uint8_t *data, uint16_t len)
{
    const uint16_t conn = chr->connHandle();
    if (len == 0 || len > BLE_GATT_MESH_MAX_CHUNK)
        return;
    {
        concurrency::LockGuard guard(&lock);
        pushRx(conn, data, len);
    }
    rxArrived++;
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
}

void onCentralConnect(uint16_t conn)
{
    // `dialing` stays set until this settles: the scanner is back on and would dial again in the gap.
    // Discovery blocks; this runs on Bluefruit's callback task, never the SoftDevice's event path.
    // The SoftDevice runs one GATTC procedure per link at a time, so the MTU exchange comes last.
    const char *failed = nullptr;
    if (!Bluefruit.connected(conn))
        failed = "link gone before discovery";
    else if (!meshClientService.discover(conn))
        failed = "service not found";
    else if (!meshClientCharacteristic.discover())
        failed = "characteristic not found";
    else if (!meshClientCharacteristic.enableNotify())
        failed = "CCCD write refused";
    if (failed) {
        LOG_WARN("BLE GATT mesh: dialled conn %u dropped: %s", conn, failed);
        armCooldown();
        Bluefruit.disconnect(conn);
        return;
    }
    {
        concurrency::LockGuard guard(&lock);
        if (Link *l = addLink(conn)) {
            l->outbound = true;
            l->subscribed = true;
            l->everSubscribed = true;
        }
    }
    dialing = false;
    // 247 is what configCentralBandwidth(BANDWIDTH_MAX) allows; asking for more is refused and leaves 23.
    if (BLEConnection *c = Bluefruit.Connection(conn))
        c->requestMtuExchange(247);
    LOG_INFO("BLE GATT mesh: dialled conn %u is a mesh peer (chunk %u)", conn, chunkFor(conn));
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
}

void onCentralDisconnect(uint16_t conn, uint8_t reason)
{
    {
        concurrency::LockGuard guard(&lock);
        if (Link *l = findLink(conn)) {
            l->used = false;
            pushRx(conn, nullptr, 0);
        }
    }
    armCooldown();
    LOG_INFO("BLE GATT mesh: dialled conn %u disconnected (reason 0x%02x)", conn, reason);
    if (bleGattMeshHandler)
        bleGattMeshHandler->wake();
}
#endif // BLE_GATT_MESH_DIAL
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
#if BLE_GATT_MESH_DIAL
    static bool clientReady = false;
    if (!clientReady) {
        clientReady = true;
        meshClientService.begin();
        meshClientCharacteristic.begin();
        meshClientCharacteristic.setNotifyCallback(onNotify);
        Bluefruit.Central.setConnectCallback(onCentralConnect);
        Bluefruit.Central.setDisconnectCallback(onCentralDisconnect);
        // Bluefruit dials with its scanner's parameters, whose timeout is 0: a peer that never accepts
        // leaves the dial pending forever and this node never dials again.
        Bluefruit.Scanner.getParams()->timeout = BLE_GATT_MESH_DIAL_TIMEOUT_MS / 10;
    }
#endif
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

void NRF52BLEGattMesh::rearmAdvertising()
{
    // Bluefruit stops the connectable advertisement on connect and restarts it only when no peripheral
    // link is left, so with the phone and a peer sharing the radio the free slot would otherwise go
    // unadvertised. Bluefruit's own connect/disconnect handling ran synchronously before the deferred
    // callbacks this is reached from.
    if ((config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER) &&
        Bluefruit.Periph.connected() < 2 && !Bluefruit.Advertising.isRunning())
        Bluefruit.Advertising.start(0);
}

// An iOS app in the background advertises none of its service UUIDs. iOS moves them into Apple's
// "overflow area": manufacturer data for company 0x004c, type 0x01, then a 128-bit bitmap with one bit
// set per service, the bit chosen by an undocumented hash of the UUID. The bit for the mesh-peer
// service was measured on 2026-09-19 (iPadOS 26.6, BlueZ witness on james-pc): byte 1, mask 0x40 -
// the same frame foreground and background, with the UUID beside it only in the foreground. Any
// service that hashes to the same bit is a false positive, which the dial pays for with one connect:
// the service discovery that follows finds no mesh-peer service and drops the link.
//
// The overflow area rides in the SCAN RESPONSE, never the ADV_IND - this node's own passive scan
// received 1,500 Apple entries in a minute and not one of type 0x01, then saw it on the first scan
// response once the scan went active (NRF52BLEMesh.cpp). Proven the same day: a RAK4631 dialled a
// backgrounded, peripheral-only iPad off one such response, both HELLOs crossed, and a 127-byte
// text landed on the iPad in one write.
#define BLE_GATT_MESH_IOS_OVERFLOW_BYTE 1
#define BLE_GATT_MESH_IOS_OVERFLOW_MASK 0x40

static bool reportHasIosOverflowBit(const ble_gap_evt_adv_report_t *report)
{
    uint8_t mfr[31];
    const uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfr, sizeof(mfr));
    // company id (2, little endian), overflow type (1), bitmap (16)
    if (len != 19 || mfr[0] != 0x4c || mfr[1] != 0x00 || mfr[2] != 0x01)
        return false;
    return (mfr[3 + BLE_GATT_MESH_IOS_OVERFLOW_BYTE] & BLE_GATT_MESH_IOS_OVERFLOW_MASK) != 0;
}

void NRF52BLEGattMesh::onScanReport(const ble_gap_evt_adv_report_t *report)
{
#if BLE_GATT_MESH_DIAL
    if (!report || !(config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_GATT_PEER))
        return;
    // A scan-response report carries the advertiser's connectable bit too (every overflow response
    // logged on 2026-09-19 read conn=1 rsp=1), so this one gate covers both the ADV_IND and the
    // response, and a scannable-but-not-connectable advertiser is never dialled.
    if (!report->type.connectable || dialing || Bluefruit.Central.connected() > 0)
        return;
    // A controller holds one link per peer address, so a peer already connected the other way - a
    // phone that dialled this node first - cannot be dialled: the CONNECT_IND is ignored and the
    // attempt ends in 0x3e. The dial is for the peer that cannot dial, an iPhone in the background.
    for (uint16_t c = 0; c < BLE_MAX_CONNECTION; c++) {
        BLEConnection *bc = Bluefruit.Connection(c);
        if (!bc || !bc->connected())
            continue;
        const ble_gap_addr_t peer = bc->getPeerAddr();
        if (peer.addr_type == report->peer_addr.addr_type && memcmp(peer.addr, report->peer_addr.addr, sizeof(peer.addr)) == 0)
            return;
    }
    if (cooldownArmed && memcmp(&cooldownAddr, &report->peer_addr, sizeof(cooldownAddr)) == 0 &&
        Throttle::isWithinTimespanMs(cooldownSinceMs, BLE_GATT_MESH_DIAL_COOLDOWN_MS))
        return;
        // Only the primary advertisement is seen: the mesh scan is passive, so a node that puts the UUID in
        // its scan response (this firmware on nRF52) is never dialled. Phones put it in the advertisement.
        // The dial is for the peer that cannot dial: an iOS app in the background, which always carries the
        // overflow bit (in the foreground too). An advertiser showing the UUID alone is Android or another
        // radio, and both reach this node by themselves - dialling one of those spent the single central
        // slot on the phone that was about to connect anyway, then redialled its rotated address for ever.
#if BLE_GATT_MESH_DIAL_UUID
    if (!Bluefruit.Scanner.checkReportForUuid(report, BLEUuid(serviceUuid)) && !reportHasIosOverflowBit(report))
        return;
#else
    if (!reportHasIosOverflowBit(report))
        return;
#endif
    dialing = true;
    dialAddr = report->peer_addr;
    if (!Bluefruit.Central.connect(report)) {
        LOG_WARN("BLE GATT mesh: dial refused by the SoftDevice");
        armCooldown();
        return;
    }
    const uint8_t *a = report->peer_addr.addr;
    LOG_INFO("BLE GATT mesh: dialling %02x:%02x:%02x:%02x:%02x:%02x (rssi %d)", a[5], a[4], a[3], a[2], a[1], a[0], report->rssi);
#else
    (void)report;
#endif
}

void NRF52BLEGattMesh::onDialTimeout()
{
#if BLE_GATT_MESH_DIAL
    if (!dialing)
        return;
    LOG_INFO("BLE GATT mesh: dial timed out");
    armCooldown();
#endif
}

void NRF52BLEGattMesh::onConnect(uint16_t conn)
{
    {
        concurrency::LockGuard guard(&lock);
        addLink(conn);
    }
    rearmAdvertising();
}

bool NRF52BLEGattMesh::onDisconnect(uint16_t conn)
{
    bool subscribed = false;
    {
        concurrency::LockGuard guard(&lock);
        if (Link *l = findLink(conn)) {
            subscribed = l->everSubscribed;
            l->used = false;
            pushRx(conn, nullptr, 0); // the pump drops its half-built packets
        }
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
        out[n].outbound = l.outbound;
        n++;
    }
    return n;
}

bool NRF52BLEGattMesh::platformNotify(BLEGattPeerId peer, const uint8_t *data, size_t len)
{
#if BLE_GATT_MESH_DIAL
    bool outbound = false;
    {
        concurrency::LockGuard guard(&lock);
        Link *l = findLink(peer);
        outbound = l && l->outbound;
    }
    if (outbound) {
        if (!Bluefruit.Central.connected(peer))
            return false;
        // Write-without-response: a full command queue refuses now and the pump retries, where
        // write_resp would block the main task on every fragment.
        return meshClientCharacteristic.write(data, (uint16_t)len) == len;
    }
#endif
    // Bluefruit's notify blocks up to 100 ms waiting for a buffer, so a peer whose link is gone must be
    // refused here, not discovered by timing out fifty times on the main task.
    if (!Bluefruit.connected(peer))
        return false;
    return meshPeerCharacteristic.notify(peer, data, (uint16_t)len);
}

bool NRF52BLEGattMesh::platformProbe(BLEGattPeerId peer)
{
#if BLE_GATT_MESH_DIAL
    bool outbound = false;
    {
        concurrency::LockGuard guard(&lock);
        Link *l = findLink(peer);
        outbound = l && l->outbound;
    }
    if (!outbound)
        return true;
    if (!Bluefruit.Central.connected(peer))
        return false;
    // A write with response: the peer's ATT layer must answer, and a peer whose app died has no
    // handle left to answer for. The greeting is idempotent on the client, so it is the probe.
    uint8_t hello[BLE_GATT_MESH_HELLO_SIZE];
    const size_t len = buildHello(hello, sizeof(hello));
    ble_gattc_write_params_t params = {
        .write_op = BLE_GATT_OP_WRITE_REQ,
        .flags = 0,
        .handle = meshClientCharacteristic.valueHandle(),
        .offset = 0,
        .len = (uint16_t)len,
        .p_value = hello,
    };
    probeConn = peer;
    probeState = PROBE_PENDING;
    if (sd_ble_gattc_write(peer, &params) != NRF_SUCCESS) {
        probeState = PROBE_IDLE; // the stack is busy with another procedure; the retry will ask again
        return false;
    }
    // This runs on the main task; the response lands on the BLE task, and delay() yields to it.
    const uint32_t started = millis();
    while (probeState == PROBE_PENDING && Bluefruit.Central.connected(peer) &&
           Throttle::isWithinTimespanMs(started, BLE_GATT_MESH_PROBE_WAIT_MS))
        delay(10);
    const bool answered = probeState == PROBE_ANSWERED;
    probeState = PROBE_IDLE;
    probeConn = BLE_CONN_HANDLE_INVALID;
    return answered;
#else
    (void)peer;
    return true;
#endif
}

void NRF52BLEGattMesh::onWriteResponse(uint16_t conn, uint16_t status)
{
#if BLE_GATT_MESH_DIAL
    if (conn == probeConn && probeState == PROBE_PENDING)
        probeState = status == BLE_GATT_STATUS_SUCCESS ? PROBE_ANSWERED : PROBE_REFUSED;
#else
    (void)conn;
    (void)status;
#endif
}

void NRF52BLEGattMesh::onSecurityRequest(uint16_t conn)
{
#if BLE_GATT_MESH_DIAL
    // A phone bonded to this node's phone API asks the link be encrypted the moment it is dialled.
    // Bluefruit answers nothing on a central link, the phone's SMP timer runs out at 30 s and it drops
    // the link (0x05). The mesh-peer link is open by design - the channel key is the security - so
    // decline, which the SoftDevice does for a NULL parameter set.
    BLEConnection *c = Bluefruit.Connection(conn);
    if (!c || c->getRole() != BLE_GAP_ROLE_CENTRAL)
        return;
    const uint32_t err = sd_ble_gap_authenticate(conn, NULL);
    LOG_INFO("BLE GATT mesh: declined the security request on dialled conn %u (0x%x)", conn, (unsigned)err);
#else
    (void)conn;
#endif
}

void NRF52BLEGattMesh::platformShedOutbound(BLEGattPeerId peer)
{
#if BLE_GATT_MESH_DIAL
    bool outbound = false;
    {
        concurrency::LockGuard guard(&lock);
        Link *l = findLink(peer);
        outbound = l && l->outbound;
    }
    if (!outbound)
        return;
    LOG_INFO("BLE GATT mesh: shedding dialled conn %u", peer);
    Bluefruit.disconnect(peer); // onCentralDisconnect arms the cooldown
#else
    (void)peer;
#endif
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
