#include "TestUtil.h"
#include <unity.h>

static void test_placeholder()
{
    TEST_ASSERT_TRUE(true);
}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_placeholder);
    exit(UNITY_END());
}

void loop() {}
}
