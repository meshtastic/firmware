#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"
#include <stddef.h>
#include <stdint.h>

/**
 * BLE advertisement bearer for encrypted MeshPacket protobufs.
 *
 * The platform BLE layer is intentionally kept outside this codec.  ESP32 and
 * nRF52 implementations can carry these frames in service data, manufacturer
 * data, or an extended advertising set without changing router semantics.
 */
class BleAdvertisementMeshCodec
{
  public:
    static constexpr uint8_t FRAME_MAGIC_0 = 'M';
    static constexpr uint8_t FRAME_MAGIC_1 = 'T';
    static constexpr uint8_t FRAME_VERSION = 1;
    // Legacy BLE advertisements are 31 bytes including AD type overhead.  A
    // manufacturer-data bearer leaves 27 bytes for our frame after the 16-bit
    // company id.
    static constexpr size_t MAX_FRAME_SIZE = 27;
    static constexpr size_t FRAME_HEADER_SIZE = 16;
    static constexpr size_t MAX_FRAGMENT_PAYLOAD_SIZE = MAX_FRAME_SIZE - FRAME_HEADER_SIZE;
    static constexpr uint8_t MAX_FRAGMENTS = 64;

    typedef bool (*EmitFrame)(const uint8_t *frame, size_t frameLength, void *context);

    BleAdvertisementMeshCodec();

    /**
     * Encode an encrypted MeshPacket into one or more BLE ADV frames.
     */
    static bool forEachFrame(const meshtastic_MeshPacket *mp, EmitFrame emit, void *context);
    static uint8_t frameCount(const meshtastic_MeshPacket *mp);

    /**
     * Accept one BLE ADV frame. Returns true when a complete MeshPacket has
     * been reassembled into out.
     */
    bool receiveFrame(const uint8_t *frame, size_t frameLength, meshtastic_MeshPacket *out);

  private:
    uint32_t currentFrom = 0;
    uint32_t currentPacketId = 0;
    uint16_t currentCrc = 0;
    uint8_t expectedFragments = 0;
    uint8_t lastFragmentLength = 0;
    uint64_t receivedMask = 0;
    uint8_t encodedPacket[meshtastic_MeshPacket_size] = {0};

    void reset(uint32_t from, uint32_t packetId, uint16_t crc, uint8_t fragmentCount);
};

#if HAS_BLE_MESH_ADVERTISING

#include "MeshTypes.h"
#include "concurrency/OSThread.h"

class BleAdvertisementMesh : public concurrency::OSThread
{
  public:
    typedef bool (*PlatformAdvertiseFrame)(const uint8_t *frame, size_t frameLength);

    struct QueuedFrame
    {
        uint8_t data[BleAdvertisementMeshCodec::MAX_FRAME_SIZE] = {0};
        uint8_t length = 0;
    };

    explicit BleAdvertisementMesh(PlatformAdvertiseFrame advertiseFrame);

    bool onSend(const meshtastic_MeshPacket *mp);
    void onAdvertisement(const uint8_t *frame, size_t frameLength);

  private:
    static constexpr uint8_t MAX_PACKET_FRAMES =
        (meshtastic_MeshPacket_size + BleAdvertisementMeshCodec::MAX_FRAGMENT_PAYLOAD_SIZE - 1) /
        BleAdvertisementMeshCodec::MAX_FRAGMENT_PAYLOAD_SIZE;
    static constexpr uint8_t FRAME_REPEAT_ROUNDS = 8;
    static constexpr uint16_t FRAME_QUEUE_SIZE = MAX_PACKET_FRAMES * FRAME_REPEAT_ROUNDS;
    static constexpr uint32_t FRAME_INTERVAL_MS = 140;
    static constexpr uint8_t RECENT_PACKET_COUNT = 8;

    PlatformAdvertiseFrame advertiseFrame = nullptr;
    BleAdvertisementMeshCodec codec;
    QueuedFrame queue[FRAME_QUEUE_SIZE];
    uint16_t queueHead = 0;
    uint16_t queueTail = 0;
    uint16_t queuedFrames = 0;
    uint32_t recentFrom[RECENT_PACKET_COUNT] = {0};
    uint32_t recentPacketId[RECENT_PACKET_COUNT] = {0};
    uint8_t recentPacketIndex = 0;

    uint16_t freeSlots() const { return FRAME_QUEUE_SIZE - queuedFrames; }
    bool enqueueFrame(const uint8_t *frame, size_t frameLength);
    bool rememberPacket(uint32_t from, uint32_t packetId);
    virtual int32_t runOnce() override;
};

extern BleAdvertisementMesh *bleAdvertisementMesh;

#endif // HAS_BLE_MESH_ADVERTISING
