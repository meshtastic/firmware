#include "configuration.h"

#if HAS_BLE_MESH

#include "BLEMeshHandler.h"
#include "FSCommon.h"
#include "HardwareRNG.h"
#include "SafeFile.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "concurrency/LockGuard.h"
#include "main.h"
#include "meshUtils.h"

#include <ErriezCRC32.h>
#include <RNG.h>
#include <SHA256.h>
#include <cstddef>

BLEMeshHandler *bleMeshHandler = nullptr;

// AD type constants, spelled locally so this file does not have to pick between the NimBLE and
// SoftDevice headers - the values are from the Bluetooth Core Supplement, not from either stack.
#define BLE_MESH_AD_TYPE_FLAGS 0x01
#define BLE_MESH_AD_TYPE_MFG_DATA 0xFF
#define BLE_MESH_AD_FLAGS_LE_GENERAL_DISC_BREDR_UNSUP 0x06

namespace
{
constexpr uint32_t PAIR_STORE_MAGIC = 0x42504d32;
constexpr uint8_t PAIR_STORE_VERSION = 2;
constexpr const char *PAIR_STORE_FILE = "/prefs/ble-pairs.dat";
constexpr uint8_t BRIDGE_KEY_CONTEXT[] = "Meshtastic BLE bridge v2";
constexpr NodeNum BRIDGE_NONCE_DOMAIN = 0x424c4500;
constexpr size_t PAIRING_HELLO_LEN = 1 + sizeof(NodeNum) + sizeof(uint64_t) + 32;

#pragma pack(push, 1)
struct PersistedPairStore {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint8_t reserved[2];
    NodeNum localNode;
    NodeNum nodes[BLE_MESH_MAX_PAIRED_NODES];
    uint32_t crc;
};
#pragma pack(pop)

void writeU32(uint8_t *out, uint32_t value)
{
    memcpy(out, &value, sizeof(value));
}

uint32_t readU32(const uint8_t *in)
{
    uint32_t value;
    memcpy(&value, in, sizeof(value));
    return value;
}

void writeU64(uint8_t *out, uint64_t value)
{
    memcpy(out, &value, sizeof(value));
}

uint64_t readU64(const uint8_t *in)
{
    uint64_t value;
    memcpy(&value, in, sizeof(value));
    return value;
}

void writeAdvertisementPrefix(uint8_t *out, size_t totalLen)
{
    // Flags AD structure.
    out[0] = 2;
    out[1] = BLE_MESH_AD_TYPE_FLAGS;
    out[2] = BLE_MESH_AD_FLAGS_LE_GENERAL_DISC_BREDR_UNSUP;
    // Manufacturer-specific data AD structure: length covers everything after the length byte.
    out[3] = static_cast<uint8_t>(totalLen - 4);
    out[4] = BLE_MESH_AD_TYPE_MFG_DATA;
    out[5] = static_cast<uint8_t>(BLE_MESH_COMPANY_ID & 0xff);
    out[6] = static_cast<uint8_t>(BLE_MESH_COMPANY_ID >> 8);
    out[7] = BLE_MESH_PROTOCOL_VERSION;
}

} // namespace

bool BLEMeshHandler::validateIdentity(NodeNum nodeNum, const uint8_t publicKey[32])
{
    return nodeNum != 0 && publicKey && crc32Buffer(publicKey, 32) == nodeNum;
}

void BLEMeshHandler::ensurePairsLoaded()
{
    if (!pairsLoaded)
        loadPairs();
}

void BLEMeshHandler::loadPairs()
{
    pairsLoaded = true;
    pairCount = 0;

#ifdef FSCom
    concurrency::LockGuard fsGuard(spiLock);
    auto file = FSCom.open(PAIR_STORE_FILE, FILE_O_READ);
    if (!file)
        return;

    PersistedPairStore stored{};
    const bool readOk = file.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) == sizeof(stored);
    file.close();
    const uint32_t expectedCrc = crc32Buffer(&stored, offsetof(PersistedPairStore, crc));
    if (!readOk || stored.magic != PAIR_STORE_MAGIC || stored.version != PAIR_STORE_VERSION ||
        stored.count > BLE_MESH_MAX_PAIRED_NODES || stored.crc != expectedCrc || !nodeDB ||
        stored.localNode != nodeDB->getNodeNum()) {
        LOG_WARN("BLE pairing: ignoring invalid or stale peer store");
        return;
    }

    for (uint8_t i = 0; i < stored.count; i++) {
        if (stored.nodes[i] == 0 || stored.nodes[i] == nodeDB->getNodeNum() || findPair(stored.nodes[i]) >= 0)
            continue;
        pairs[pairCount++] = stored.nodes[i];
    }
    LOG_INFO("BLE pairing: restored %u approved peer(s)", pairCount);
#endif
}

bool BLEMeshHandler::savePairs()
{
#ifdef FSCom
    PersistedPairStore stored{};
    stored.magic = PAIR_STORE_MAGIC;
    stored.version = PAIR_STORE_VERSION;
    if (!nodeDB)
        return false;
    stored.localNode = nodeDB->getNodeNum();
    {
        concurrency::LockGuard guard(&pairLock);
        stored.count = pairCount;
        for (uint8_t i = 0; i < pairCount; i++)
            stored.nodes[i] = pairs[i];
    }
    stored.crc = crc32Buffer(&stored, offsetof(PersistedPairStore, crc));

    {
        concurrency::LockGuard fsGuard(spiLock);
        FSCom.mkdir("/prefs");
    }
    SafeFile file(PAIR_STORE_FILE, true);
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
    return written == sizeof(stored) && file.close();
#else
    return true;
#endif
}

int BLEMeshHandler::findPair(NodeNum nodeNum)
{
    for (uint8_t i = 0; i < pairCount; i++) {
        if (pairs[i] == nodeNum)
            return i;
    }
    return -1;
}

bool BLEMeshHandler::addPair(NodeNum nodeNum, const uint8_t publicKey[32])
{
    ensurePairsLoaded();
    if (!validateIdentity(nodeNum, publicKey))
        return false;

    std::array<NodeNum, BLE_MESH_MAX_PAIRED_NODES> previousPairs;
    uint8_t previousCount;
    {
        concurrency::LockGuard guard(&pairLock);
        previousPairs = pairs;
        previousCount = pairCount;
        int index = findPair(nodeNum);
        if (index < 0) {
            if (pairCount >= BLE_MESH_MAX_PAIRED_NODES)
                return false;
            index = pairCount++;
        }
        pairs[index] = nodeNum;
    }
    if (savePairs()) {
        nodeDB->commitRemoteKey(nodeNum, publicKey, NodeDB::KeyCommitTrust::ManuallyVerified);
        auto *node = nodeDB->getMeshNode(nodeNum);
        if (node)
            nodeInfoLiteSetBit(node, NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK, true);
        nodeDB->saveToDisk(SEGMENT_NODEDATABASE);
        return true;
    }
    concurrency::LockGuard guard(&pairLock);
    pairs = previousPairs;
    pairCount = previousCount;
    return false;
}

#ifdef PIO_UNIT_TESTING
bool BLEMeshHandler::addPairForTest(NodeNum nodeNum, const uint8_t publicKey[32])
{
    return addPair(nodeNum, publicKey);
}
#endif

bool BLEMeshHandler::forgetPeer(NodeNum nodeNum)
{
    ensurePairsLoaded();
    std::array<NodeNum, BLE_MESH_MAX_PAIRED_NODES> previousPairs;
    uint8_t previousCount;
    {
        concurrency::LockGuard guard(&pairLock);
        previousPairs = pairs;
        previousCount = pairCount;
        int index = findPair(nodeNum);
        if (index < 0)
            return false;
        for (uint8_t i = static_cast<uint8_t>(index); i + 1 < pairCount; i++)
            pairs[i] = pairs[i + 1];
        pairs[--pairCount] = 0;
    }
    if (savePairs())
        return true;
    concurrency::LockGuard guard(&pairLock);
    pairs = previousPairs;
    pairCount = previousCount;
    return false;
}

bool BLEMeshHandler::isPaired(NodeNum nodeNum)
{
    ensurePairsLoaded();
    concurrency::LockGuard guard(&pairLock);
    return findPair(nodeNum) >= 0;
}

uint8_t BLEMeshHandler::pairedCount()
{
    ensurePairsLoaded();
    concurrency::LockGuard guard(&pairLock);
    return pairCount;
}

bool BLEMeshHandler::deriveBridgeKey(const uint8_t peerKey[32], uint8_t out[32])
{
    meshtastic_NodeInfoLite_public_key_t remote = {32, {0}};
    memcpy(remote.bytes, peerKey, 32);
    concurrency::LockGuard guard(cryptLock);
    return crypto && crypto->deriveSharedKey(remote, BRIDGE_KEY_CONTEXT, sizeof(BRIDGE_KEY_CONTEXT) - 1, out);
}

bool BLEMeshHandler::beginPairing(PairingCandidateCallback callback)
{
    if (!isRunning || !nodeDB || config.security.public_key.size != 32 || config.security.private_key.size != 32 ||
        !validateIdentity(nodeDB->getNodeNum(), config.security.public_key.bytes))
        return false;

    uint64_t freshNonce = 0;
    {
        concurrency::LockGuard guard(cryptLock);
        if (!HardwareRNG::fill(reinterpret_cast<uint8_t *>(&freshNonce), sizeof(freshNonce)))
            CryptRNG.rand(reinterpret_cast<uint8_t *>(&freshNonce), sizeof(freshNonce));
    }
    if (freshNonce == 0)
        return false;

    {
        concurrency::LockGuard guard(&pairLock);
        pairingActive = true;
        pairingStartedMs = Time::getMillis();
        lastPairingAdvertisementMs = 0;
        pairingNonce = freshNonce;
        pairingCandidate = PairingCandidate{};
        pairingCallback = callback;
    }
    setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
    LOG_INFO("BLE pairing: discovery window opened");
    return true;
}

void BLEMeshHandler::cancelPairing()
{
    concurrency::LockGuard guard(&pairLock);
    pairingActive = false;
    pairingStartedMs = 0;
    lastPairingAdvertisementMs = 0;
    pairingNonce = 0;
    pairingCandidate = PairingCandidate{};
    pairingCallback = nullptr;
}

uint32_t BLEMeshHandler::pairingVerificationCode(const PairingCandidate &candidate)
{
    uint8_t bridgeKey[32];
    if (!deriveBridgeKey(candidate.publicKey, bridgeKey))
        return UINT32_MAX;

    const NodeNum localNode = nodeDB->getNodeNum();
    SHA256 digest;
    digest.reset();
    digest.update(bridgeKey, sizeof(bridgeKey));
    if (localNode < candidate.nodeNum) {
        digest.update(&localNode, sizeof(localNode));
        digest.update(&pairingNonce, sizeof(pairingNonce));
        digest.update(config.security.public_key.bytes, 32);
        digest.update(&candidate.nodeNum, sizeof(candidate.nodeNum));
        digest.update(&candidate.nonce, sizeof(candidate.nonce));
        digest.update(candidate.publicKey, 32);
    } else {
        digest.update(&candidate.nodeNum, sizeof(candidate.nodeNum));
        digest.update(&candidate.nonce, sizeof(candidate.nonce));
        digest.update(candidate.publicKey, 32);
        digest.update(&localNode, sizeof(localNode));
        digest.update(&pairingNonce, sizeof(pairingNonce));
        digest.update(config.security.public_key.bytes, 32);
    }
    uint8_t result[32];
    digest.finalize(result, sizeof(result));
    memset(bridgeKey, 0, sizeof(bridgeKey));
    uint32_t code = readU32(result) % 1000000;
    memset(result, 0, sizeof(result));
    return code;
}

bool BLEMeshHandler::approvePairingCandidate()
{
    PairingCandidate candidate;
    {
        concurrency::LockGuard guard(&pairLock);
        if (!pairingActive || !pairingCandidate.valid)
            return false;
        candidate = pairingCandidate;
    }

    if (!addPair(candidate.nodeNum, candidate.publicKey))
        return false;

    cancelPairing();
    LOG_INFO("BLE pairing: approved node 0x%08x", candidate.nodeNum);
    return true;
}

uint8_t BLEMeshHandler::buildPairingAdvertisement(uint8_t *out, size_t outCap)
{
    const size_t total = BLE_MESH_ADV_OVERHEAD + PAIRING_HELLO_LEN;
    if (!out || outCap < total || !pairingActive || config.security.public_key.size != 32)
        return 0;

    writeAdvertisementPrefix(out, total);
    uint8_t *frame = out + BLE_MESH_ADV_OVERHEAD;
    frame[0] = BLE_MESH_FRAME_PAIRING_HELLO;
    writeU32(frame + 1, nodeDB->getNodeNum());
    writeU64(frame + 5, pairingNonce);
    memcpy(frame + 13, config.security.public_key.bytes, 32);
    return static_cast<uint8_t>(total);
}

uint8_t BLEMeshHandler::buildAdvPayload(const meshtastic_MeshPacket *mp, NodeNum peer, uint8_t *out, size_t outCap)
{
    // Router::send() encrypts before it reaches any transport, so an unencrypted packet here is a
    // bug upstream, not something to quietly put on the air.
    if (!mp || !out || mp->which_payload_variant != meshtastic_MeshPacket_encrypted_tag || mp->from == 0)
        return 0;

    meshtastic_NodeInfoLite_public_key_t remote = {0, {0}};
    if (!nodeDB || !nodeDB->copyPublicKeyForDecrypt(peer, remote))
        return 0;

    uint8_t proto[BLE_MESH_MAX_PROTO_LEN];
    const size_t protoLen = pb_encode_to_bytes(proto, sizeof(proto), &meshtastic_MeshPacket_msg, mp);
    if (protoLen == 0) {
        // pb_encode_to_bytes returns 0 both for a genuine encode failure and for a packet that does
        // not fit the buffer. Either way it cannot ride BLE; it still goes out over LoRa.
        LOG_WARN("BLE mesh: drop 0x%08x, does not fit authenticated advertisement", mp->id);
        return 0;
    }

    const size_t total = BLE_MESH_ADV_OVERHEAD + BLE_MESH_DATA_HEADER_LEN + protoLen + MESHTASTIC_PKC_OVERHEAD;
    if (total > outCap || total > BLE_MESH_ADV_TOTAL_MAX)
        return 0;

    writeAdvertisementPrefix(out, total);
    uint8_t *header = out + BLE_MESH_ADV_OVERHEAD;
    header[0] = BLE_MESH_FRAME_DATA;
    writeU32(header + 1, nodeDB->getNodeNum());
    writeU32(header + 5, peer);
    writeU32(header + 9, mp->id);

    uint8_t *ciphertext = header + BLE_MESH_DATA_HEADER_LEN;
    {
        concurrency::LockGuard guard(cryptLock);
        if (!crypto || !crypto->encryptCurve25519(peer, nodeDB->getNodeNum() ^ BRIDGE_NONCE_DOMAIN, remote, mp->id, protoLen,
                                                  proto, ciphertext))
            return 0;
    }
    return static_cast<uint8_t>(total);
}

bool BLEMeshHandler::onSend(const meshtastic_MeshPacket *mp)
{
    if (!isRunning || !mp)
        return false;

    ensurePairsLoaded();
    std::array<NodeNum, BLE_MESH_MAX_PAIRED_NODES> approved{};
    uint8_t approvedCount;
    {
        concurrency::LockGuard guard(&pairLock);
        approvedCount = pairCount;
        for (uint8_t i = 0; i < pairCount; i++)
            approved[i] = pairs[i];
    }

    bool queued = false;
    for (uint8_t i = 0; i < approvedCount; i++) {
        // BLE-sourced packets are valid rebroadcasts. PacketHistory and hop_limit provide the same
        // loop protection used by the LoRa path.
        if (txCount >= BLE_MESH_TX_QUEUE_SIZE) {
            LOG_WARN("BLE mesh: TX queue full, dropping peer copy of 0x%08x", mp->id);
            break;
        }
        AdvSlot slot;
        slot.len = buildAdvPayload(mp, approved[i], slot.data.data(), slot.data.size());
        if (slot.len == 0)
            continue;
        txQueue[txTail] = slot;
        txTail = (txTail + 1) % BLE_MESH_TX_QUEUE_SIZE;
        txCount++;
        queued = true;
    }

    if (queued) {
        setIntervalFromNow(0);
        concurrency::mainDelay.interrupt();
    }
    return queued;
}

int32_t BLEMeshHandler::runOnce()
{
    if (!isRunning || !platformReady())
        return 500;

    if (!readyHandled) {
        readyHandled = true;
        onBluetoothReady();
    }

    if (advertising) {
        if (platformAdvertisingActive())
            return 10;
        platformEndAdvertising();
        advertising = false;
    }

    PairingCandidate candidate;
    PairingCandidateCallback callback;
    bool pairingTimedOut = false;
    {
        concurrency::LockGuard guard(&pairLock);
        if (pairingActive && Throttle::hasElapsed(pairingStartedMs, BLE_MESH_PAIRING_TIMEOUT_MS)) {
            pairingTimedOut = true;
        } else if (pairingActive && pairingCandidate.valid && !pairingCandidate.notified) {
            pairingCandidate.notified = true;
            candidate = pairingCandidate;
            callback = pairingCallback;
        }
    }
    if (pairingTimedOut) {
        LOG_INFO("BLE pairing: discovery window expired");
        cancelPairing();
    } else if (callback) {
        const uint32_t code = pairingVerificationCode(candidate);
        if (code != UINT32_MAX)
            callback(candidate.nodeNum, code);
    }

    if (txCount != 0) {
        AdvSlot slot = txQueue[txHead];
        txHead = (txHead + 1) % BLE_MESH_TX_QUEUE_SIZE;
        txCount--;
        if (platformBeginAdvertising(slot.data.data(), slot.len))
            advertising = true;
        return 10;
    }

    bool pairingDue = false;
    {
        concurrency::LockGuard guard(&pairLock);
        pairingDue = pairingActive && (lastPairingAdvertisementMs == 0 ||
                                       Throttle::hasElapsed(lastPairingAdvertisementMs, BLE_MESH_PAIRING_ADV_INTERVAL_MS));
    }
    if (pairingDue) {
        uint8_t adv[BLE_MESH_ADV_TOTAL_MAX];
        const uint8_t len = buildPairingAdvertisement(adv, sizeof(adv));
        if (len && platformBeginAdvertising(adv, len)) {
            concurrency::LockGuard guard(&pairLock);
            lastPairingAdvertisementMs = Time::getMillis();
            advertising = true;
        }
        return 10;
    }

    return 100;
}

void BLEMeshHandler::handlePairingHello(const uint8_t *data, size_t len, int8_t rssi)
{
    if (len != PAIRING_HELLO_LEN)
        return;

    const NodeNum sender = readU32(data + 1);
    const uint64_t nonce = readU64(data + 5);
    const uint8_t *publicKey = data + 13;
    if (sender == nodeDB->getNodeNum() || nonce == 0 || !validateIdentity(sender, publicKey))
        return;

    concurrency::LockGuard guard(&pairLock);
    if (!pairingActive)
        return;
    if (pairingCandidate.valid && pairingCandidate.notified && pairingCandidate.nodeNum != sender)
        return;
    if (!pairingCandidate.valid || pairingCandidate.nodeNum == sender || rssi > pairingCandidate.rssi) {
        const bool sameCandidate = pairingCandidate.valid && pairingCandidate.nodeNum == sender &&
                                   pairingCandidate.nonce == nonce && memcmp(pairingCandidate.publicKey, publicKey, 32) == 0;
        pairingCandidate.valid = true;
        pairingCandidate.notified = sameCandidate ? pairingCandidate.notified : false;
        pairingCandidate.nodeNum = sender;
        pairingCandidate.nonce = nonce;
        pairingCandidate.rssi = rssi;
        memcpy(pairingCandidate.publicKey, publicKey, 32);
        concurrency::mainDelay.interrupt();
    }
}

void BLEMeshHandler::handleAuthenticatedData(const uint8_t *data, size_t len, int8_t rssi)
{
    if (len <= BLE_MESH_DATA_HEADER_LEN + MESHTASTIC_PKC_OVERHEAD)
        return;

    const NodeNum sender = readU32(data + 1);
    const NodeNum recipient = readU32(data + 5);
    const uint32_t frameId = readU32(data + 9);
    // Nothing legitimate has no bridge sender, and our own advertisement echoing back would loop.
    if (sender == 0 || sender == nodeDB->getNodeNum() || recipient != nodeDB->getNodeNum())
        return;

    meshtastic_NodeInfoLite_public_key_t remote = {0, {0}};
    {
        concurrency::LockGuard guard(&pairLock);
        if (findPair(sender) < 0 || !nodeDB->copyPublicKeyForDecrypt(sender, remote))
            return;
    }

    const size_t encryptedLen = len - BLE_MESH_DATA_HEADER_LEN;
    const size_t plaintextLen = encryptedLen - MESHTASTIC_PKC_OVERHEAD;
    if (plaintextLen > BLE_MESH_MAX_PROTO_LEN)
        return;
    const uint8_t *ciphertext = data + BLE_MESH_DATA_HEADER_LEN;
    uint8_t plaintext[BLE_MESH_MAX_PROTO_LEN];
    bool authenticated;
    {
        concurrency::LockGuard guard(cryptLock);
        authenticated = crypto && crypto->decryptCurve25519(sender ^ BRIDGE_NONCE_DOMAIN, remote, frameId, encryptedLen,
                                                            ciphertext, plaintext);
    }
    if (!authenticated)
        return;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    if (!pb_decode_from_bytes(plaintext, plaintextLen, &meshtastic_MeshPacket_msg, &mp))
        return;
    // An out-of-range hop count is not relayable; UdpMulticastHandler drops it identically.
    if (mp.which_payload_variant != meshtastic_MeshPacket_encrypted_tag || mp.from == 0 || mp.from == nodeDB->getNodeNum() ||
        mp.hop_limit > HOP_MAX || mp.hop_start > HOP_MAX)
        return;

    // Wire-carried flags and authentication metadata are local-only; rebuild local BLE metadata.
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV;
    mp.via_mqtt = false;
    mp.tx_after = 0;
    mp.priority = meshtastic_MeshPacket_Priority_UNSET;
    mp.pki_encrypted = false;
    mp.public_key.size = 0;
    memset(mp.public_key.bytes, 0, sizeof(mp.public_key.bytes));
    mp.rx_snr = 0;
    mp.rx_rssi = rssi;
    mp.has_rx_rssi = true;

    UniquePacketPoolPacket packet = packetPool.allocUniqueCopy(mp);
    if (!packet)
        return;
    LOG_DEBUG("BLE mesh authenticated RX bridge=0x%08x from=0x%08x id=0x%08x", sender, mp.from, mp.id);
    enqueueReceived(packet.release());
}

void BLEMeshHandler::deliverToRouter(const uint8_t *data, size_t len, int8_t rssi)
{
    if (!isRunning || !nodeDB || !data || len == 0)
        return;
    ensurePairsLoaded();
    if (data[0] == BLE_MESH_FRAME_PAIRING_HELLO)
        handlePairingHello(data, len, rssi);
    else if (data[0] == BLE_MESH_FRAME_DATA)
        handleAuthenticatedData(data, len, rssi);
}

void BLEMeshHandler::enqueueReceived(meshtastic_MeshPacket *p)
{
    router->enqueueReceivedMessage(p);
}

#endif // HAS_BLE_MESH
