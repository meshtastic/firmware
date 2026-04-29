#include "mesh/ble/BleAdvertisementMesh.h"
#include <Arduino.h>
#include <stdlib.h>
#include <unity.h>

namespace
{
struct Capture
{
    uint8_t frames[BleAdvertisementMeshCodec::MAX_FRAGMENTS][BleAdvertisementMeshCodec::MAX_FRAME_SIZE] = {};
    size_t lengths[BleAdvertisementMeshCodec::MAX_FRAGMENTS] = {};
    size_t count = 0;
};

bool captureFrame(const uint8_t *frame, size_t frameLength, void *context)
{
    auto *capture = static_cast<Capture *>(context);
    if (capture->count >= BleAdvertisementMeshCodec::MAX_FRAGMENTS) {
        return false;
    }
    memcpy(capture->frames[capture->count], frame, frameLength);
    capture->lengths[capture->count] = frameLength;
    capture->count++;
    return true;
}

meshtastic_MeshPacket makePacket()
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0x11223344;
    mp.to = 0xffffffff;
    mp.id = 0x55667788;
    mp.channel = 8;
    mp.hop_limit = 3;
    mp.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    mp.encrypted.size = 32;
    for (uint8_t i = 0; i < mp.encrypted.size; i++) {
        mp.encrypted.bytes[i] = i;
    }
    return mp;
}

meshtastic_MeshPacket makeMaxEncryptedPacket()
{
    meshtastic_MeshPacket mp = makePacket();
    mp.encrypted.size = sizeof(mp.encrypted.bytes);
    for (uint16_t i = 0; i < mp.encrypted.size; i++) {
        mp.encrypted.bytes[i] = i & 0xff;
    }
    return mp;
}
} // namespace

void test_ble_adv_codec_round_trips_fragmented_mesh_packet()
{
    meshtastic_MeshPacket original = makePacket();
    Capture capture;

    TEST_ASSERT_TRUE(BleAdvertisementMeshCodec::forEachFrame(&original, captureFrame, &capture));
    TEST_ASSERT_GREATER_THAN(1, capture.count);
    TEST_ASSERT_EQUAL_UINT8(capture.count, BleAdvertisementMeshCodec::frameCount(&original));

    BleAdvertisementMeshCodec codec;
    meshtastic_MeshPacket decoded = meshtastic_MeshPacket_init_zero;
    bool complete = false;
    for (size_t i = 0; i < capture.count; i++) {
        complete = codec.receiveFrame(capture.frames[i], capture.lengths[i], &decoded);
    }

    TEST_ASSERT_TRUE(complete);
    TEST_ASSERT_EQUAL_UINT32(original.from, decoded.from);
    TEST_ASSERT_EQUAL_UINT32(original.to, decoded.to);
    TEST_ASSERT_EQUAL_UINT32(original.id, decoded.id);
    TEST_ASSERT_EQUAL_UINT8(original.channel, decoded.channel);
    TEST_ASSERT_EQUAL_UINT8(original.hop_limit, decoded.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(original.encrypted.size, decoded.encrypted.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.encrypted.bytes, decoded.encrypted.bytes, original.encrypted.size);
}

void test_ble_adv_codec_round_trips_max_encrypted_mesh_packet()
{
    meshtastic_MeshPacket original = makeMaxEncryptedPacket();
    Capture capture;

    TEST_ASSERT_TRUE(BleAdvertisementMeshCodec::forEachFrame(&original, captureFrame, &capture));
    TEST_ASSERT_GREATER_THAN(8, capture.count);
    TEST_ASSERT_LESS_OR_EQUAL(BleAdvertisementMeshCodec::MAX_FRAGMENTS, capture.count);
    TEST_ASSERT_EQUAL_UINT8(capture.count, BleAdvertisementMeshCodec::frameCount(&original));

    BleAdvertisementMeshCodec codec;
    meshtastic_MeshPacket decoded = meshtastic_MeshPacket_init_zero;
    bool complete = false;
    for (size_t i = 0; i < capture.count; i++) {
        complete = codec.receiveFrame(capture.frames[i], capture.lengths[i], &decoded);
    }

    TEST_ASSERT_TRUE(complete);
    TEST_ASSERT_EQUAL_UINT32(original.from, decoded.from);
    TEST_ASSERT_EQUAL_UINT32(original.id, decoded.id);
    TEST_ASSERT_EQUAL_UINT16(original.encrypted.size, decoded.encrypted.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.encrypted.bytes, decoded.encrypted.bytes, original.encrypted.size);
}

void test_ble_adv_codec_rejects_crc_mismatch()
{
    meshtastic_MeshPacket original = makePacket();
    Capture capture;
    TEST_ASSERT_TRUE(BleAdvertisementMeshCodec::forEachFrame(&original, captureFrame, &capture));

    capture.frames[capture.count - 1][capture.lengths[capture.count - 1] - 1] ^= 0x55;

    BleAdvertisementMeshCodec codec;
    meshtastic_MeshPacket decoded = meshtastic_MeshPacket_init_zero;
    bool complete = false;
    for (size_t i = 0; i < capture.count; i++) {
        complete = codec.receiveFrame(capture.frames[i], capture.lengths[i], &decoded);
    }

    TEST_ASSERT_FALSE(complete);
}

void setup()
{
    delay(10);
    UNITY_BEGIN();
    RUN_TEST(test_ble_adv_codec_round_trips_fragmented_mesh_packet);
    RUN_TEST(test_ble_adv_codec_round_trips_max_encrypted_mesh_packet);
    RUN_TEST(test_ble_adv_codec_rejects_crc_mismatch);
    exit(UNITY_END());
}

void loop() {}
