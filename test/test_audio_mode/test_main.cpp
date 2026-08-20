#include "TestUtil.h"
#include "configuration.h"
#include "modules/Audio/AudioModeSelect.h"
#include <initializer_list>
#include <unity.h>

using Baud = meshtastic_ModuleConfig_AudioConfig_Audio_Baud;

#define BAUD(x) meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_##x

// Full-packet airtime in ms at CR4/5, 249 bytes on the wire. Hand-computed;
// the firmware reads these from RadioLibInterface::getPacketTime() instead.
static constexpr uint32_t AIR_SHORT_TURBO = 99;   // SF7  / 500 kHz
static constexpr uint32_t AIR_SHORT_FAST = 199;   // SF7  / 250 kHz
static constexpr uint32_t AIR_MEDIUM_TURBO = 316; // SF9  / 500 kHz
static constexpr uint32_t AIR_MEDIUM_FAST = 631;  // SF9  / 250 kHz
static constexpr uint32_t AIR_LONG_FAST = 2116;   // SF11 / 250 kHz
static constexpr uint32_t AIR_LONG_SLOW = 4559;   // SF12 / 250 kHz

void setUp() {}
void tearDown() {}

// `bitrate - 1` still yields the right id for every rate except 450, so a
// regression here breaks exactly one mode and nothing else.
static void test_codec2_ids_are_not_config_minus_one()
{
    const AudioModeInfo *m450 = audioModeFromConfig(BAUD(450));
    TEST_ASSERT_NOT_NULL(m450);
    TEST_ASSERT_EQUAL_INT(10, m450->codec2Mode);
    TEST_ASSERT_NOT_EQUAL_INT((int)BAUD(450) - 1, m450->codec2Mode);

    TEST_ASSERT_EQUAL_INT(1, audioModeFromConfig(BAUD(2400))->codec2Mode);
    TEST_ASSERT_EQUAL_INT(3, audioModeFromConfig(BAUD(1400))->codec2Mode);
    TEST_ASSERT_EQUAL_INT(8, audioModeFromConfig(BAUD(700C))->codec2Mode);
}

static void test_legacy_and_default_resolve_to_700c()
{
    for (Baud b : {BAUD(DEFAULT), BAUD(700), BAUD(700B)}) {
        const AudioModeInfo *m = audioModeFromConfig(b);
        TEST_ASSERT_NOT_NULL(m);
        TEST_ASSERT_EQUAL_INT(AUDIO_C2_700C, m->codec2Mode);
    }
}

// Unbuilt rates must land on something, never nullptr.
static void test_unbuilt_rates_fall_back()
{
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_2400, audioModeFromConfig(BAUD(3200))->codec2Mode);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_1400, audioModeFromConfig(BAUD(1600))->codec2Mode);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_1400, audioModeFromConfig(BAUD(1300))->codec2Mode);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_1400, audioModeFromConfig(BAUD(1200))->codec2Mode);
}

// Wire-visible: the receiver sizes its ring from this.
static void test_frame_geometry()
{
    struct {
        Baud cfg;
        uint16_t frames;
        uint32_t ms;
    } expect[] = {
        {BAUD(2400), 37, 740},
        {BAUD(1400), 32, 1280},
        {BAUD(700C), 56, 2240},
        {BAUD(450), 75, 3000},
    };
    for (auto &e : expect) {
        const AudioModeInfo *m = audioModeFromConfig(e.cfg);
        TEST_ASSERT_EQUAL_UINT16(e.frames, audioFramesPerPacket(*m));
        TEST_ASSERT_EQUAL_UINT32(e.ms, audioMsPerPacket(*m));
    }
}

static void test_occupancy()
{
    const AudioModeInfo *m450 = audioModeFromConfig(BAUD(450));
    const AudioModeInfo *m2400 = audioModeFromConfig(BAUD(2400));

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.71f, audioOccupancy(*m450, AIR_LONG_FAST));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.27f, audioOccupancy(*m2400, AIR_SHORT_FAST));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.13f, audioOccupancy(*m2400, AIR_SHORT_TURBO));
}

static void test_fast_preset_keeps_configured_mode()
{
    const AudioModeInfo *m = chooseAudioMode(BAUD(2400), AIR_SHORT_TURBO, 0.0f);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_2400, m->codec2Mode);
}

static void test_slow_preset_steps_down()
{
    const AudioModeInfo *m = chooseAudioMode(BAUD(2400), AIR_SHORT_FAST, 0.0f);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_1400, m->codec2Mode);

    m = chooseAudioMode(BAUD(2400), AIR_MEDIUM_FAST, 0.0f);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_450, m->codec2Mode);
}

static void test_congestion_steps_down_one_further()
{
    const AudioModeInfo *quiet = chooseAudioMode(BAUD(2400), AIR_MEDIUM_TURBO, 0.0f);
    const AudioModeInfo *busy = chooseAudioMode(BAUD(2400), AIR_MEDIUM_TURBO, 40.0f);
    TEST_ASSERT_NOT_NULL(quiet);
    TEST_ASSERT_NOT_NULL(busy);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_1400, quiet->codec2Mode);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_700C, busy->codec2Mode);

    // At the threshold is not over it.
    const AudioModeInfo *edge = chooseAudioMode(BAUD(2400), AIR_MEDIUM_TURBO, AUDIO_CONGESTION_PERCENT);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_1400, edge->codec2Mode);
}

// Degradation only.
static void test_never_upgrades_above_configured()
{
    const AudioModeInfo *m = chooseAudioMode(BAUD(450), AIR_SHORT_TURBO, 0.0f);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_450, m->codec2Mode);
}

// The only case where PTT may do nothing.
static void test_refuses_when_nothing_fits()
{
    TEST_ASSERT_NULL(chooseAudioMode(BAUD(2400), AIR_LONG_FAST, 0.0f));
    TEST_ASSERT_NULL(chooseAudioMode(BAUD(450), AIR_LONG_FAST, 0.0f));
    TEST_ASSERT_NULL(chooseAudioMode(BAUD(450), AIR_LONG_SLOW, 0.0f));
}

// Congestion must not push past the bottom of the ladder into a refusal.
static void test_congestion_does_not_cause_refusal_when_lowest_fits()
{
    const AudioModeInfo *m = chooseAudioMode(BAUD(2400), AIR_MEDIUM_FAST, 90.0f);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(AUDIO_C2_450, m->codec2Mode);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_codec2_ids_are_not_config_minus_one);
    RUN_TEST(test_legacy_and_default_resolve_to_700c);
    RUN_TEST(test_unbuilt_rates_fall_back);
    RUN_TEST(test_frame_geometry);
    RUN_TEST(test_occupancy);
    RUN_TEST(test_fast_preset_keeps_configured_mode);
    RUN_TEST(test_slow_preset_steps_down);
    RUN_TEST(test_congestion_steps_down_one_further);
    RUN_TEST(test_never_upgrades_above_configured);
    RUN_TEST(test_refuses_when_nothing_fits);
    RUN_TEST(test_congestion_does_not_cause_refusal_when_lowest_fits);
    exit(UNITY_END());
}

void loop() {}
