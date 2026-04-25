#include "configuration.h"
#include "mesh/ble/BleAdvertisementMesh.h"
#include "mesh/mesh-pb-constants.h"

#if HAS_BLE_MESH_ADVERTISING
#include "Router.h"
#include "main.h"
#endif
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>

namespace
{
uint16_t crc16ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

void putLe16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void putLe32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint16_t getLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t getLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
} // namespace

BleAdvertisementMeshCodec::BleAdvertisementMeshCodec() {}

uint8_t BleAdvertisementMeshCodec::frameCount(const meshtastic_MeshPacket *mp)
{
    if (!mp || mp->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) {
        return 0;
    }

    uint8_t encoded[meshtastic_MeshPacket_size] = {0};
    size_t encodedLength = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_MeshPacket_msg, mp);
    if (encodedLength == 0) {
        return 0;
    }

    return (encodedLength + MAX_FRAGMENT_PAYLOAD_SIZE - 1) / MAX_FRAGMENT_PAYLOAD_SIZE;
}

bool BleAdvertisementMeshCodec::forEachFrame(const meshtastic_MeshPacket *mp, EmitFrame emit, void *context)
{
    if (!mp || !emit || mp->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) {
        return false;
    }

    uint8_t encoded[meshtastic_MeshPacket_size] = {0};
    size_t encodedLength = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_MeshPacket_msg, mp);
    if (encodedLength == 0) {
        return false;
    }

    uint8_t fragmentCount = (encodedLength + MAX_FRAGMENT_PAYLOAD_SIZE - 1) / MAX_FRAGMENT_PAYLOAD_SIZE;
    if (fragmentCount == 0 || fragmentCount > MAX_FRAGMENTS) {
        return false;
    }

    uint16_t crc = crc16ccitt(encoded, encodedLength);
    for (uint8_t fragment = 0; fragment < fragmentCount; fragment++) {
        size_t offset = fragment * MAX_FRAGMENT_PAYLOAD_SIZE;
        size_t remaining = encodedLength - offset;
        uint8_t payloadLength =
            static_cast<uint8_t>(remaining > MAX_FRAGMENT_PAYLOAD_SIZE ? MAX_FRAGMENT_PAYLOAD_SIZE : remaining);

        uint8_t frame[MAX_FRAME_SIZE] = {0};
        frame[0] = FRAME_MAGIC_0;
        frame[1] = FRAME_MAGIC_1;
        frame[2] = FRAME_VERSION;
        frame[3] = fragment;
        frame[4] = fragmentCount;
        frame[5] = payloadLength;
        putLe32(&frame[6], mp->from);
        putLe32(&frame[10], mp->id);
        putLe16(&frame[14], crc);
        memcpy(&frame[FRAME_HEADER_SIZE], &encoded[offset], payloadLength);

        if (!emit(frame, FRAME_HEADER_SIZE + payloadLength, context)) {
            return false;
        }
    }

    return true;
}

void BleAdvertisementMeshCodec::reset(uint32_t from, uint32_t packetId, uint16_t crc, uint8_t fragmentCount)
{
    currentFrom = from;
    currentPacketId = packetId;
    currentCrc = crc;
    expectedFragments = fragmentCount;
    lastFragmentLength = 0;
    receivedMask = 0;
    memset(encodedPacket, 0, sizeof(encodedPacket));
}

bool BleAdvertisementMeshCodec::receiveFrame(const uint8_t *frame, size_t frameLength, meshtastic_MeshPacket *out)
{
    if (!frame || !out || frameLength < FRAME_HEADER_SIZE || frameLength > MAX_FRAME_SIZE || frame[0] != FRAME_MAGIC_0 ||
        frame[1] != FRAME_MAGIC_1 || frame[2] != FRAME_VERSION) {
        return false;
    }

    uint8_t fragment = frame[3];
    uint8_t fragmentCount = frame[4];
    uint8_t payloadLength = frame[5];
    uint32_t from = getLe32(&frame[6]);
    uint32_t packetId = getLe32(&frame[10]);
    uint16_t crc = getLe16(&frame[14]);

    if (fragmentCount == 0 || fragmentCount > MAX_FRAGMENTS || fragment >= fragmentCount ||
        payloadLength > MAX_FRAGMENT_PAYLOAD_SIZE || frameLength != FRAME_HEADER_SIZE + payloadLength) {
        return false;
    }

    size_t offset = fragment * MAX_FRAGMENT_PAYLOAD_SIZE;
    if (offset + payloadLength > sizeof(encodedPacket)) {
        return false;
    }

    if (from != currentFrom || packetId != currentPacketId || crc != currentCrc || fragmentCount != expectedFragments) {
        reset(from, packetId, crc, fragmentCount);
    }

    memcpy(&encodedPacket[offset], &frame[FRAME_HEADER_SIZE], payloadLength);
    receivedMask |= 1ULL << fragment;
    if (fragment == fragmentCount - 1) {
        lastFragmentLength = payloadLength;
    }

    uint64_t completeMask = fragmentCount == 64 ? UINT64_MAX : ((1ULL << fragmentCount) - 1);
    if (receivedMask != completeMask || lastFragmentLength == 0) {
        return false;
    }

    size_t encodedLength = (fragmentCount - 1) * MAX_FRAGMENT_PAYLOAD_SIZE + lastFragmentLength;
    if (crc16ccitt(encodedPacket, encodedLength) != currentCrc) {
        reset(0, 0, 0, 0);
        return false;
    }

    memset(out, 0, sizeof(*out));
    bool decoded = pb_decode_from_bytes(encodedPacket, encodedLength, &meshtastic_MeshPacket_msg, out);
    reset(0, 0, 0, 0);
    return decoded;
}

#if HAS_BLE_MESH_ADVERTISING

BleAdvertisementMesh *bleAdvertisementMesh = nullptr;

namespace
{
struct PlatformEmitContext
{
    BleAdvertisementMesh::QueuedFrame *frames;
    uint8_t maxFrames;
    uint8_t count;
};

bool emitPlatformFrame(const uint8_t *frame, size_t frameLength, void *context)
{
    auto *platformContext = static_cast<PlatformEmitContext *>(context);
    if (!platformContext || !frame || frameLength == 0 || frameLength > BleAdvertisementMeshCodec::MAX_FRAME_SIZE ||
        platformContext->count >= platformContext->maxFrames) {
        return false;
    }

    BleAdvertisementMesh::QueuedFrame &queued = platformContext->frames[platformContext->count++];
    memcpy(queued.data, frame, frameLength);
    queued.length = frameLength;
    return true;
}
} // namespace

BleAdvertisementMesh::BleAdvertisementMesh(PlatformAdvertiseFrame advertiseFrame)
    : concurrency::OSThread("BleAdvMesh"), advertiseFrame(advertiseFrame)
{
}

bool BleAdvertisementMesh::onSend(const meshtastic_MeshPacket *mp)
{
    if (!mp || !advertiseFrame ||
        mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADVERTISEMENT) {
        return false;
    }

    uint8_t neededFrames = BleAdvertisementMeshCodec::frameCount(mp);
    uint16_t queuedCopies = static_cast<uint16_t>(neededFrames) * FRAME_REPEAT_ROUNDS;
    if (neededFrames == 0 || queuedCopies > freeSlots()) {
        LOG_WARN("Drop BLE advertisement broadcast id=%u, fragments=%u free=%u", mp->id, neededFrames, freeSlots());
        return false;
    }

    QueuedFrame frames[BleAdvertisementMeshCodec::MAX_FRAGMENTS];
    PlatformEmitContext context = {frames, BleAdvertisementMeshCodec::MAX_FRAGMENTS, 0};
    if (!BleAdvertisementMeshCodec::forEachFrame(mp, emitPlatformFrame, &context) || context.count != neededFrames) {
        return false;
    }

    LOG_DEBUG("Queue packet for BLE advertisements (id=%u fragments=%u rounds=%u)", mp->id, neededFrames, FRAME_REPEAT_ROUNDS);
    for (uint8_t round = 0; round < FRAME_REPEAT_ROUNDS; round++) {
        for (uint8_t i = 0; i < context.count; i++) {
            const QueuedFrame &frame = context.frames[(round + i) % context.count];
            if (!enqueueFrame(frame.data, frame.length)) {
                return false;
            }
        }
    }

    return true;
}

void BleAdvertisementMesh::onAdvertisement(const uint8_t *frame, size_t frameLength)
{
    meshtastic_MeshPacket mp = {};
    if (!codec.receiveFrame(frame, frameLength, &mp)) {
        return;
    }

    if (!rememberPacket(mp.from, mp.id)) {
        LOG_DEBUG("Drop duplicate BLE advertisement packet (from=0x%x id=%u)", mp.from, mp.id);
        return;
    }

    if (router && mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADVERTISEMENT;
        UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
        p->rx_snr = 0;
        p->rx_rssi = 0;
        router->enqueueReceivedMessage(p.release());
    }
}

bool BleAdvertisementMesh::enqueueFrame(const uint8_t *frame, size_t frameLength)
{
    if (!frame || frameLength == 0 || frameLength > BleAdvertisementMeshCodec::MAX_FRAME_SIZE || queuedFrames >= FRAME_QUEUE_SIZE) {
        return false;
    }

    QueuedFrame &queued = queue[queueTail];
    memcpy(queued.data, frame, frameLength);
    queued.length = frameLength;
    queueTail = (queueTail + 1) % FRAME_QUEUE_SIZE;
    queuedFrames++;
    setIntervalFromNow(0);
    return true;
}

bool BleAdvertisementMesh::rememberPacket(uint32_t from, uint32_t packetId)
{
    for (uint8_t i = 0; i < RECENT_PACKET_COUNT; i++) {
        if (recentFrom[i] == from && recentPacketId[i] == packetId) {
            return false;
        }
    }

    recentFrom[recentPacketIndex] = from;
    recentPacketId[recentPacketIndex] = packetId;
    recentPacketIndex = (recentPacketIndex + 1) % RECENT_PACKET_COUNT;
    return true;
}

int32_t BleAdvertisementMesh::runOnce()
{
    if (!advertiseFrame || queuedFrames == 0) {
        return 1000;
    }

    QueuedFrame queued = queue[queueHead];
    queueHead = (queueHead + 1) % FRAME_QUEUE_SIZE;
    queuedFrames--;

    if (!advertiseFrame(queued.data, queued.length)) {
        LOG_WARN("BLE advertisement frame transmit failed");
    }

    return queuedFrames > 0 ? FRAME_INTERVAL_MS : 1000;
}

#endif // HAS_BLE_MESH_ADVERTISING
