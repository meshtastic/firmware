#include "SerialConsole.h"
#include "concurrency/OSThread.h"
#include "gps/RTC.h"

#include "TestUtil.h"

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#include <thread>
#endif

void initializeTestEnvironment()
{
    concurrency::hasBeenSetup = true;
    consoleInit();
#if ARCH_PORTDUINO
    struct timeval tv;
    tv.tv_sec = time(NULL);
    tv.tv_usec = 0;
    perhapsSetRTC(RTCQualityNTP, &tv);
#endif
    concurrency::OSThread::setup();
}

void testDelay(unsigned long ms)
{
#if defined(ARDUINO)
    ::delay(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}