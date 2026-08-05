#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "RadioLibInterface.h"
#include "TestUtil.h"
#include <SPI.h>
#include <unity.h>

#include "meshtastic/config.pb.h"

class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override { releaseClientNotificationToPool(n); }
};

static MockMeshService *mockMeshService;

static LockingArduinoHal *getTestHal()
{
    static LockingArduinoHal hal(SPI, SPISettings(1000000, MSBFIRST, SPI_MODE0));
    return &hal;
}

class TestableRadioLibInterface : public RadioLibInterface
{
  public:
    TestableRadioLibInterface() : RadioLibInterface(getTestHal(), RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, nullptr) {}

    void seedNoiseFloorForTest()
    {
        noiseFloorSamples[0] = -110;
        currentSampleIndex = 1;
        isNoiseFloorBufferFull = false;
        lastNoiseFloorUpdate = 1234;
        currentNoiseFloor = -110;
    }

    uint32_t getLastNoiseFloorUpdateForTest() const { return lastNoiseFloorUpdate; }

  protected:
    void disableInterrupt() override {}
    void enableInterrupt(void (*)()) override {}
    bool isChannelActive() override { return false; }
    bool isActivelyReceiving() override { return false; }
    void addReceiveMetadata(meshtastic_MeshPacket *) override {}
    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
    int16_t getCurrentRSSI() override { return NOISE_FLOOR_DEFAULT; }
};

static void test_bwCodeToKHz_specialMappings()
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 31.25f, bwCodeToKHz(31));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 62.5f, bwCodeToKHz(62));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 203.125f, bwCodeToKHz(200));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 406.25f, bwCodeToKHz(400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 812.5f, bwCodeToKHz(800));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1625.0f, bwCodeToKHz(1600));
}

static void test_bwCodeToKHz_passthrough()
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 125.0f, bwCodeToKHz(125));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 250.0f, bwCodeToKHz(250));
}

static void test_bootstrapLoRaConfigFromPreset_noopWhenUsePresetFalse()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = false;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    cfg.bandwidth = 123;
    cfg.spread_factor = 8;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL_UINT16(123, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(8, cfg.spread_factor);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, cfg.modem_preset);
}

static void test_bootstrapLoRaConfigFromPreset_setsDerivedFields_nonWideRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL_UINT16(250, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(9, cfg.spread_factor);
}

static void test_bootstrapLoRaConfigFromPreset_setsDerivedFields_wideRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL_UINT16(800, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(9, cfg.spread_factor);
}

static void test_bootstrapLoRaConfigFromPreset_fallsBackIfBandwidthExceedsRegionSpan()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
    TEST_ASSERT_EQUAL_UINT16(250, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(11, cfg.spread_factor);
}

static void configureLongFastUs(float frequencyOffset = 0.0f)
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    config.lora.frequency_offset = frequencyOffset;
}

static void test_radioLibReconfigureResetsNoiseFloorWhenFrequencyChanges()
{
    TestableRadioLibInterface testRadioLib;
    configureLongFastUs();
    testRadioLib.reconfigure();
    testRadioLib.seedNoiseFloorForTest();

    config.lora.frequency_offset = 0.125f;
    testRadioLib.reconfigure();

    TEST_ASSERT_FALSE(testRadioLib.hasNoiseFloorSamples());
    TEST_ASSERT_EQUAL_INT32(-120, testRadioLib.getNoiseFloor());
    TEST_ASSERT_EQUAL_UINT32(0, testRadioLib.getLastNoiseFloorUpdateForTest());
}

static void test_radioLibReconfigureKeepsNoiseFloorWhenFrequencyUnchanged()
{
    TestableRadioLibInterface testRadioLib;
    configureLongFastUs();
    testRadioLib.reconfigure();
    testRadioLib.seedNoiseFloorForTest();

    testRadioLib.reconfigure();

    TEST_ASSERT_TRUE(testRadioLib.hasNoiseFloorSamples());
    TEST_ASSERT_EQUAL_INT32(-110, testRadioLib.getNoiseFloor());
    TEST_ASSERT_EQUAL_UINT32(1234, testRadioLib.getLastNoiseFloorUpdateForTest());
}

void setUp(void)
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
}

void tearDown(void)
{
    service = nullptr;
    delete mockMeshService;
    mockMeshService = nullptr;
}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_bwCodeToKHz_specialMappings);
    RUN_TEST(test_bwCodeToKHz_passthrough);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_noopWhenUsePresetFalse);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_setsDerivedFields_nonWideRegion);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_setsDerivedFields_wideRegion);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_fallsBackIfBandwidthExceedsRegionSpan);
    RUN_TEST(test_radioLibReconfigureResetsNoiseFloorWhenFrequencyChanges);
    RUN_TEST(test_radioLibReconfigureKeepsNoiseFloorWhenFrequencyUnchanged);
    exit(UNITY_END());
}

void loop() {}
