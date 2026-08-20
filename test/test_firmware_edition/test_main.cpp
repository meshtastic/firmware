// devicestate.my_node survives a firmware reinstall, so a vanilla build (no
// USERPREFS_FIRMWARE_EDITION) must reset a persisted event edition at boot.
#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define FE_TEST_ENTRY extern "C"
#else
#define FE_TEST_ENTRY
#endif

void setUp(void) {}
void tearDown(void) {}

static meshtastic_FirmwareEdition persistedEdition()
{
    meshtastic_DeviceState saved = meshtastic_DeviceState_init_zero;
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS, nodeDB->loadProto(deviceStateFileName, meshtastic_DeviceState_size,
                                                                      sizeof(saved), &meshtastic_DeviceState_msg, &saved));
    return saved.my_node.firmware_edition;
}

static void test_vanillaBoot_resetsPersistedEventEdition(void)
{
    devicestate.my_node.firmware_edition = meshtastic_FirmwareEdition_DEFCON;
    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_DEVICESTATE));
    TEST_ASSERT_EQUAL(meshtastic_FirmwareEdition_DEFCON, persistedEdition());

    NodeDB *rebooted = new NodeDB();
    delete nodeDB;
    nodeDB = rebooted;

    TEST_ASSERT_EQUAL(meshtastic_FirmwareEdition_VANILLA, devicestate.my_node.firmware_edition);
    // On disk too, not just in RAM: the stamp must land before the boot save decision.
    TEST_ASSERT_EQUAL(meshtastic_FirmwareEdition_VANILLA, persistedEdition());
}

FE_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    nodeDB = new NodeDB();

    UNITY_BEGIN();
    RUN_TEST(test_vanillaBoot_resetsPersistedEventEdition);
    exit(UNITY_END());
}
FE_TEST_ENTRY void loop() {}
