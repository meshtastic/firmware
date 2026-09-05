#include "Arduino.h"
#include "NodeDB.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "configuration.h"
#include "mesh/JapanTxHook.h"
#include "mesh/MeshRadio.h"
#include "mesh/RadioInterface.h"
#include "mesh/RadioTxHook.h"
#include "mesh/Throttle.h"
#include <unity.h>
#include <vector>

namespace
{

class MockRadioInterface : public RadioInterface
{
  public:
    int16_t rssiToReturn = -100;
    std::vector<int16_t> rssiSequence;
    size_t sequenceIndex = 0;

    int16_t getCurrentRSSI() override
    {
        if (!rssiSequence.empty()) {
            if (sequenceIndex < rssiSequence.size()) {
                return rssiSequence[sequenceIndex++];
            }
            return rssiSequence.back();
        }
        return rssiToReturn;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 10;
    }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        (void)p;
        return ERRNO_OK;
    }
};

void setRegion(meshtastic_Config_LoRaConfig_RegionCode code)
{
    config.lora.region = code;
    initRegion();
}

} // namespace

void setUp(void)
{
    Time::useRealClock();
    Time::setTestMillis(10000);
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
}

void tearDown(void)
{
    Time::useRealClock();
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_UNSET);
}

// ---------------------------------------------------------------------------
// R2: Inter-Transmission Pause Duration Getter & Enforcement
// ---------------------------------------------------------------------------

void test_pause_duration_getter(void)
{
    TEST_ASSERT_EQUAL_UINT32(50, JapanTxHook::getTxPauseDurationMs(meshtastic_Config_LoRaConfig_RegionCode_JP));
    TEST_ASSERT_EQUAL_UINT32(0, JapanTxHook::getTxPauseDurationMs(meshtastic_Config_LoRaConfig_RegionCode_US));
    TEST_ASSERT_EQUAL_UINT32(0, JapanTxHook::getTxPauseDurationMs(meshtastic_Config_LoRaConfig_RegionCode_EU_868));

    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    TEST_ASSERT_EQUAL_UINT32(50, getTxPauseDurationMs());

    setRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL_UINT32(0, getTxPauseDurationMs());
}

void test_pause_first_transmission_no_defer(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -95;

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x1001;

    // Node has not sent any packet yet (lastTxEndTime == 0); first TX must not defer
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, &pkt));
}

void test_pause_consecutive_transmission_held_before_50ms(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -95;

    meshtastic_MeshPacket pkt1 = meshtastic_MeshPacket_init_zero;
    pkt1.id = 0x1001;
    Time::setTestMillis(1000);
    hook.postTransmit(&radio, &pkt1); // Packet 1 finished at t = 1000

    meshtastic_MeshPacket pkt2 = meshtastic_MeshPacket_init_zero;
    pkt2.id = 0x1002;

    // Attempt transmission 20ms later (t = 1020); 50ms pause not elapsed
    Time::setTestMillis(1020);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt2));
    TEST_ASSERT_EQUAL_UINT32(1050, pkt2.tx_after);

    // Attempt transmission at 49ms (t = 1049); still inside quiet window
    Time::setTestMillis(1049);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt2));
    TEST_ASSERT_EQUAL_UINT32(1050, pkt2.tx_after);

    // Attempt transmission at 50ms (t = 1050); pause elapsed -> allowed to proceed
    Time::setTestMillis(1050);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, &pkt2));
}

void test_pause_does_not_shorten_existing_longer_tx_after(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -95;

    meshtastic_MeshPacket pkt1 = meshtastic_MeshPacket_init_zero;
    pkt1.id = 0x1001;
    Time::setTestMillis(1000);
    hook.postTransmit(&radio, &pkt1);

    meshtastic_MeshPacket pkt2 = meshtastic_MeshPacket_init_zero;
    pkt2.id = 0x1002;
    pkt2.tx_after = 2000; // Scheduled far ahead

    Time::setTestMillis(1020);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt2));
    TEST_ASSERT_EQUAL_UINT32(2000, pkt2.tx_after); // Must not be overwritten to 1050
}

// ---------------------------------------------------------------------------
// R1: Continuous RSSI Carrier Sensing (5 ms window, -80 dBm threshold)
// ---------------------------------------------------------------------------

void test_carrier_sense_free_below_threshold(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -85;

    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE(hook.performCarrierSense(&radio));
}

void test_carrier_sense_busy_at_threshold(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -80; // Exactly -80 dBm threshold per ARIB STD-T108

    Time::setTestMillis(1000);
    TEST_ASSERT_FALSE(hook.performCarrierSense(&radio));
}

void test_carrier_sense_busy_above_threshold(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -70; // Strong signal/interference

    Time::setTestMillis(1000);
    TEST_ASSERT_FALSE(hook.performCarrierSense(&radio));
}

void test_carrier_sense_busy_spike_during_window(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;
    // Initial readings clear, spike on 3rd sample, then clear again
    radio.rssiSequence = {-90, -90, -75, -90, -90};

    Time::setTestMillis(1000);
    TEST_ASSERT_FALSE(hook.performCarrierSense(&radio));
}

void test_carrier_sense_threshold_boundaries(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;

    Time::setTestMillis(1000);
    radio.rssiToReturn = -81;
    TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "-81 dBm must be considered free");

    Time::setTestMillis(1000);
    radio.rssiToReturn = -80;
    TEST_ASSERT_FALSE_MESSAGE(hook.performCarrierSense(&radio), "-80 dBm must be considered busy");

    Time::setTestMillis(1000);
    radio.rssiToReturn = -79;
    TEST_ASSERT_FALSE_MESSAGE(hook.performCarrierSense(&radio), "-79 dBm must be considered busy");
}

void test_before_transmit_busy_defers_and_backs_off(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -75; // Busy

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x2001;

    Time::setTestMillis(5000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt));
    TEST_ASSERT_EQUAL_UINT32(1, hook.getBusyCount());
    // 1st backoff: range [500, 1000]
    TEST_ASSERT_TRUE(pkt.tx_after >= 5000 + 500);
    TEST_ASSERT_TRUE(pkt.tx_after <= 5000 + 1000);

    // 2nd consecutive busy
    Time::setTestMillis(6000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt));
    TEST_ASSERT_EQUAL_UINT32(2, hook.getBusyCount());
    // 2nd backoff: range [1000, 2000]
    TEST_ASSERT_TRUE(pkt.tx_after >= 6000 + 1000);
    TEST_ASSERT_TRUE(pkt.tx_after <= 6000 + 2000);

    // Channel becomes free: transmits and resets busy count
    radio.rssiToReturn = -90;
    Time::setTestMillis(9000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, &pkt));
    TEST_ASSERT_EQUAL_UINT32(0, hook.getBusyCount());
}

void test_exponential_backoff_computation(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, JapanTxHook::computeBackoffMs(0));

    for (int i = 0; i < 20; i++) {
        uint32_t b1 = JapanTxHook::computeBackoffMs(1);
        TEST_ASSERT_TRUE(b1 >= 500 && b1 <= 1000);

        uint32_t b2 = JapanTxHook::computeBackoffMs(2);
        TEST_ASSERT_TRUE(b2 >= 1000 && b2 <= 2000);

        uint32_t b3 = JapanTxHook::computeBackoffMs(3);
        TEST_ASSERT_TRUE(b3 >= 2000 && b3 <= 4000);

        uint32_t b4 = JapanTxHook::computeBackoffMs(4);
        TEST_ASSERT_TRUE(b4 >= 2000 && b4 <= 4000);

        // Clamping protects against bit-shift overflow
        uint32_t b10 = JapanTxHook::computeBackoffMs(10);
        TEST_ASSERT_TRUE(b10 >= 2000 && b10 <= 4000);
    }
}

// ---------------------------------------------------------------------------
// R3: Non-JP Region Isolation (Zero added delay, zero RSSI gating)
// ---------------------------------------------------------------------------

void test_non_jp_bypasses_carrier_sense(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -50; // Channel saturated with power

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x3001;

    // US region must ignore RSSI and return PRETX_SEND immediately
    Time::setTestMillis(1000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, &pkt));
    TEST_ASSERT_EQUAL_UINT32(0, hook.getBusyCount());
}

void test_non_jp_bypasses_pause_enforcement(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    JapanTxHook hook;
    MockRadioInterface radio;

    meshtastic_MeshPacket pkt1 = meshtastic_MeshPacket_init_zero;
    pkt1.id = 0x3001;
    Time::setTestMillis(1000);
    hook.postTransmit(&radio, &pkt1);

    meshtastic_MeshPacket pkt2 = meshtastic_MeshPacket_init_zero;
    pkt2.id = 0x3002;

    // Consecutive transmit attempt only 1ms after previous transmit
    Time::setTestMillis(1001);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, &pkt2));
}

// ---------------------------------------------------------------------------
// Edge Cases & Dispatcher Integration
// ---------------------------------------------------------------------------

void test_null_packet_is_noop(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -70;

    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, nullptr));
}

void test_dispatcher_integration(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -70; // Busy

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x4001;

    // Calling via RadioTxHooks dispatcher must route to JapanTxHook
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &pkt));

    // Clear channel
    radio.rssiToReturn = -90;
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, RadioTxHooks::beforeTransmit(&radio, &pkt));

    // Complete sending
    RadioTxHooks::postTransmit(&radio, &pkt);

    // Consecutive transmit immediately after must defer via dispatcher
    meshtastic_MeshPacket pkt2 = meshtastic_MeshPacket_init_zero;
    pkt2.id = 0x4002;
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &pkt2));
}

void test_millisecond_rollover_safety(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -90;

    // Near 32-bit wrap
    const uint32_t nearWrap = 0xFFFFFFE0u; // 32 ms before wrap
    Time::setTestMillis(nearWrap);

    meshtastic_MeshPacket pkt1 = meshtastic_MeshPacket_init_zero;
    pkt1.id = 0x5001;
    hook.postTransmit(&radio, &pkt1);

    // 20 ms after transmit (still before wrap)
    Time::setTestMillis(nearWrap + 20);
    meshtastic_MeshPacket pkt2 = meshtastic_MeshPacket_init_zero;
    pkt2.id = 0x5002;
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt2));

    // After wrap: 40 ms after transmit (total 40 < 50)
    const uint32_t pastWrap40 = nearWrap + 40; // 0x00000008 wrapped
    Time::setTestMillis(pastWrap40);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt2));

    // 55 ms after transmit (wrap passed, pause satisfied)
    const uint32_t pastWrap55 = nearWrap + 55;
    Time::setTestMillis(pastWrap55);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_SEND, hook.beforeTransmit(&radio, &pkt2));
}

void test_carrier_sense_unavailable_and_invalid_rssi_does_not_block(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;

    // Default 0 dBm (unavailable) must not be treated as busy >= -80 dBm
    radio.rssiToReturn = JapanTxHook::RSSI_UNAVAILABLE;
    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "0 dBm (unavailable) must not block");

    // Negative driver error codes (e.g. -706) must not be treated as busy
    radio.rssiToReturn = JapanTxHook::RSSI_INVALID_DRIVER_ERROR;
    Time::setTestMillis(2000);
    TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "-706 (driver error) must not block");

    // Reading beyond physical receiver floor (< -192 dBm) must not block
    radio.rssiToReturn = -193;
    Time::setTestMillis(3000);
    TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "-193 dBm (out of physical range) must not block");
}

void test_carrier_sense_ultra_low_rssi_valid_and_clear(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;

    // Real-world quiet noise floor and high-sensitivity readings (-128 to -192 dBm)
    // (e.g. SX1262 down to -141 dBm, SX1276 down to -148 dBm, SX127x HF offset down to -164 dBm)
    // must be recognized as valid RSSI and deemed free (< -80 dBm)
    const int16_t lowLevels[] = {-128, -135, -141, -148, -150, -164, -192};
    for (int16_t lvl : lowLevels) {
        radio.rssiToReturn = lvl;
        Time::setTestMillis(1000);
        TEST_ASSERT_TRUE_MESSAGE(JapanTxHook::isValidRssi(lvl), "Must be recognized as valid RSSI");
        TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "Ultra-low valid RSSI must be free channel");
    }
}

void test_packet_released_resets_busy_count(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -70; // Busy

    meshtastic_MeshPacket pkt1 = meshtastic_MeshPacket_init_zero;
    pkt1.id = 0x6001;

    Time::setTestMillis(5000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt1));
    TEST_ASSERT_EQUAL_UINT32(1, hook.getBusyCount());

    // Packet is released/dropped
    hook.packetReleased(&radio, &pkt1);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, hook.getBusyCount(), "packetReleased must reset busy count");

    // Next independent packet starts with fresh count 1 on busy
    meshtastic_MeshPacket pkt2 = meshtastic_MeshPacket_init_zero;
    pkt2.id = 0x6002;
    Time::setTestMillis(6000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt2));
    TEST_ASSERT_EQUAL_UINT32(1, hook.getBusyCount());
}

void test_deadline_wrap_zero_remapped_to_one(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -95;

    // Set lastTxEndTime so that lastTxEndTime + 50 == 0 (wrap to 0)
    hook.setLastTxEndTime(0xFFFFFFCEu); // UINT32_MAX - 49 -> + 50 wraps to 0

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x7001;

    Time::setTestMillis(0xFFFFFFCEu + 10);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, hook.beforeTransmit(&radio, &pkt));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pkt.tx_after, "Wrapped deadline 0 must be remapped to 1");
}

void test_is_valid_rssi_helper(void)
{
    // Unavailable / Default
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(0));
    // Positive values (garbage or error)
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(1));
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(10));
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(127));
    // Out of physical receiver range and RadioLib error codes (< -192)
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(-193));
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(-500));
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(-706));
    TEST_ASSERT_FALSE(JapanTxHook::isValidRssi(-1000));
    // Valid operational RSSI readings [-192, -1]
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-1));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-50));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-80));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-85));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-120));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-127));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-128));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-135));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-141));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-148));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-150));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-164));
    TEST_ASSERT_TRUE(JapanTxHook::isValidRssi(-192));
}

void test_carrier_sense_positive_rssi_and_error_codes_do_not_block(void)
{
    JapanTxHook hook;
    MockRadioInterface radio;

    // Positive RSSI (+10 dBm) must not be treated as valid busy signal
    radio.rssiToReturn = 10;
    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "+10 dBm must not block");

    // Negative error code (-706) must not be treated as valid busy signal
    radio.rssiToReturn = -706;
    Time::setTestMillis(2000);
    TEST_ASSERT_TRUE_MESSAGE(hook.performCarrierSense(&radio), "-706 must not block");
}

void test_carrier_sense_null_iface_returns_true_immediately(void)
{
    JapanTxHook hook;
    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE(hook.performCarrierSense(nullptr));
}

void test_dispatcher_packet_released_resets_busy_count(void)
{
    setRegion(meshtastic_Config_LoRaConfig_RegionCode_JP);
    JapanTxHook hook;
    MockRadioInterface radio;
    radio.rssiToReturn = -70;

    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x6003;

    Time::setTestMillis(5000);
    TEST_ASSERT_EQUAL_INT(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &pkt));
    TEST_ASSERT_EQUAL_UINT32(1, hook.getBusyCount());

    RadioTxHooks::packetReleased(&radio, &pkt);
    TEST_ASSERT_EQUAL_UINT32(0, hook.getBusyCount());
}

void test_pretx_defer_rollover_calculation(void)
{
    // Test that when nowAfter is near UINT32_MAX and tx_after has wrapped around 0,
    // unsigned subtraction tx_after - nowAfter produces the exact remaining interval
    // and deadlinePassedAt correctly reports false without tripping any > comparison.
    const uint32_t nowAfter = 0xFFFFFFFEu;
    const uint32_t txAfter = 0x00000004u; // 6 ms in the future

    TEST_ASSERT_FALSE(Throttle::deadlinePassedAt(nowAfter, txAfter));
    const uint32_t remaining = txAfter - nowAfter;
    TEST_ASSERT_EQUAL_UINT32(6, remaining);

    // Verify that the faulty (txAfter > nowAfter) check evaluates to FALSE across rollover,
    // demonstrating why the raw > check was a bug.
    TEST_ASSERT_FALSE(txAfter > nowAfter);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    RUN_TEST(test_pause_duration_getter);
    RUN_TEST(test_pause_first_transmission_no_defer);
    RUN_TEST(test_pause_consecutive_transmission_held_before_50ms);
    RUN_TEST(test_pause_does_not_shorten_existing_longer_tx_after);

    RUN_TEST(test_carrier_sense_free_below_threshold);
    RUN_TEST(test_carrier_sense_busy_at_threshold);
    RUN_TEST(test_carrier_sense_busy_above_threshold);
    RUN_TEST(test_carrier_sense_busy_spike_during_window);
    RUN_TEST(test_carrier_sense_threshold_boundaries);
    RUN_TEST(test_carrier_sense_unavailable_and_invalid_rssi_does_not_block);
    RUN_TEST(test_carrier_sense_ultra_low_rssi_valid_and_clear);
    RUN_TEST(test_is_valid_rssi_helper);
    RUN_TEST(test_carrier_sense_positive_rssi_and_error_codes_do_not_block);
    RUN_TEST(test_carrier_sense_null_iface_returns_true_immediately);
    RUN_TEST(test_before_transmit_busy_defers_and_backs_off);
    RUN_TEST(test_packet_released_resets_busy_count);
    RUN_TEST(test_dispatcher_packet_released_resets_busy_count);
    RUN_TEST(test_exponential_backoff_computation);

    RUN_TEST(test_non_jp_bypasses_carrier_sense);
    RUN_TEST(test_non_jp_bypasses_pause_enforcement);

    RUN_TEST(test_null_packet_is_noop);
    RUN_TEST(test_dispatcher_integration);
    RUN_TEST(test_millisecond_rollover_safety);
    RUN_TEST(test_deadline_wrap_zero_remapped_to_one);
    RUN_TEST(test_pretx_defer_rollover_calculation);

    exit(UNITY_END());
}

void loop() {}
