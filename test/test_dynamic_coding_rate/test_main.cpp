#include "AirtimePolicy.h"
#include "TestUtil.h"

#include <unity.h>

static uint32_t fakeAirtime(uint32_t packetLen, uint8_t codingRate, void *)
{
    return packetLen * 2 + codingRate * 100;
}

static meshtastic_MeshPacket makePacket(meshtastic_PortNum portnum,
                                        meshtastic_MeshPacket_Priority priority = meshtastic_MeshPacket_Priority_UNSET,
                                        bool wantAck = false)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = portnum;
    packet.priority = priority;
    packet.want_ack = wantAck;
    packet.from = 0x12345678;
    packet.to = 0x87654321;
    packet.id = 42;
    packet.hop_start = 3;
    packet.hop_limit = 3;
    return packet;
}

static DcrPacketContext makeContext(const AirtimePolicy &policy, const meshtastic_MeshPacket &packet,
                                    const DcrRetryContext &retry = {})
{
    meshtastic_PortNum portnum = meshtastic_PortNum_UNKNOWN_APP;
    bool classKnown = false;
    DcrPacketClass packetClass = policy.classifyPacket(packet, &portnum, &classKnown);

    return {.packet = &packet,
            .portnum = portnum,
            .packetClass = packetClass,
            .priority = packet.priority,
            .baseCr = DCR_CR_SLIM,
            .packetLen = 20,
            .predictedAirtimeMs = fakeAirtime(20, DCR_CR_SLIM, nullptr),
            .retry = retry,
            .classKnown = classKnown,
            .localOrigin = true,
            .relay = false,
            .lateRelay = false,
            .lastHop = false,
            .direct = true,
            .rxSnr = 0.0f,
            .rxRssi = 0,
            .nowMsec = 1000};
}

static ChannelAirtimeStats idleChannel()
{
    return {.channelUtilizationPercent = 1.0f, .txUtilizationPercent = 0.1f, .dutyCyclePercent = 100.0f, .queueDepth = 0};
}

static ChannelAirtimeStats congestedChannel()
{
    return {.channelUtilizationPercent = 30.0f, .txUtilizationPercent = 8.0f, .dutyCyclePercent = 100.0f, .queueDepth = 8};
}

static ChannelAirtimeStats busyChannel()
{
    return {.channelUtilizationPercent = 15.0f, .txUtilizationPercent = 3.0f, .dutyCyclePercent = 100.0f, .queueDepth = 0};
}

static DcrSettings onSettings()
{
    DcrSettings settings;
    settings.mode = meshtastic_Config_LoRaConfig_DynamicCodingRateMode_DCR_ON;
    return settings;
}

static DcrDecision choose(AirtimePolicy &policy, const meshtastic_MeshPacket &packet, const ChannelAirtimeStats &channel,
                          const DcrSettings &settings, const DcrRetryContext &retry = {})
{
    DcrPacketContext ctx = makeContext(policy, packet, retry);
    return policy.choose(ctx, channel, settings, fakeAirtime, nullptr);
}

static void test_settings_defaults_to_off_with_safety_clamps()
{
    AirtimePolicy policy;
    meshtastic_Config_LoRaConfig config = meshtastic_Config_LoRaConfig_init_zero;
    config.dcr_min_cr = 9;
    config.dcr_max_cr = 4;
    config.dcr_robust_airtime_pct = 250;

    DcrSettings settings = policy.settingsFromConfig(config);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_DynamicCodingRateMode_DCR_OFF, settings.mode);
    TEST_ASSERT_EQUAL_UINT8(DCR_CR_SLIM, settings.minCr);
    TEST_ASSERT_EQUAL_UINT8(DCR_CR_RESCUE, settings.maxCr);
    TEST_ASSERT_EQUAL_UINT8(100, settings.robustAirtimePct);
    TEST_ASSERT_TRUE(settings.trackNeighborCr);
    TEST_ASSERT_EQUAL_UINT8(DCR_CR_NORMAL, settings.telemetryMaxCr);
    TEST_ASSERT_EQUAL_UINT8(DCR_CR_SLIM, settings.userMinCr);
    TEST_ASSERT_EQUAL_UINT8(DCR_CR_ROBUST, settings.alertMinCr);
}

static void test_telemetry_uses_compact_cr_when_congested()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TELEMETRY_APP);

    DcrDecision decision = choose(policy, packet, congestedChannel(), settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_SLIM, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_CONGESTED, decision.reasonFlags);
}

static void test_idle_telemetry_is_not_rescue_by_default()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TELEMETRY_APP);

    DcrDecision decision = choose(policy, packet, idleChannel(), settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_NORMAL, decision.cr);
}

static void test_idle_text_uses_robust_cr()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP);

    DcrDecision decision = choose(policy, packet, idleChannel(), settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_ROBUST, decision.cr);
}

static void test_busy_text_can_use_slim_cr_by_default()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP);

    DcrDecision decision = choose(policy, packet, busyChannel(), settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_SLIM, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_BUSY, decision.reasonFlags);
}

static void test_alert_uses_rescue_cr_when_idle()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_ALERT_APP, meshtastic_MeshPacket_Priority_ALERT);

    DcrDecision decision = choose(policy, packet, idleChannel(), settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_RESCUE, decision.cr);
}

static void test_final_quiet_retry_escalates_text_to_rescue()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_MeshPacket_Priority_RELIABLE, true);
    DcrRetryContext retry = {.attempt = 2, .finalRetry = true, .quietLoss = true};

    DcrDecision decision = choose(policy, packet, idleChannel(), settings, retry);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_RESCUE, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_FINAL_RETRY, decision.reasonFlags);
}

static void test_congested_retry_does_not_jump_to_rescue()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_MeshPacket_Priority_RELIABLE, true);
    DcrRetryContext retry = {.attempt = 2, .finalRetry = true, .quietLoss = true};

    DcrDecision decision = choose(policy, packet, congestedChannel(), settings, retry);

    TEST_ASSERT_LESS_OR_EQUAL_UINT8(DCR_CR_ROBUST, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_COLLISION_PRESSURE, decision.reasonFlags);
}

static void test_busy_final_retry_does_not_take_quiet_loss_bonus()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_MeshPacket_Priority_RELIABLE, true);
    DcrRetryContext retry = {.attempt = 2, .finalRetry = true, .quietLoss = true};

    DcrDecision decision = choose(policy, packet, busyChannel(), settings, retry);

    TEST_ASSERT_BITS_HIGH(DCR_REASON_BUSY, decision.reasonFlags);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_COLLISION_PRESSURE, decision.reasonFlags);
    TEST_ASSERT_EQUAL_UINT32(0, decision.reasonFlags & DCR_REASON_FINAL_RETRY);
}

static void test_duty_cycle_pressure_clamps_alert()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    auto packet = makePacket(meshtastic_PortNum_ALERT_APP, meshtastic_MeshPacket_Priority_ALERT);
    ChannelAirtimeStats channel = idleChannel();
    channel.dutyCyclePercent = 1.0f;
    channel.txUtilizationPercent = 1.0f;

    DcrDecision decision = choose(policy, packet, channel, settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_ROBUST, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_DUTY_CYCLE, decision.reasonFlags);
}

static void test_token_bucket_clamps_nonurgent_rescue()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    settings.robustAirtimePct = 10;
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_MeshPacket_Priority_RELIABLE, true);
    policy.observeTxStart(packet, DCR_CR_RESCUE, 1000, false, 1000);

    DcrRetryContext retry = {.attempt = 2, .finalRetry = true, .quietLoss = true};
    DcrDecision decision = choose(policy, packet, idleChannel(), settings, retry);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_ROBUST, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_TOKEN_BUCKET, decision.reasonFlags);
}

static void test_min_cr_survives_token_bucket_clamp()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    settings.minCr = DCR_CR_RESCUE;
    settings.maxCr = DCR_CR_RESCUE;
    settings.robustAirtimePct = 10;
    auto packet = makePacket(meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_MeshPacket_Priority_RELIABLE, true);
    policy.observeTxStart(packet, DCR_CR_RESCUE, 1000, false, 1000);

    DcrRetryContext retry = {.attempt = 2, .finalRetry = true, .quietLoss = true};
    DcrDecision decision = choose(policy, packet, idleChannel(), settings, retry);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_RESCUE, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_TOKEN_BUCKET, decision.reasonFlags);
}

static void test_min_cr_survives_busy_expendable_clamp()
{
    AirtimePolicy policy;
    DcrSettings settings = onSettings();
    settings.minCr = DCR_CR_RESCUE;
    settings.maxCr = DCR_CR_RESCUE;
    settings.telemetryMaxCr = DCR_CR_RESCUE;

    DcrPacketContext ctx = {.packet = nullptr,
                            .portnum = meshtastic_PortNum_TELEMETRY_APP,
                            .packetClass = DcrPacketClass::Expendable,
                            .priority = meshtastic_MeshPacket_Priority_UNSET,
                            .baseCr = DCR_CR_SLIM,
                            .packetLen = 20,
                            .predictedAirtimeMs = fakeAirtime(20, DCR_CR_SLIM, nullptr),
                            .retry = {},
                            .classKnown = true,
                            .localOrigin = true,
                            .relay = false,
                            .lateRelay = true,
                            .lastHop = true,
                            .direct = true,
                            .rxSnr = -10.0f,
                            .rxRssi = -110,
                            .nowMsec = 1000};
    ChannelAirtimeStats channel = idleChannel();
    channel.channelUtilizationPercent = 10.0f;

    DcrDecision decision = policy.choose(ctx, channel, settings, fakeAirtime, nullptr);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_RESCUE, decision.cr);
    TEST_ASSERT_BITS_HIGH(DCR_REASON_BUSY, decision.reasonFlags);
}

static void test_off_mode_keeps_base_cr()
{
    AirtimePolicy policy;
    DcrSettings settings;
    settings.mode = meshtastic_Config_LoRaConfig_DynamicCodingRateMode_DCR_OFF;
    auto packet = makePacket(meshtastic_PortNum_ALERT_APP, meshtastic_MeshPacket_Priority_ALERT);

    DcrDecision decision = choose(policy, packet, idleChannel(), settings);

    TEST_ASSERT_EQUAL_UINT8(DCR_CR_SLIM, decision.cr);
    TEST_ASSERT_EQUAL_UINT32(DCR_REASON_NONE, decision.reasonFlags);
}

static void test_rx_observation_tracks_direct_sender()
{
    AirtimePolicy policy;

    policy.observeRx({.from = 0x12345678,
                      .relayNode = NO_RELAY_NODE,
                      .hopStart = 3,
                      .hopLimit = 3,
                      .rxCr = DCR_CR_ROBUST,
                      .airtimeMs = 420,
                      .snr = -4.0f,
                      .rssi = -95,
                      .nowMsec = 1234},
                     true);

    const NeighborCrStats *stats = policy.getNeighborStats(0x12345678);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL_UINT8(DCR_CR_ROBUST, stats->lastRxCr);
    TEST_ASSERT_EQUAL_UINT32(1, policy.getCounters().rxCr[AirtimePolicy::crIndex(DCR_CR_ROBUST)]);
}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_settings_defaults_to_off_with_safety_clamps);
    RUN_TEST(test_telemetry_uses_compact_cr_when_congested);
    RUN_TEST(test_idle_telemetry_is_not_rescue_by_default);
    RUN_TEST(test_idle_text_uses_robust_cr);
    RUN_TEST(test_busy_text_can_use_slim_cr_by_default);
    RUN_TEST(test_alert_uses_rescue_cr_when_idle);
    RUN_TEST(test_final_quiet_retry_escalates_text_to_rescue);
    RUN_TEST(test_congested_retry_does_not_jump_to_rescue);
    RUN_TEST(test_busy_final_retry_does_not_take_quiet_loss_bonus);
    RUN_TEST(test_duty_cycle_pressure_clamps_alert);
    RUN_TEST(test_token_bucket_clamps_nonurgent_rescue);
    RUN_TEST(test_min_cr_survives_token_bucket_clamp);
    RUN_TEST(test_min_cr_survives_busy_expendable_clamp);
    RUN_TEST(test_off_mode_keeps_base_cr);
    RUN_TEST(test_rx_observation_tracks_direct_sender);
    exit(UNITY_END());
}

void loop() {}
