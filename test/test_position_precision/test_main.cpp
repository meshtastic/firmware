#include "CryptoEngine.h"
#include "TestUtil.h"
#include "modules/PositionModule.h"
#include "mesh/TypeConversions.h"
#include <unity.h>
#include <cstdint>

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

void test_no_existing_data() {
    // Test with no existing position data - should always update
    int32_t lat = (int32_t)0x12345678;
    int32_t lon = (int32_t)0x87654321;
    
    // Create position structs
    meshtastic_PositionLite existingPos = meshtastic_PositionLite_init_default;
    existingPos.latitude_i = 0;
    existingPos.longitude_i = 0;
    existingPos.precision_bits = 0;
    
    meshtastic_Position incomingPos = meshtastic_Position_init_default;
    incomingPos.latitude_i = lat;
    incomingPos.longitude_i = lon;
    incomingPos.precision_bits = 16;
    
    // Simulate no existing position by testing against invalid coordinates
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(existingPos, incomingPos));
}

void test_same_position_different_precision() {
    // Test: same physical location, different precision levels
    int32_t lat = (int32_t)0x075BCD15;  // 123456789 in hex
    int32_t lon = (int32_t)0x3ADE68B1;  // 987654321 in hex
    
    // Create PositionLite structs for existing positions
    meshtastic_PositionLite highPrecPosLite = meshtastic_PositionLite_init_default;
    highPrecPosLite.latitude_i = lat;
    highPrecPosLite.longitude_i = lon;
    highPrecPosLite.precision_bits = 32;
    
    meshtastic_PositionLite lowPrecPosLite = meshtastic_PositionLite_init_default;
    lowPrecPosLite.latitude_i = lat;
    lowPrecPosLite.longitude_i = lon;
    lowPrecPosLite.precision_bits = 13;
    
    // Create Position structs for incoming positions
    meshtastic_Position lowPrecPos = meshtastic_Position_init_default;
    lowPrecPos.latitude_i = lat;
    lowPrecPos.longitude_i = lon;
    lowPrecPos.precision_bits = 13;
    
    meshtastic_Position highPrecPos = meshtastic_Position_init_default;
    highPrecPos.latitude_i = lat;
    highPrecPos.longitude_i = lon;
    highPrecPos.precision_bits = 32;
    
    // High precision -> Low precision: should NOT update (preserve high precision)
    TEST_ASSERT_FALSE(PositionModule::shouldUpdatePosition(highPrecPosLite, lowPrecPos));
    
    // Low precision -> High precision: should update
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(lowPrecPosLite, highPrecPos));
    
    // Same precision: should update (refreshes timestamp)
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(lowPrecPosLite, lowPrecPos));
}

void test_movement_detection() {
    // Test movement detection with hex coordinates
    int32_t lat1 = (int32_t)0x12345678;
    int32_t lon1 = (int32_t)0x87654321;
    
    // At 8-bit precision, change top byte to ensure detection
    int32_t lat2 = (int32_t)0x22345678; // Changed from 0x12 to 0x22
    int32_t lon2 = (int32_t)0x87654321; // Same longitude
    
    // Create PositionLite structs for existing positions
    meshtastic_PositionLite pos1HighLite = meshtastic_PositionLite_init_default;
    pos1HighLite.latitude_i = lat1;
    pos1HighLite.longitude_i = lon1;
    pos1HighLite.precision_bits = 32;
    
    meshtastic_PositionLite pos1LowLite = meshtastic_PositionLite_init_default;
    pos1LowLite.latitude_i = lat1;
    pos1LowLite.longitude_i = lon1;
    pos1LowLite.precision_bits = 8;
    
    // Create Position structs for incoming positions
    meshtastic_Position pos2Low = meshtastic_Position_init_default;
    pos2Low.latitude_i = lat2;
    pos2Low.longitude_i = lon2;
    pos2Low.precision_bits = 8;
    
    meshtastic_Position pos2High = meshtastic_Position_init_default;
    pos2High.latitude_i = lat2;
    pos2High.longitude_i = lon2;
    pos2High.precision_bits = 32;
    
    // Different positions should always update, regardless of precision
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(pos1HighLite, pos2Low));
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(pos1LowLite, pos2High));
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(pos1LowLite, pos2Low));
}

void test_sar_scenario() {
    // Test the specific Search and Rescue use case
    
    // Initial: High precision GPS position on private channel
    int32_t baseLat = (int32_t)0x075BCD15;
    int32_t baseLon = (int32_t)0x3ADE68B1;
    uint32_t privateChannelPrec = 32; // Full precision
    
    // Later: Same location received on public channel (degraded precision)
    uint32_t publicChannelPrec = 13; // ~610m accuracy
    
    // Create PositionLite struct for existing position
    meshtastic_PositionLite privatePosLite = meshtastic_PositionLite_init_default;
    privatePosLite.latitude_i = baseLat;
    privatePosLite.longitude_i = baseLon;
    privatePosLite.precision_bits = privateChannelPrec;
    
    // Create Position structs for incoming positions
    meshtastic_Position publicPos = meshtastic_Position_init_default;
    publicPos.latitude_i = baseLat;
    publicPos.longitude_i = baseLon;
    publicPos.precision_bits = publicChannelPrec;
    
    // Should NOT update - preserve the high precision data
    bool preserveHighPrec = PositionModule::shouldUpdatePosition(privatePosLite, publicPos);
    TEST_ASSERT_FALSE(preserveHighPrec);
    
    // Now: Actual movement detected even with lower precision
    int32_t movedLat = baseLat + (int32_t)0x927C0; // +600000 (~6km)
    int32_t movedLon = baseLon + (int32_t)0x927C0;
    
    meshtastic_Position movedPos = meshtastic_Position_init_default;
    movedPos.latitude_i = movedLat;
    movedPos.longitude_i = movedLon;
    movedPos.precision_bits = publicChannelPrec;
    
    // Should update - movement detected despite lower precision
    bool detectMovement = PositionModule::shouldUpdatePosition(privatePosLite, movedPos);
    TEST_ASSERT_TRUE(detectMovement);
}

void test_precision_bit_masking() {
    // Test the bit masking logic directly
    
    // For precision=13: mask should clear bottom 19 bits
    uint32_t mask13 = 0xFFFFFFFF << (32 - 13);
    TEST_ASSERT_EQUAL_HEX32(0xFFF80000, mask13);
    
    // Test masking effect on real coordinates
    int32_t original = (int32_t)0x075BCD15; // 123456789
    int32_t masked = original & mask13;
    TEST_ASSERT_EQUAL_HEX32(0x07580000, masked); // Bottom 19 bits cleared
    
    // Verify different coordinates in same precision bucket are treated as same
    int32_t coord1 = (int32_t)0x075B0000;
    int32_t coord2 = (int32_t)0x075BFFFF; // Same precision bucket at precision=13
    
    int32_t masked1 = coord1 & mask13;
    int32_t masked2 = coord2 & mask13;
    TEST_ASSERT_EQUAL(masked1, masked2); // Should be identical after masking
}

void test_real_gps_coordinates() {
    // Test with realistic GPS coordinates in hex
    
    // San Francisco: 37.7749° N, 122.4194° W
    // In int32 format: lat = 377749000 (0x1682F808), lon = -1224194000 (0xB6F64FB0)
    int32_t sfLat = (int32_t)0x1682F808;
    int32_t sfLon = (int32_t)0xB6F64FB0; // Negative value
    
    // Small movement within same precision bucket - should still update at same precision
    int32_t nearbyLat = sfLat + 1000; // Small offset
    int32_t nearbyLon = sfLon + 1000;
    
    // Create PositionLite struct for existing position
    meshtastic_PositionLite sfPosLite = meshtastic_PositionLite_init_default;
    sfPosLite.latitude_i = sfLat;
    sfPosLite.longitude_i = sfLon;
    sfPosLite.precision_bits = 13;
    
    // Create Position struct for incoming position
    meshtastic_Position nearbyPos = meshtastic_Position_init_default;
    nearbyPos.latitude_i = nearbyLat;
    nearbyPos.longitude_i = nearbyLon;
    nearbyPos.precision_bits = 13;
    
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(sfPosLite, nearbyPos));
    
    // Large movement - should always update
    int32_t distantLat = sfLat + (int32_t)0x100000; // Large offset
    int32_t distantLon = sfLon + (int32_t)0x100000;
    
    meshtastic_PositionLite sfPosHighLite = meshtastic_PositionLite_init_default;
    sfPosHighLite.latitude_i = sfLat;
    sfPosHighLite.longitude_i = sfLon;
    sfPosHighLite.precision_bits = 32;
    
    meshtastic_Position distantPos = meshtastic_Position_init_default;
    distantPos.latitude_i = distantLat;
    distantPos.longitude_i = distantLon;
    distantPos.precision_bits = 13;
    
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(sfPosHighLite, distantPos));
}

void test_very_low_precision() {
    // Test 4-bit precision with clear bit differences
    int32_t lat1 = (int32_t)0x80000000; // High bit set
    int32_t lon1 = (int32_t)0x40000000;
    int32_t lat2 = (int32_t)0x90000000; // Different high bits
    int32_t lon2 = (int32_t)0x50000000;
    
    // Create PositionLite struct for existing position
    meshtastic_PositionLite pos1Lite = meshtastic_PositionLite_init_default;
    pos1Lite.latitude_i = lat1;
    pos1Lite.longitude_i = lon1;
    pos1Lite.precision_bits = 4;
    
    // Create Position struct for incoming position
    meshtastic_Position pos2 = meshtastic_Position_init_default;
    pos2.latitude_i = lat2;
    pos2.longitude_i = lon2;
    pos2.precision_bits = 4;
    
    // At 4-bit precision, mask is 0xF0000000
    // 0x80000000 & 0xF0000000 = 0x80000000
    // 0x90000000 & 0xF0000000 = 0x90000000
    // These should be different, so should update
    TEST_ASSERT_TRUE(PositionModule::shouldUpdatePosition(pos1Lite, pos2));
}

void setup() {
    initializeTestEnvironment();
    UNITY_BEGIN(); // IMPORTANT LINE!
    
    // Run critical position precision tests
    RUN_TEST(test_no_existing_data);
    RUN_TEST(test_same_position_different_precision);
    RUN_TEST(test_movement_detection);
    RUN_TEST(test_sar_scenario);
    RUN_TEST(test_precision_bit_masking);
    
    exit(UNITY_END()); // stop unit testing
}

void loop() {}
