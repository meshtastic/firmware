// Regression test for the boot-time firmware_edition stamp in NodeDB's constructor.
// devicestate.my_node survives a firmware reinstall, so a vanilla build (no
// USERPREFS_FIRMWARE_EDITION) must actively reset a persisted event edition or the
// device keeps reporting DEFCON/etc. forever and clients keep the event branding.
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

// Native test builds define no USERPREFS_FIRMWARE_EDITION, so a fresh NodeDB boot here
// is exactly a "user installed Vanilla over event firmware" reboot.
static void test_vanillaBoot_resetsPersistedEventEdition(void)
{
    devicestate.my_node.firmware_edition = meshtastic_FirmwareEdition_DEFCON;
    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_DEVICESTATE));

    NodeDB *rebooted = new NodeDB();
    delete nodeDB;
    nodeDB = rebooted;

    TEST_ASSERT_EQUAL(meshtastic_FirmwareEdition_VANILLA, devicestate.my_node.firmware_edition);
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
