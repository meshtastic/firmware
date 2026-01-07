#include <Arduino.h>
#include <unity.h>

#include "TestUtil.h"
// Ensure adaptive coding rate logic is available during tests
#ifndef USE_ADAPTIVE_CODING_RATE
#define USE_ADAPTIVE_CODING_RATE 1
#endif
#include "mesh/RadioInterface.h"

class TestRadio : public RadioInterface
{
  public:
    bool applyForTest(const meshtastic_MeshPacket *p) { return applyAdaptiveCodingRate(p); }

    uint8_t getAttempts(NodeNum from, PacketId id)
    {
#ifdef USE_ADAPTIVE_CODING_RATE
        auto it = adaptiveAttempts.find(adaptiveKey(from, id));
        return (it == adaptiveAttempts.end()) ? 0 : it->second.attempts;
#else
        (void)from;
        (void)id;
        return 0;
#endif
    }

#ifdef USE_ADAPTIVE_CODING_RATE
    void setAdaptiveState(NodeNum from, PacketId id, uint8_t attempts, uint32_t lastUse)
    {
        adaptiveAttempts[adaptiveKey(from, id)] = {attempts, lastUse};
    }
#endif

    uint8_t currentCr() const { return cr; }
    void setCrForTest(uint8_t value) { cr = value; }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t /*totalPacketLen*/, bool /*received*/ = false) override { return 0; }

    bool reconfigure() override
    {
        reconfigureCount++;
        lastCr = cr;
        return true;
    }

    uint32_t reconfigureCount = 0;
    uint8_t lastCr = 0;
};

void test_attempt_progression()
{
    TestRadio radio;
    meshtastic_MeshPacket packet = {};
    packet.from = 0xABCDEF01;
    packet.id = 0x1;

    TEST_ASSERT_FALSE(radio.applyForTest(&packet));
    TEST_ASSERT_EQUAL_UINT8(1, radio.getAttempts(packet.from, packet.id));
    TEST_ASSERT_EQUAL_UINT8(5, radio.currentCr());
    TEST_ASSERT_EQUAL_UINT32(0, radio.reconfigureCount);

    TEST_ASSERT_TRUE(radio.applyForTest(&packet));
    TEST_ASSERT_EQUAL_UINT8(2, radio.getAttempts(packet.from, packet.id));
    TEST_ASSERT_EQUAL_UINT8(7, radio.currentCr());
    TEST_ASSERT_EQUAL_UINT32(1, radio.reconfigureCount);
    TEST_ASSERT_EQUAL_UINT8(7, radio.lastCr);

    TEST_ASSERT_TRUE(radio.applyForTest(&packet));
    TEST_ASSERT_EQUAL_UINT8(3, radio.getAttempts(packet.from, packet.id));
    TEST_ASSERT_EQUAL_UINT8(8, radio.currentCr());
    TEST_ASSERT_EQUAL_UINT32(2, radio.reconfigureCount);
    TEST_ASSERT_EQUAL_UINT8(8, radio.lastCr);
}

void test_attempts_are_per_packet()
{
    TestRadio radio;
    meshtastic_MeshPacket first = {};
    first.from = 0x1001;
    first.id = 0xA;

    meshtastic_MeshPacket second = {};
    second.from = 0x1001;
    second.id = 0xB;

    radio.applyForTest(&first);
    radio.applyForTest(&second);
    radio.applyForTest(&first);

    TEST_ASSERT_EQUAL_UINT8(2, radio.getAttempts(first.from, first.id));
    TEST_ASSERT_EQUAL_UINT8(1, radio.getAttempts(second.from, second.id));
    TEST_ASSERT_EQUAL_UINT8(7, radio.currentCr());
}

void test_clear_resets_attempts_and_rate()
{
    TestRadio radio;
    meshtastic_MeshPacket packet = {};
    packet.from = 0xCAFE;
    packet.id = 0x55;

    radio.applyForTest(&packet);
    radio.applyForTest(&packet);
    radio.applyForTest(&packet);

    radio.reconfigureCount = 0;
    radio.setCrForTest(8);
    radio.clearAdaptiveCodingRateState(packet.from, packet.id);

    TEST_ASSERT_TRUE(radio.applyForTest(&packet));
    TEST_ASSERT_EQUAL_UINT8(1, radio.getAttempts(packet.from, packet.id));
    TEST_ASSERT_EQUAL_UINT8(5, radio.currentCr());
    TEST_ASSERT_EQUAL_UINT32(1, radio.reconfigureCount);
}

void test_prunes_expired_state()
{
    TestRadio radio;
    meshtastic_MeshPacket packet = {};
    packet.from = 0xBEEF;
    packet.id = 0x99;

    radio.applyForTest(&packet);
#ifdef USE_ADAPTIVE_CODING_RATE
    const uint32_t now = millis();
    radio.setAdaptiveState(packet.from, packet.id, 3, now - (5 * 60 * 1000UL + 50));
#endif
    radio.reconfigureCount = 0;
    radio.setCrForTest(5);

    TEST_ASSERT_FALSE(radio.applyForTest(&packet));
    TEST_ASSERT_EQUAL_UINT8(1, radio.getAttempts(packet.from, packet.id));
    TEST_ASSERT_EQUAL_UINT32(0, radio.reconfigureCount);
}

void setup()
{
    printf("AdaptiveCodingRate test setup start\n");
    fflush(stdout);
    // Use minimal init to avoid pulling in SerialConsole/portduino peripherals for these logic-only tests
    initializeTestEnvironmentMinimal();

    printf("AdaptiveCodingRate test init done\n");
    fflush(stdout);

    UNITY_BEGIN();
    RUN_TEST(test_attempt_progression);
    RUN_TEST(test_attempts_are_per_packet);
    RUN_TEST(test_clear_resets_attempts_and_rate);
    RUN_TEST(test_prunes_expired_state);
    UNITY_END();
}

void loop()
{
    delay(1000);
}
