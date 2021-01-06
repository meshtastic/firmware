#include "JMTest.h"
#include <Arduino.h>

JMTest jMTest;

JMTest::JMTest() : concurrency::OSThread("JMTest") {}

int32_t JMTest::runOnce()
{
    DEBUG_MSG("JMTest::runOnce()\n");

    return (1000);
}

