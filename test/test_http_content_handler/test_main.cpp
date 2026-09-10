#include "TestUtil.h"
#include <cstdlib>
#include <unity.h>

static void test_placeholder()
{
    TEST_ASSERT_TRUE(true);
}

extern "C" {
// Required by Unity: PlatformIO's weak defaults do not link on MinGW (PE-COFF weak externals).
void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_placeholder);
    exit(UNITY_END());
}

void loop() {}
}
